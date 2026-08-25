#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mpp_encoder_core.hpp"

namespace {

/*
 * 实时前端连接 RKISP mainpath 与 MPP：
 *   copy   : V4L2 MMAP -> CPU 逐行复制 -> MPP 内部 DRM buffer；
 *   dmabuf : V4L2 MMAP/EXPBUF -> MPP 导入同一 DMA-BUF。
 * 两条路径使用同一编码核心和 300 帧验收窗口，以便比较搬运成本而不是比较
 * 两套编码器。当前工具固定接收已配置好的 1920x1080 NV12 /dev/video11。
 */

using namespace camera_mpp;
using Clock = std::chrono::steady_clock;

constexpr unsigned int kBuffers = 4;
constexpr unsigned int kSkipFrames = 3;
constexpr unsigned int kFrames = 300;
constexpr int kPollTimeoutMs = 2000;

int xioctl(int fd, unsigned long request, void *argument)
{
	int ret;
	do {
		ret = ioctl(fd, request, argument);
	} while (ret < 0 && errno == EINTR);
	return ret;
}

std::runtime_error system_error(const char *operation)
{
	const int saved = errno;
	return std::runtime_error(std::string(operation) + ": " +
				  std::strerror(saved));
}

double elapsed_us(const Clock::time_point &start, const Clock::time_point &end)
{
	return std::chrono::duration<double, std::micro>(end - start).count();
}

struct MappedBuffer {
	/*
	 * address 供 copy 路径读取；export_fd 和 mpp_buffer 只在 DMA-BUF 路径
	 * 使用。销毁顺序必须先释放 MPP import，再关闭 fd，最后解除 mmap。
	 */
	void *address = MAP_FAILED;
	std::size_t length = 0;
	int export_fd = -1;
	MppBuffer mpp_buffer = nullptr;
};

struct CapturedFrame {
	unsigned int index;
	const unsigned char *data;
	std::uint32_t sequence;
};

class VideoCapture {
public:
	VideoCapture(const char *device, bool export_dmabuf)
	{
		try {
			initialize(device, export_dmabuf);
		} catch (...) {
			cleanup();
			throw;
		}
	}

	~VideoCapture()
	{
		cleanup();
	}

	void start()
	{
		for (unsigned int index = 0; index < buffers_.size(); ++index)
			requeue(index);
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
			throw system_error("VIDIOC_STREAMON");
		streaming_ = true;
	}

	void stop()
	{
		if (!streaming_)
			return;
		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
			throw system_error("VIDIOC_STREAMOFF");
		streaming_ = false;
	}

	CapturedFrame dequeue(unsigned int &timeouts)
	{
		pollfd descriptor = {fd_, POLLIN | POLLPRI, 0};
		int ret;
		do {
			ret = poll(&descriptor, 1, kPollTimeoutMs);
		} while (ret < 0 && errno == EINTR);
		if (ret < 0)
			throw system_error("poll");
		if (ret == 0) {
			++timeouts;
			throw std::runtime_error("V4L2 poll timeout");
		}

		v4l2_plane planes[VIDEO_MAX_PLANES] = {};
		v4l2_buffer buffer = {};
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.length = VIDEO_MAX_PLANES;
		buffer.m.planes = planes;
		if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0)
			throw system_error("VIDIOC_DQBUF");
		if (buffer.index >= buffers_.size() || buffer.length < 1)
			throw std::runtime_error("invalid V4L2 dequeued buffer");
		if (planes[0].data_offset != 0 || planes[0].bytesused < kInputSize)
			throw std::runtime_error("invalid V4L2 NV12 plane layout");

		/* 返回 index 而不立即 QBUF；调用方决定何时已不再使用该帧。 */
		return {buffer.index,
			static_cast<const unsigned char *>(buffers_[buffer.index].address),
			buffer.sequence};
	}

	void requeue(unsigned int index)
	{
		v4l2_plane planes[VIDEO_MAX_PLANES] = {};
		v4l2_buffer buffer = {};
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buffer.memory = V4L2_MEMORY_MMAP;
		buffer.index = index;
		buffer.length = 1;
		buffer.m.planes = planes;
		planes[0].length = buffers_.at(index).length;
		if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0)
			throw system_error("VIDIOC_QBUF");
	}

	MppBuffer mpp_buffer(unsigned int index) const
	{
		return buffers_.at(index).mpp_buffer;
	}

