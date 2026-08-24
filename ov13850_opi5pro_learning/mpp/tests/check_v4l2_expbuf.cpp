#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

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
	return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "ERROR: usage: " << argv[0] << " <video-device>\n";
		return 2;
	}

	int video_fd = -1;
	try {
		video_fd = open(argv[1], O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (video_fd < 0)
			throw system_error("open");

		v4l2_requestbuffers request = {};
		request.count = 4;
		request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		request.memory = V4L2_MEMORY_MMAP;
		if (xioctl(video_fd, VIDIOC_REQBUFS, &request) < 0)
			throw system_error("VIDIOC_REQBUFS");
		if (request.count < 2)
			throw std::runtime_error("driver returned fewer than 2 buffers");

		for (unsigned int index = 0; index < request.count; ++index) {
			v4l2_exportbuffer export_buffer = {};
			export_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			export_buffer.index = index;
			export_buffer.plane = 0;
			export_buffer.flags = O_CLOEXEC;
			if (xioctl(video_fd, VIDIOC_EXPBUF, &export_buffer) < 0)
				throw system_error("VIDIOC_EXPBUF");

			struct stat status = {};
			if (fstat(export_buffer.fd, &status) < 0) {
				close(export_buffer.fd);
				throw system_error("fstat(dma-buf)");
			}

			std::cout << "index=" << index
				  << " fd=" << export_buffer.fd
				  << " size=" << status.st_size << '\n';
			close(export_buffer.fd);
		}

		request.count = 0;
		if (xioctl(video_fd, VIDIOC_REQBUFS, &request) < 0)
			throw system_error("VIDIOC_REQBUFS(count=0)");

		close(video_fd);
		std::cout << "V4L2_EXPBUF_OK buffers=4\n";
	} catch (const std::exception &error) {
		if (video_fd >= 0)
			close(video_fd);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
