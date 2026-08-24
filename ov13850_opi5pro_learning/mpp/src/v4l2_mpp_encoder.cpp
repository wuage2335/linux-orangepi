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
	void *address = MAP_FAILED;
	std::size_t length = 0;
};

struct CapturedFrame {
	unsigned int index;
	const unsigned char *data;
	std::uint32_t sequence;
};

class VideoCapture {
public:
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

private:
	void initialize(const char *device)
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
			buffers_.push_back({address, planes[0].length});
		}
	}

	void cleanup() noexcept
	{
		if (fd_ >= 0 && streaming_) {
			v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			xioctl(fd_, VIDIOC_STREAMOFF, &type);
		}
		for (const MappedBuffer &buffer : buffers_)
			if (buffer.address != MAP_FAILED)
				munmap(buffer.address, buffer.length);
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
	if (argc != 3) {
		std::cerr << "ERROR: usage: " << argv[0]
			  << " <video-device> <output.h264>\n";
		return 2;
	}
	std::remove(argv[2]);

	try {
		EncoderConfig config;
		std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot create H.264 output");
		MppEncoder encoder(config);
		EncoderStats stats;
		encoder.write_header(output, stats);
		VideoCapture capture(argv[1]);
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

			const auto copy_start = Clock::now();
			encoder.load_nv12(frame.data, kInputSize);
			const auto copy_end = Clock::now();
			copy_total_us += elapsed_us(copy_start, copy_end);
			capture.requeue(frame.index);

			const auto mpp_start = Clock::now();
			const bool eos = encoder.encode_frame(
				index, index == kFrames - 1, output, stats);
			const auto mpp_end = Clock::now();
			mpp_total_us += elapsed_us(mpp_start, mpp_end);
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
		std::cout << "codec=h264 bitrate=8000000 gop=60\n";
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
		std::remove(argv[2]);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