private:
	void initialize(const char *device, bool export_dmabuf)
	{
		fd_ = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd_ < 0)
			throw system_error("open video device");

		v4l2_capability capability = {};
		if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0)
			throw system_error("VIDIOC_QUERYCAP");
		const std::uint32_t caps =
			(capability.capabilities & V4L2_CAP_DEVICE_CAPS) ?
			capability.device_caps : capability.capabilities;
		if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
		    !(caps & V4L2_CAP_STREAMING))
			throw std::runtime_error("video device lacks capture/streaming support");

		v4l2_format format = {};
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (xioctl(fd_, VIDIOC_G_FMT, &format) < 0)
			throw system_error("VIDIOC_G_FMT");
		const auto &pixel = format.fmt.pix_mp;
		if (pixel.width != kWidth || pixel.height != kHeight ||
		    pixel.pixelformat != V4L2_PIX_FMT_NV12 || pixel.num_planes != 1 ||
		    pixel.plane_fmt[0].bytesperline != kWidth ||
		    pixel.plane_fmt[0].sizeimage < kInputSize) {
			std::ostringstream message;
			message << "unexpected V4L2 format " << pixel.width << 'x'
				<< pixel.height << " stride="
				<< pixel.plane_fmt[0].bytesperline;
			throw std::runtime_error(message.str());
		}

		v4l2_requestbuffers request = {};
		request.count = kBuffers;
		request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		request.memory = V4L2_MEMORY_MMAP;
		if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0)
			throw system_error("VIDIOC_REQBUFS");
		if (request.count < 2)
			throw std::runtime_error("driver returned fewer than 2 buffers");

		buffers_.reserve(request.count);
		for (unsigned int index = 0; index < request.count; ++index) {
			v4l2_plane planes[VIDEO_MAX_PLANES] = {};
			v4l2_buffer buffer = {};
			buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			buffer.memory = V4L2_MEMORY_MMAP;
			buffer.index = index;
			buffer.length = VIDEO_MAX_PLANES;
			buffer.m.planes = planes;
			if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0)
				throw system_error("VIDIOC_QUERYBUF");

			void *address = mmap(nullptr, planes[0].length,
					     PROT_READ | PROT_WRITE, MAP_SHARED,
					     fd_, planes[0].m.mem_offset);
			if (address == MAP_FAILED)
				throw system_error("mmap");
			MappedBuffer mapped;
			mapped.address = address;
			mapped.length = planes[0].length;
			buffers_.push_back(mapped);
			MappedBuffer &stored = buffers_.back();

			if (export_dmabuf) {
				/*
				 * EXPBUF 不复制像素，只为同一个 V4L2 缓冲区取得可跨
				 * 子系统共享的 fd；MPP_BUFFER_TYPE_EXT_DMA 再导入该 fd。
				 */
				v4l2_exportbuffer export_buffer = {};
				export_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				export_buffer.index = index;
				export_buffer.plane = 0;
				export_buffer.flags = O_CLOEXEC;
				if (xioctl(fd_, VIDIOC_EXPBUF, &export_buffer) < 0)
					throw system_error("VIDIOC_EXPBUF");
				stored.export_fd = export_buffer.fd;

				MppBufferInfo info = {};
				info.type = MPP_BUFFER_TYPE_EXT_DMA;
				info.fd = export_buffer.fd;
				info.size = planes[0].length;
				check_mpp(mpp_buffer_import(&stored.mpp_buffer, &info),
					  "mpp_buffer_import(V4L2 dma-buf)");
			}
		}
	}

	void cleanup() noexcept
	{
		if (fd_ >= 0 && streaming_) {
			v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			xioctl(fd_, VIDIOC_STREAMOFF, &type);
		}
		for (const MappedBuffer &buffer : buffers_) {
			if (buffer.mpp_buffer)
				mpp_buffer_put(buffer.mpp_buffer);
			if (buffer.export_fd >= 0)
				close(buffer.export_fd);
			if (buffer.address != MAP_FAILED)
				munmap(buffer.address, buffer.length);
		}
		if (fd_ >= 0)
			close(fd_);
	}

	int fd_ = -1;
	bool streaming_ = false;
	std::vector<MappedBuffer> buffers_;
};

} // namespace

