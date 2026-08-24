#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "im2d.hpp"

/*
 * 阶段 3 第一版实时 RGA copy path：
 *
 *   OV13850 -> CIF/RKISP -> /dev/video11 (1920x1080 NV12)
 *                            |
 *                            v
 *                     V4L2 MMAP buffer
 *                            |
 *                     DQBUF 后 memcpy
 *                            |
 *               立即 QBUF，把 capture buffer 还给驱动
 *                            |
 *                     独立源内存 -> RGA
 *                            |
 *                     1280x720 NV12
 *
 * 显式 copy 让 V4L2 buffer 所有权与 RGA 生命周期解耦：QBUF 后驱动可以立即
 * 填充下一帧，而同步 RGA 只访问用户态长期持有的 source_。该版本先验证队列
 * 时序和稳定性，不宣称零拷贝；direct-MMAP 和 DMA-BUF 留给后续对比实验。
 */
namespace {

using SteadyClock = std::chrono::steady_clock;

/*
 * 固定数据契约对应已经验证的 RKISP mainpath。第一版不接受任意格式，避免把
 * 格式协商、通用 stride 处理和 RGA 实时队列三个问题混在一起。
 */
constexpr int kSrcWidth = 1920;
constexpr int kSrcHeight = 1080;
constexpr int kDstWidth = 1280;
constexpr int kDstHeight = 720;
constexpr std::size_t kSrcSize = 3110400;
constexpr std::size_t kDstSize = 1382400;
constexpr unsigned int kRequestedBuffers = 4;
constexpr unsigned int kSkipFrames = 3;
constexpr unsigned int kProcessFrames = 300;
constexpr int kPollTimeoutMs = 2000;
constexpr int kRgaFormat = RK_FORMAT_YCbCr_420_SP;

static_assert(kSrcSize ==
	static_cast<std::size_t>(kSrcWidth) * kSrcHeight * 3 / 2);
static_assert(kDstSize ==
	static_cast<std::size_t>(kDstWidth) * kDstHeight * 3 / 2);

std::runtime_error system_error(const char *operation)
{
	/* 在调用其他 libc 函数前保存 errno，避免原始系统调用错误被覆盖。 */
	const int saved_errno = errno;

	return std::runtime_error(std::string(operation) + ": " +
				  std::strerror(saved_errno));
}

int xioctl(int fd, unsigned long request, void *argument)
{
	/* signal 中断不代表 ioctl 业务失败，EINTR 时重试原请求。 */
	int ret;

	do {
		ret = ioctl(fd, request, argument);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

double elapsed_us(const SteadyClock::time_point &start,
		  const SteadyClock::time_point &end)
{
	return std::chrono::duration<double, std::micro>(end - start).count();
}

struct CapturedFrame {
	/*
	 * CapturedFrame 只在 DQBUF 到下一次 QBUF 之间有效。data 指向驱动映射的
	 * MMAP 内存；QBUF 后驱动重新拥有该地址，调用者不得继续读取。
	 */
	unsigned int index;
	unsigned char *data;
	std::size_t length;
	std::size_t bytes_used;
	std::uint32_t sequence;
};

struct MappedBuffer {
	void *address = MAP_FAILED;
	std::size_t length = 0;
};

class VideoCapture {
public:
	/*
	 * VideoCapture 独占 video fd、所有 MMAP 地址和 streaming 状态。构造函数
	 * 完成一半时 C++ 不会调用本对象析构，因此 catch 中必须主动回收已取得资源。
	 */
	explicit VideoCapture(const char *device)
	{
		try {
			initialize(device);
		} catch (...) {
			cleanup();
			throw;
		}
	}

	~VideoCapture()
	{
		cleanup();
	}

	VideoCapture(const VideoCapture &) = delete;
	VideoCapture &operator=(const VideoCapture &) = delete;

	void start()
	{
		/*
		 * STREAMON 前必须先把全部 buffers 放进驱动队列，否则 ISP 没有可写
		 * 目标。4 个 buffers 在采集、用户处理和驱动排队之间提供缓冲余量。
		 */
		if (streaming_)
			return;

		for (unsigned int i = 0; i < buffers_.size(); ++i)
			requeue(i);

		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
			throw system_error("VIDIOC_STREAMON");

		streaming_ = true;
	}

	void stop()
	{
		/* STREAMOFF 取消队列并触发上游 sensor/CIF/ISP 停流。 */
		if (!streaming_)
			return;

		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
			throw system_error("VIDIOC_STREAMOFF");

		streaming_ = false;
	}

	CapturedFrame dequeue(unsigned int &timeouts)
	{
		/*
		 * 30 fps 正常约 33 ms 到一帧；2 秒仍无帧说明链路已经异常，不能无限
		 * 阻塞。poll 只负责等待 ready，真正取得所有权仍以 DQBUF 成功为准。
		 */
		pollfd descriptor = {};
		descriptor.fd = fd_;
		descriptor.events = POLLIN | POLLPRI;

		int poll_ret;
		do {
			poll_ret = poll(&descriptor, 1, kPollTimeoutMs);
		} while (poll_ret < 0 && errno == EINTR);

		if (poll_ret < 0)
			throw system_error("poll");
		if (poll_ret == 0) {
			++timeouts;
			throw std::runtime_error(
				"poll timeout after 2000 ms, timeouts=" +
				std::to_string(timeouts));
		}
		if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
			throw std::runtime_error("poll reported a device error");

		/* multiplanar API 即使当前 NV12 只有一个 plane，也必须传 planes 数组。 */
		v4l2_plane planes[VIDEO_MAX_PLANES] = {};
		v4l2_buffer buffer = {};
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.length = VIDEO_MAX_PLANES;
		buffer.m.planes = planes;

		if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0)
			throw system_error("VIDIOC_DQBUF");
		if (buffer.index >= buffers_.size())
			throw std::runtime_error("VIDIOC_DQBUF returned invalid index");
		if (buffer.length < 1)
			throw std::runtime_error("VIDIOC_DQBUF returned no planes");
		/*
		 * 本版固定处理 plane 起始地址和完整连续 NV12。遇到 data_offset 或短帧
		 * 主动拒绝，避免 memcpy 越界或把 metadata 当成 Y 数据。
		 */
		if (planes[0].data_offset != 0)
			throw std::runtime_error("plane 0 data_offset is not zero");
		if (planes[0].bytesused < kSrcSize)
			throw std::runtime_error("plane 0 bytesused is smaller than 3110400");
		if (planes[0].bytesused > buffers_[buffer.index].length)
			throw std::runtime_error("plane 0 bytesused exceeds mmap length");

		return {
			buffer.index,
			static_cast<unsigned char *>(buffers_[buffer.index].address),
			buffers_[buffer.index].length,
			planes[0].bytesused,
			buffer.sequence,
		};
	}

	void requeue(unsigned int index)
	{
		/* QBUF 把该 index 的所有权交还驱动，之后用户态不再访问其地址。 */
		if (index >= buffers_.size())
			throw std::runtime_error("QBUF index is out of range");

		v4l2_plane planes[VIDEO_MAX_PLANES] = {};
		v4l2_buffer buffer = {};
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = index;
		buffer.length = 1;
		buffer.m.planes = planes;
		planes[0].length = buffers_[index].length;

		if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0)
			throw system_error("VIDIOC_QBUF");
	}

private:
	void initialize(const char *device)
	{
		/* O_NONBLOCK 配合 poll，保证等待策略和 2 秒超时由程序自己控制。 */
		fd_ = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd_ < 0)
			throw system_error("open video device");

		v4l2_capability capability = {};
		if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0)
			throw system_error("VIDIOC_QUERYCAP");

