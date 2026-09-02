#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

#include "mpp_buffer.h"
#include "mpp_err.h"

namespace camera_mpp {

constexpr unsigned int kCaptureBufferCount = 4;
constexpr int kCapturePollTimeoutMs = 2000;
constexpr std::uint32_t kCaptureWidth = 1920;
constexpr std::uint32_t kCaptureHeight = 1080;
constexpr std::size_t kCaptureInputSize =
	static_cast<std::size_t>(kCaptureWidth) * kCaptureHeight * 3 / 2;

enum class V4L2MemoryMode {
	MmapOnly,
	DmaBufExport,
};

struct CapturedFrame {
	unsigned int index;
	const unsigned char *data;
	std::uint32_t sequence;
	std::uint64_t timestamp_ns;
	std::uint32_t timestamp_flags;
};

inline int v4l2_xioctl(int fd, unsigned long request, void *argument)
{
	int ret;
	do {
		ret = ioctl(fd, request, argument);
	} while (ret < 0 && errno == EINTR);
	return ret;
}

inline std::runtime_error v4l2_system_error(const char *operation)
{
	const int saved = errno;
	return std::runtime_error(std::string(operation) + ": " +
				  std::strerror(saved));
}

inline void v4l2_check_mpp(MPP_RET ret, const char *operation)
{
	if (ret != MPP_OK)
		throw std::runtime_error(std::string(operation) +
					 " failed, MPP_RET=" + std::to_string(ret));
}

class V4L2Capture {
public:
	V4L2Capture(const char *device, V4L2MemoryMode memory_mode)
	{
		try {
			initialize(device, memory_mode);
		} catch (...) {
			cleanup();
			throw;
		}
	}

	~V4L2Capture()
	{
		cleanup();
	}

	V4L2Capture(const V4L2Capture &) = delete;
	V4L2Capture &operator=(const V4L2Capture &) = delete;

	void start()
	{
		for (unsigned int index = 0; index < buffers_.size(); ++index)
			requeue(index);

		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (v4l2_xioctl(fd_, VIDIOC_STREAMON, &type) < 0)
			throw v4l2_system_error("VIDIOC_STREAMON");
		streaming_ = true;
	}

	void stop()
	{
		if (!streaming_)
			return;

		v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (v4l2_xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0)
			throw v4l2_system_error("VIDIOC_STREAMOFF");
		streaming_ = false;
	}

	CapturedFrame dequeue(unsigned int &timeouts)
	{
		pollfd descriptor = {fd_, POLLIN | POLLPRI, 0};
		int ret;
		do {
			ret = poll(&descriptor, 1, kCapturePollTimeoutMs);
		} while (ret < 0 && errno == EINTR);
		if (ret < 0)
			throw v4l2_system_error("poll");
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
		if (v4l2_xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0)
			throw v4l2_system_error("VIDIOC_DQBUF");
		if (buffer.index >= buffers_.size() || buffer.length < 1)
			throw std::runtime_error("invalid V4L2 dequeued buffer");
		if (planes[0].data_offset != 0 ||
		    planes[0].bytesused < kCaptureInputSize)
			throw std::runtime_error("invalid V4L2 NV12 plane layout");

		return {
			buffer.index,
			static_cast<const unsigned char *>(buffers_[buffer.index].address),
			buffer.sequence,
			static_cast<std::uint64_t>(buffer.timestamp.tv_sec) * 1000000000ULL +
				static_cast<std::uint64_t>(buffer.timestamp.tv_usec) * 1000ULL,
			buffer.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK,
		};
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
		if (v4l2_xioctl(fd_, VIDIOC_QBUF, &buffer) < 0)
			throw v4l2_system_error("VIDIOC_QBUF");
	}

	MppBuffer mpp_buffer(unsigned int index) const
	{
		return buffers_.at(index).mpp_buffer;
	}

private:
	struct MappedBuffer {
		void *address = MAP_FAILED;
		std::size_t length = 0;
		int export_fd = -1;
		MppBuffer mpp_buffer = nullptr;
	};

	void initialize(const char *device, V4L2MemoryMode memory_mode)
	{
		fd_ = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (fd_ < 0)
			throw v4l2_system_error("open video device");

		v4l2_capability capability = {};
		if (v4l2_xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0)
			throw v4l2_system_error("VIDIOC_QUERYCAP");
		const std::uint32_t caps =
			(capability.capabilities & V4L2_CAP_DEVICE_CAPS) ?
			capability.device_caps : capability.capabilities;
		if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
		    !(caps & V4L2_CAP_STREAMING))
			throw std::runtime_error(
				"video device lacks capture/streaming support");

		v4l2_format format = {};
		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if (v4l2_xioctl(fd_, VIDIOC_G_FMT, &format) < 0)
			throw v4l2_system_error("VIDIOC_G_FMT");
		const auto &pixel = format.fmt.pix_mp;
		if (pixel.width != kCaptureWidth ||
		    pixel.height != kCaptureHeight ||
		    pixel.pixelformat != V4L2_PIX_FMT_NV12 ||
		    pixel.num_planes != 1 ||
		    pixel.plane_fmt[0].bytesperline != kCaptureWidth ||
		    pixel.plane_fmt[0].sizeimage < kCaptureInputSize) {
			std::ostringstream message;
			message << "unexpected V4L2 format " << pixel.width << 'x'
				<< pixel.height << " stride="
				<< pixel.plane_fmt[0].bytesperline;
			throw std::runtime_error(message.str());
		}

		v4l2_requestbuffers request = {};
		request.count = kCaptureBufferCount;
		request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		request.memory = V4L2_MEMORY_MMAP;
		if (v4l2_xioctl(fd_, VIDIOC_REQBUFS, &request) < 0)
			throw v4l2_system_error("VIDIOC_REQBUFS");
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
			if (v4l2_xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0)
				throw v4l2_system_error("VIDIOC_QUERYBUF");

			void *address = mmap(nullptr, planes[0].length,
					     PROT_READ | PROT_WRITE, MAP_SHARED,
					     fd_, planes[0].m.mem_offset);
			if (address == MAP_FAILED)
				throw v4l2_system_error("mmap");

			MappedBuffer mapped;
			mapped.address = address;
			mapped.length = planes[0].length;
			buffers_.push_back(mapped);
			MappedBuffer &stored = buffers_.back();

			if (memory_mode == V4L2MemoryMode::DmaBufExport) {
				v4l2_exportbuffer export_buffer = {};
				export_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				export_buffer.index = index;
				export_buffer.plane = 0;
				export_buffer.flags = O_CLOEXEC;
				if (v4l2_xioctl(fd_, VIDIOC_EXPBUF, &export_buffer) < 0)
					throw v4l2_system_error("VIDIOC_EXPBUF");
				stored.export_fd = export_buffer.fd;

				MppBufferInfo info = {};
				info.type = MPP_BUFFER_TYPE_EXT_DMA;
				info.fd = export_buffer.fd;
				info.size = planes[0].length;
				v4l2_check_mpp(
					mpp_buffer_import(&stored.mpp_buffer, &info),
					"mpp_buffer_import(V4L2 dma-buf)");
			}
		}
	}

	void cleanup() noexcept
	{
		if (fd_ >= 0 && streaming_) {
			v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			v4l2_xioctl(fd_, VIDIOC_STREAMOFF, &type);
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
		fd_ = -1;
		streaming_ = false;
	}

	int fd_ = -1;
	bool streaming_ = false;
	std::vector<MappedBuffer> buffers_;
};

} // namespace camera_mpp