int main(int argc, char **argv)
{
	bool use_dmabuf = false;
	const char *device = nullptr;
	const char *output_path = nullptr;
	if (argc == 3) {
		device = argv[1];
		output_path = argv[2];
	} else if (argc == 4 && !std::strcmp(argv[1], "--dmabuf")) {
		use_dmabuf = true;
		device = argv[2];
		output_path = argv[3];
	} else {
		std::cerr << "ERROR: usage: " << argv[0]
			  << " [--dmabuf] <video-device> <output.h264>\n";
		return 2;
	}
	std::remove(output_path);

	try {
		EncoderConfig config;
		/*
		 * 关键布局差异：copy 目标由我们按 1088 行构造；V4L2 导出的
		 * 单平面 NV12 的 UV 实际紧跟在第 1080 行后。DMA-BUF 若错误声明
		 * 为 1088，会让 MPP 从错误位置读取 UV，虽然编码调用仍可能成功。
		 */
		config.ver_stride = use_dmabuf ? kHeight : kVerStride;
		std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot create H.264 output");
		VideoCapture capture(device, use_dmabuf);
		MppEncoder encoder(config);
		OstreamPacketSink output_sink(output);
		EncoderStats stats;
		encoder.write_header(output_sink, stats);
		capture.start();

		unsigned int timeouts = 0;
		for (unsigned int i = 0; i < kSkipFrames; ++i) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			capture.requeue(frame.index);
		}

		double copy_total_us = 0.0;
		double mpp_total_us = 0.0;
		std::uint64_t dropped = 0;
		std::uint32_t previous_sequence = 0;
		bool have_previous = false;
		const auto loop_start = Clock::now();

		for (unsigned int index = 0; index < kFrames; ++index) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			if (have_previous) {
				const std::uint32_t delta = frame.sequence - previous_sequence;
				if (delta == 0)
					++dropped;
				else if (delta > 1)
					dropped += delta - 1;
			}
			previous_sequence = frame.sequence;
			have_previous = true;

			if (!use_dmabuf) {
				/* copy 完成后 V4L2 原 buffer 已无引用，可立即归还采集队列。 */
				const auto copy_start = Clock::now();
				encoder.load_nv12(frame.data, kInputSize);
				const auto copy_end = Clock::now();
				copy_total_us += elapsed_us(copy_start, copy_end);
				capture.requeue(frame.index);
			}

			const auto mpp_start = Clock::now();
			bool eos;
			if (use_dmabuf) {
				eos = encoder.encode_external_frame(
					capture.mpp_buffer(frame.index), index,
					index == kFrames - 1, output_sink, stats);
			} else {
				eos = encoder.encode_frame(
					index, index == kFrames - 1, output_sink, stats);
			}
			const auto mpp_end = Clock::now();
			mpp_total_us += elapsed_us(mpp_start, mpp_end);
			if (use_dmabuf)
				/* MPP 阻塞取回 packet 后才归还，避免 ISP 覆盖正在编码的帧。 */
				capture.requeue(frame.index);
			if (index == kFrames - 1 && !eos)
				throw std::runtime_error("last packet did not carry EOS");
		}

		const auto loop_end = Clock::now();
		capture.stop();
		output.close();
		if (!output)
			throw std::runtime_error("failed to finalize H.264 output");

		const double loop_seconds =
			std::chrono::duration<double>(loop_end - loop_start).count();
		std::cout << "codec=h264 mode="
			  << (use_dmabuf ? "dmabuf" : "copy")
			  << " bitrate=8000000 gop=60\n";
		std::cout << "pre_skipped=3 frames_in=300 frames_out=300"
			  << " timeouts=" << timeouts << " dropped=" << dropped << '\n';
		std::cout << "packets=" << stats.packets
			  << " idr_frames=" << stats.idr_frames
			  << " encoded_bytes=" << stats.encoded_bytes << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "copy_average_us=" << copy_total_us / kFrames
			  << " mpp_average_us=" << mpp_total_us / kFrames
			  << " loop_fps=" << kFrames / loop_seconds << '\n';
		std::cout << "MPP_V4L2_ENCODE_OK\n";
	} catch (const std::exception &error) {
		std::remove(output_path);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
