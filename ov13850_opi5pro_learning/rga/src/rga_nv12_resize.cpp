#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "im2d.hpp"

/*
 * 阶段 3 的最小 RGA 业务闭环：
 *
 *   RKISP 已采集的 1920x1080 NV12 文件
 *                  |
 *                  v
 *       读入进程虚拟内存（src_data）
 *                  |
 *                  v
 *   importbuffer_virtualaddr + wrapbuffer_handle
 *                  |
 *                  v
 *          RGA imresize（同步执行）
 *                  |
 *                  v
 *       1280x720 NV12 内存（dst_data）-> 输出文件
 *
 * 这个程序故意不接 V4L2 实时队列和 DMA-BUF。先把文件尺寸、NV12 内存布局、
 * librga/内核驱动兼容性和纯缩放耗时隔离验证，后续实时链路可以复用相同的
 * RGA buffer 描述和错误处理。
 */
namespace {

/*
 * 第一版固定输入输出，避免“任意尺寸/任意格式”掩盖硬件接口问题。NV12 由
 * 全分辨率 Y 平面和半尺寸交错 UV 平面组成，总字节数为 width*height*3/2。
 */
constexpr int kSrcWidth = 1920;
constexpr int kSrcHeight = 1080;
constexpr int kDstWidth = 1280;
constexpr int kDstHeight = 720;

constexpr int kWarmups = 5;
constexpr int kIterations = 100;

constexpr int kFormat = RK_FORMAT_YCbCr_420_SP;

constexpr std::size_t kSrcSize =
	static_cast<std::size_t>(kSrcWidth) * kSrcHeight * 3 / 2;

constexpr std::size_t kDstSize =
	static_cast<std::size_t>(kDstWidth) * kDstHeight * 3 / 2;

static_assert(kSrcWidth % 2 == 0);
static_assert(kSrcHeight % 2 == 0);
static_assert(kDstWidth % 2 == 0);
static_assert(kDstHeight % 2 == 0);

/*
 * 普通 vector 内存先导入 librga，得到可提交给 RGA 驱动的 handle。该类只
 * 表达 handle 的所有权：构造时导入，析构时释放，禁止复制以避免 double
 * release。src/dst 各导入一次，5+100 次缩放复用同一个 handle。
 */
class ImportedBuffer {
public:
	ImportedBuffer(void *address, std::size_t size)
		: handle_(importbuffer_virtualaddr(
			  address, static_cast<int>(size)))
	{
	}

	~ImportedBuffer()
	{
		if (handle_ != 0)
			releasebuffer_handle(handle_);
	}

	ImportedBuffer(const ImportedBuffer &) = delete;
	ImportedBuffer &operator=(const ImportedBuffer &) = delete;

	bool valid() const
	{
		return handle_ != 0;
	}

	rga_buffer_handle_t get() const
	{
		return handle_;
	}

private:
	rga_buffer_handle_t handle_ = 0;
};

bool read_exact_frame(const char *path,
		      std::vector<unsigned char> &buffer)
{
	/*
	 * ios::ate 先定位文件末尾取得大小。文件必须恰好是一帧，不接受短帧、
	 * 多帧拼接或其他分辨率，避免 RGA 按错误布局访问越界数据。
	 */
	std::ifstream input(path, std::ios::binary | std::ios::ate);

	if (!input) {
		std::cerr << "ERROR: cannot open input: "
			  << path << '\n';
		return false;
	}

	const std::streamoff size = input.tellg();

	if (size != static_cast<std::streamoff>(buffer.size())) {
		std::cerr << "ERROR: input size=" << size
			  << " expected=" << buffer.size() << '\n';
		return false;
	}

	input.seekg(0, std::ios::beg);
	input.read(reinterpret_cast<char *>(buffer.data()),
		   static_cast<std::streamsize>(buffer.size()));

	if (!input) {
		std::cerr << "ERROR: failed to read complete input frame\n";
		return false;
	}

	return true;
}

bool write_exact_frame(const char *path,
		       const std::vector<unsigned char> &buffer)
{
	/*
	 * 输出写入必须完整成功；发生磁盘或 I/O 错误时删除残缺文件，避免后续
	 * 验证只看到文件存在就把失败结果误认为有效 NV12 帧。
	 */
	std::ofstream output(path, std::ios::binary | std::ios::trunc);

	if (!output) {
		std::cerr << "ERROR: cannot create output: "
			  << path << '\n';
		return false;
	}

	output.write(reinterpret_cast<const char *>(buffer.data()),
		     static_cast<std::streamsize>(buffer.size()));
	output.close();

	if (!output) {
		std::cerr << "ERROR: failed to write complete output frame\n";
		std::remove(path);
		return false;
	}

	return true;
}

bool resize_once(const rga_buffer_t &src, rga_buffer_t &dst)
{
	/* 默认 sync=1：函数返回成功时，目标 buffer 已经可以由 CPU 读取。 */
	const IM_STATUS status = imresize(src, dst);

	if (status != IM_STATUS_SUCCESS) {
		std::cerr << "ERROR: imresize failed: "
			  << imStrError(status) << '\n';
		return false;
	}

	return true;
}

} // namespace

