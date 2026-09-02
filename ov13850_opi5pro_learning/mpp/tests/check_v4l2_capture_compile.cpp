#include <cstdint>

#include "../src/v4l2_capture.hpp"

using camera_mpp::CapturedFrame;
using camera_mpp::V4L2Capture;
using camera_mpp::V4L2MemoryMode;

void exercise_capture_contract()
{
	V4L2Capture capture("/dev/video11", V4L2MemoryMode::DmaBufExport);
	unsigned int timeouts = 0;

	capture.start();
	const CapturedFrame frame = capture.dequeue(timeouts);
	const std::uint64_t timestamp_ns = frame.timestamp_ns;
	const std::uint32_t timestamp_flags = frame.timestamp_flags;
	MppBuffer buffer = capture.mpp_buffer(frame.index);
	(void)timestamp_ns;
	(void)timestamp_flags;
	(void)buffer;
	capture.requeue(frame.index);
	capture.stop();
}