		/* DEVICE_CAPS 存在时使用具体节点能力，而不是整个物理设备能力集合。 */
		const std::uint32_t caps =
			(capability.capabilities & V4L2_CAP_DEVICE_CAPS) ?
			capability.device_caps : capability.capabilities;

		if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE))
			throw std::runtime_error(
				"device does not support multiplanar capture");
		if (!(caps & V4L2_CAP_STREAMING))
			throw std::runtime_error(
				"device does not support streaming I/O");

		/*
		 * 程序只回读并验证，不修改 sensor/crop/mainpath。媒体链路配置继续由
		 * configure_rkisp_1080p.sh 负责，使配置失败与 capture 失败边界清楚。
		 */
		v4l2_format format = {};
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (xioctl(fd_, VIDIOC_G_FMT, &format) < 0)
			throw system_error("VIDIOC_G_FMT");

		const v4l2_pix_format_mplane &pixel = format.fmt.pix_mp;
		if (pixel.width != kSrcWidth || pixel.height != kSrcHeight ||
		    pixel.pixelformat != V4L2_PIX_FMT_NV12 ||
		    pixel.num_planes != 1 ||
		    pixel.plane_fmt[0].bytesperline != kSrcWidth ||
		    pixel.plane_fmt[0].sizeimage < kSrcSize) {
			std::ostringstream message;
			message << "unexpected format: width=" << pixel.width
				<< " height=" << pixel.height
				<< " fourcc=0x" << std::hex << pixel.pixelformat
				<< std::dec << " planes=" << pixel.num_planes
				<< " stride=" << pixel.plane_fmt[0].bytesperline
				<< " sizeimage=" << pixel.plane_fmt[0].sizeimage;
			throw std::runtime_error(message.str());
		}

		/* 请求 4 个驱动管理的 MMAP capture buffers。少于 2 个无法形成流水。 */
		v4l2_requestbuffers request = {};
		request.count = kRequestedBuffers;
		request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		request.memory = V4L2_MEMORY_MMAP;
		if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0)
			throw system_error("VIDIOC_REQBUFS");
		if (request.count < 2)
			throw std::runtime_error("driver returned fewer than 2 buffers");

		/*
		 * QUERYBUF 返回每个 index 的 plane 长度和 mmap offset；映射后地址在
		 * 整个 VideoCapture 生命周期保持不变，但访问权随 QBUF/DQBUF 转移。
		 */
		buffers_.reserve(request.count);
		for (unsigned int i = 0; i < request.count; ++i) {
			v4l2_plane planes[VIDEO_MAX_PLANES] = {};
			v4l2_buffer buffer = {};
			buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = i;
			buffer.length = VIDEO_MAX_PLANES;
			buffer.m.planes = planes;

			if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0)
				throw system_error("VIDIOC_QUERYBUF");
			if (buffer.length < 1 || planes[0].length < kSrcSize)
				throw std::runtime_error("MMAP plane is smaller than input frame");

			void *address = mmap(nullptr, planes[0].length,
					     PROT_READ | PROT_WRITE, MAP_SHARED,
					     fd_, planes[0].m.mem_offset);
			if (address == MAP_FAILED)
				throw system_error("mmap");

			buffers_.push_back({address, planes[0].length});
		}
	}

	void cleanup() noexcept
	{
		/*
		 * 清理幂等且不能抛异常：先停止硬件队列，再解除映射，最后关闭 fd。
		 * 该函数同时服务构造失败和正常析构，因此每一步都检查当前状态。
		 */
		if (fd_ >= 0 && streaming_) {
			v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			xioctl(fd_, VIDIOC_STREAMOFF, &type);
			streaming_ = false;
		}

		for (MappedBuffer &buffer : buffers_) {
			if (buffer.address != MAP_FAILED)
				munmap(buffer.address, buffer.length);
		}
		buffers_.clear();

		if (fd_ >= 0) {
			close(fd_);
			fd_ = -1;
		}
	}

	int fd_ = -1;
	bool streaming_ = false;
	std::vector<MappedBuffer> buffers_;
};

