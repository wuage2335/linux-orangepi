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

namespace {

using SteadyClock = std::chrono::steady_clock;

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
	const int saved_errno = errno;

	return std::runtime_error(std::string(operation) + ": " +
				  std::strerror(saved_errno));
}

int xioctl(int fd, unsigned long request, void *argument)
{
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
		if (!streaming_)
			return;

		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
			throw system_error("VIDIOC_STREAMOFF");

		streaming_ = false;
	}

	CapturedFrame dequeue(unsigned int &timeouts)
	{
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
		fd_ = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd_ < 0)
			throw system_error("open video device");

		v4l2_capability capability = {};
		if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0)
			throw system_error("VIDIOC_QUERYCAP");

		const std::uint32_t caps =
			(capability.capabilities & V4L2_CAP_DEVICE_CAPS) ?
			capability.device_caps : capability.capabilities;

		if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE))
			throw std::runtime_error(
				"device does not support multiplanar capture");
		if (!(caps & V4L2_CAP_STREAMING))
			throw std::runtime_error(
				"device does not support streaming I/O");

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

		v4l2_requestbuffers request = {};
		request.count = kRequestedBuffers;
		request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		request.memory = V4L2_MEMORY_MMAP;
		if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0)
			throw system_error("VIDIOC_REQBUFS");
		if (request.count < 2)
			throw std::runtime_error("driver returned fewer than 2 buffers");

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

	std::remove(argv[2]);
	unsigned int timeouts = 0;

	try {
		VideoCapture capture(argv[1]);
		RgaResizer resizer;
		capture.start();

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

			const auto copy_start = SteadyClock::now();
			std::memcpy(resizer.source_data(), frame.data, kSrcSize);
			const auto copy_end = SteadyClock::now();
			copy_total_us += elapsed_us(copy_start, copy_end);

			capture.requeue(frame.index);

			const auto rga_start = SteadyClock::now();
			resizer.resize();
			const auto rga_end = SteadyClock::now();
			rga_total_us += elapsed_us(rga_start, rga_end);
		}

		const auto loop_end = SteadyClock::now();
		capture.stop();
		write_output(argv[2], resizer.output_data());

		const double loop_total_s =
			std::chrono::duration<double>(loop_end - loop_start).count();
		const double copy_average_us = copy_total_us / kProcessFrames;
		const double rga_average_us = rga_total_us / kProcessFrames;
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
		std::remove(argv[2]);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}

	return 0;
}