int main(int argc, char **argv)
{
	/* 命令行接口固定为一个输入帧和一个输出帧。 */
	if (argc != 3) {
		std::cerr
			<< "ERROR: usage: "
			<< argv[0]
			<< " <input-1920x1080.nv12>"
			<< " <output-1280x720.nv12>\n";
		return 2;
	}

	/* 必须在删除旧输出之前拒绝同路径，防止把输入帧当成旧输出删掉。 */
	if (std::strcmp(argv[1], argv[2]) == 0) {
		std::cerr
			<< "ERROR: input and output paths must differ\n";
		return 2;
	}

	/* 清除上次结果，保证本次失败时不会遗留一个“看似成功”的旧文件。 */
	std::remove(argv[2]);

	try {
		std::vector<unsigned char> src_data(kSrcSize);
		std::vector<unsigned char> dst_data(kDstSize, 0);

		if (!read_exact_frame(argv[1], src_data))
			return 3;

		/*
		 * import 让 librga/驱动认识内存；wrap 只给 handle 附加宽、高、步幅
		 * 和 NV12 格式，不复制像素。内存实际所有权仍由两个 vector 持有。
		 */
		ImportedBuffer src_handle(
			src_data.data(), src_data.size());
		ImportedBuffer dst_handle(
			dst_data.data(), dst_data.size());

		if (!src_handle.valid() || !dst_handle.valid()) {
			std::cerr
				<< "ERROR: importbuffer_virtualaddr failed\n";
			return 4;
		}

		rga_buffer_t src = wrapbuffer_handle(
			src_handle.get(), kSrcWidth, kSrcHeight, kFormat);
		rga_buffer_t dst = wrapbuffer_handle(
			dst_handle.get(), kDstWidth, kDstHeight, kFormat);

		/*
		 * 空 rect 表示处理整幅图。imcheck 在提交前验证格式、分辨率和缩放比
		 * 是否受当前 RGA 硬件/驱动支持，比直接失败在 ioctl 更容易定位。
		 */
		im_rect empty = {};
		const IM_STATUS check = imcheck(src, dst, empty, empty);

		if (check != IM_STATUS_NOERROR) {
			std::cerr << "ERROR: imcheck failed: "
				  << imStrError(check) << '\n';
			return 5;
		}

		const char *version = querystring(RGA_VERSION);

		std::cout
			<< "librga="
			<< (version != nullptr ? version : "unknown")
			<< '\n';
		std::cout << "input=1920x1080 NV12 bytes="
			  << kSrcSize << '\n';
		std::cout << "output=1280x720 NV12 bytes="
			  << kDstSize << '\n';
		std::cout << "warmups=" << kWarmups
			  << " iterations=" << kIterations << '\n';

		/*
		 * 预热不计时，用于排除首次打开设备、建立映射和时钟状态切换造成的
		 * 一次性抖动；正式统计只覆盖 100 次同步 imresize。
		 */
		for (int i = 0; i < kWarmups; ++i) {
			if (!resize_once(src, dst))
				return 6;
		}

		const auto start = std::chrono::steady_clock::now();

		for (int i = 0; i < kIterations; ++i) {
			if (!resize_once(src, dst))
				return 6;
		}

		const auto end = std::chrono::steady_clock::now();

		/* 最后一次同步缩放的结果留在 dst_data，计时结束后再写盘。 */
		if (!write_exact_frame(argv[2], dst_data))
			return 7;

		const double total_us =
			std::chrono::duration<double, std::micro>(
				end - start).count();
		const double average_us = total_us / kIterations;
		/*
		 * operations_per_second 是纯 RGA resize 吞吐估算，不包含文件 I/O、
		 * V4L2 排队或摄像头曝光时间，因此不能等同于端到端摄像头 FPS。
		 */
		const double operations_per_second =
			average_us > 0.0 ? 1000000.0 / average_us : 0.0;

		std::cout << std::fixed << std::setprecision(2)
			  << "total_us=" << total_us
			  << " average_us=" << average_us
			  << " operations_per_second="
			  << operations_per_second << '\n';
		std::cout << "RGA_RESIZE_OK\n";
	} catch (const std::exception &error) {
		std::cerr << "ERROR: exception: "
			  << error.what() << '\n';
		return 8;
	}

	return 0;
}