class ImportedBuffer {
public:
	/*
	 * RGA handle 的 RAII 所有者。构造时把长期用户内存导入 librga，析构时
	 * 释放；禁止复制，避免同一 handle 被 release 两次。
	 */
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

class RgaResizer {
public:
	RgaResizer()
		: source_(kSrcSize), output_(kDstSize, 0),
		  source_handle_(source_.data(), source_.size()),
		  output_handle_(output_.data(), output_.size())
	{
		/*
		 * source_/output_ 只分配和导入一次，300 帧复用同一地址。wrap 只补充
		 * 宽、高、stride 和格式元数据，不复制像素；imcheck 只需初始化时做一次。
		 */
		if (!source_handle_.valid() || !output_handle_.valid())
			throw std::runtime_error("importbuffer_virtualaddr failed");

		source_buffer_ = wrapbuffer_handle(
			source_handle_.get(), kSrcWidth, kSrcHeight, kRgaFormat);
		output_buffer_ = wrapbuffer_handle(
			output_handle_.get(), kDstWidth, kDstHeight, kRgaFormat);

		im_rect empty = {};
		const IM_STATUS status =
			imcheck(source_buffer_, output_buffer_, empty, empty);
		if (status != IM_STATUS_NOERROR)
			throw std::runtime_error(
				std::string("imcheck failed: ") + imStrError(status));
	}

	unsigned char *source_data()
	{
		return source_.data();
	}

	const std::vector<unsigned char> &output_data() const
	{
		return output_;
	}

	void resize()
	{
		/* 默认同步执行；返回成功后 output_ 才能安全保存或交给下一处理节点。 */
		const IM_STATUS status = imresize(source_buffer_, output_buffer_);
		if (status != IM_STATUS_SUCCESS)
			throw std::runtime_error(
				std::string("imresize failed: ") + imStrError(status));
	}

private:
	std::vector<unsigned char> source_;
	std::vector<unsigned char> output_;
	ImportedBuffer source_handle_;
	ImportedBuffer output_handle_;
	rga_buffer_t source_buffer_ = {};
	rga_buffer_t output_buffer_ = {};
};

void write_output(const char *path,
		  const std::vector<unsigned char> &data)
{
	/*
	 * 只保存第 300 帧，避免磁盘 I/O 进入实时循环。写不完整时删除残缺文件，
	 * 防止仅凭“文件存在”误判本次实验成功。
	 */
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output)
		throw std::runtime_error("cannot create output file");

	output.write(reinterpret_cast<const char *>(data.data()),
		     static_cast<std::streamsize>(data.size()));
	output.close();
	if (!output) {
		std::remove(path);
		throw std::runtime_error("failed to write complete output file");
	}
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 3) {
		std::cerr << "ERROR: usage: " << argv[0]
			  << " <video-device> <last-output-1280x720.nv12>\n";
		return 2;
	}

	/* 删除旧结果；本次任一步失败时不能让上次输出冒充新结果。 */
	std::remove(argv[2]);
	unsigned int timeouts = 0;

	try {
		/* capture 与 resizer 各自拥有资源，main 只负责编排数据流。 */
		VideoCapture capture(argv[1]);
		RgaResizer resizer;
		capture.start();

		/* 启动帧只做 DQ/Q，让曝光、ISP 和队列进入稳定状态，不计入性能。 */
		for (unsigned int i = 0; i < kSkipFrames; ++i) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			capture.requeue(frame.index);
		}

		double copy_total_us = 0.0;
		double rga_total_us = 0.0;
		std::uint64_t dropped = 0;
		std::uint32_t previous_sequence = 0;
		bool have_previous_sequence = false;
		const auto loop_start = SteadyClock::now();

		for (unsigned int i = 0; i < kProcessFrames; ++i) {
			const CapturedFrame frame = capture.dequeue(timeouts);

			/*
			 * sequence 应逐帧加 1；无符号减法可自然跨越 u32 回绕。delta=0
			 * 代表重复序号，delta>1 代表中间缺帧。
			 */
			if (have_previous_sequence) {
				const std::uint32_t delta =
					frame.sequence - previous_sequence;
				if (delta == 0)
					++dropped;
				else if (delta > 1)
					dropped += static_cast<std::uint64_t>(delta - 1);
			}
			previous_sequence = frame.sequence;
			have_previous_sequence = true;

			/*
			 * 计时显式 copy。复制完成后立即 QBUF，随后 RGA 访问独立 source_，
			 * 不再持有 capture buffer，因此驱动可并行采集下一帧。
			 */
			const auto copy_start = SteadyClock::now();
			std::memcpy(resizer.source_data(), frame.data, kSrcSize);
			const auto copy_end = SteadyClock::now();
			copy_total_us += elapsed_us(copy_start, copy_end);

			capture.requeue(frame.index);

			/* RGA 单独计时，便于后续与 direct-MMAP 版本公平比较。 */
			const auto rga_start = SteadyClock::now();
			resizer.resize();
			const auto rga_end = SteadyClock::now();
			rga_total_us += elapsed_us(rga_start, rga_end);
		}

		const auto loop_end = SteadyClock::now();
		/* 先停 capture、归还上游 PM 引用，再把循环外的最后一帧写盘。 */
		capture.stop();
		write_output(argv[2], resizer.output_data());

		const double loop_total_s =
			std::chrono::duration<double>(loop_end - loop_start).count();
		const double copy_average_us = copy_total_us / kProcessFrames;
		const double rga_average_us = rga_total_us / kProcessFrames;
		/*
		 * loop FPS 包含等帧、ioctl、copy 和 RGA，不包含初始化、预丢弃和写盘；
		 * 它是本程序吞吐，不是显示/编码/网络端到端延迟。
		 */
		const double capture_process_fps =
			loop_total_s > 0.0 ? kProcessFrames / loop_total_s : 0.0;
		const char *version = querystring(RGA_VERSION);

		std::cout << "librga="
			  << (version != nullptr ? version : "unknown") << '\n';
		std::cout << "input=1920x1080 NV12 bytes=" << kSrcSize << '\n';
		std::cout << "output=1280x720 NV12 bytes=" << kDstSize << '\n';
		std::cout << "pre_skipped=" << kSkipFrames
			  << " processed=" << kProcessFrames
			  << " timeouts=" << timeouts
			  << " dropped=" << dropped << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "copy_total_us=" << copy_total_us
			  << " copy_average_us=" << copy_average_us << '\n'
			  << "rga_total_us=" << rga_total_us
			  << " rga_average_us=" << rga_average_us << '\n'
			  << "loop_total_s=" << loop_total_s
			  << " capture_process_fps=" << capture_process_fps << '\n';
		std::cout << "RGA_V4L2_LIVE_OK\n";
	} catch (const std::exception &error) {
		/* 栈展开先触发 RAII 清理，再统一删除输出并转成稳定的 ERROR: 契约。 */
		std::remove(argv[2]);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}

	return 0;
}
