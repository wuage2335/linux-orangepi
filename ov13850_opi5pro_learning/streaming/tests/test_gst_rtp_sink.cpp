#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <gst/gst.h>

#include "../src/gst_rtp_sink.hpp"

namespace {

using camera_mpp::EncodedPacketView;
using camera_streaming::GstRtpSink;
using camera_streaming::RtpSinkConfig;
using camera_streaming::make_gst_buffer;

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void require_bytes(GstBuffer *buffer,
		   const std::uint8_t *expected,
		   std::size_t expected_size)
{
	GstMapInfo map = GST_MAP_INFO_INIT;
	require(gst_buffer_map(buffer, &map, GST_MAP_READ), "buffer map failed");
	require(map.size == expected_size, "GstBuffer size changed");
	require(std::memcmp(map.data, expected, expected_size) == 0,
		"GstBuffer bytes changed");
	gst_buffer_unmap(buffer, &map);
}

void test_codec_config_buffer()
{
	const std::uint8_t bytes[] = {0x00, 0x00, 0x00, 0x01, 0x67};
	const EncodedPacketView packet = {
		bytes, sizeof(bytes), -1, false, true, false,
	};
	GstBuffer *buffer = make_gst_buffer(packet);
	require(buffer != nullptr, "codec config conversion returned null");
	require_bytes(buffer, bytes, sizeof(bytes));
	require(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER),
		"codec config lacks HEADER flag");
	require(GST_BUFFER_PTS(buffer) == GST_CLOCK_TIME_NONE,
		"codec config unexpectedly has PTS");
	require(GST_BUFFER_DURATION(buffer) == GST_CLOCK_TIME_NONE,
		"codec config unexpectedly has duration");
	gst_buffer_unref(buffer);
}

void test_timed_keyframe_buffer()
{
	const std::uint8_t bytes[] = {0x00, 0x00, 0x01, 0x65, 0xaa};
	const EncodedPacketView packet = {
		bytes, sizeof(bytes), 33333, true, false, false,
	};
	GstBuffer *buffer = make_gst_buffer(packet);
	require_bytes(buffer, bytes, sizeof(bytes));
	require(GST_BUFFER_PTS(buffer) == 33333 * GST_USECOND,
		"keyframe PTS mismatch");
	require(GST_BUFFER_DTS(buffer) == GST_BUFFER_PTS(buffer),
		"keyframe DTS mismatch");
	require(GST_BUFFER_DURATION(buffer) ==
		gst_util_uint64_scale_int(1, GST_SECOND, 30),
		"keyframe duration mismatch");
	require(!GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT),
		"keyframe marked as delta unit");
	gst_buffer_unref(buffer);
}

void test_timed_delta_buffer()
{
	const std::uint8_t bytes[] = {0x00, 0x00, 0x01, 0x41, 0xbb};
	const EncodedPacketView packet = {
		bytes, sizeof(bytes), 66666, false, false, true,
	};
	GstBuffer *buffer = make_gst_buffer(packet);
	require(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT),
		"non-keyframe lacks DELTA_UNIT flag");
	require(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_MARKER),
		"EOS packet lacks MARKER flag");
	gst_buffer_unref(buffer);
}

void test_pipeline_lifecycle()
{
	const RtpSinkConfig config = {
		"127.0.0.1", 55004, 96, 1200, 2,
	};
	GstRtpSink sink(config);
	const std::uint8_t header[] = {
		0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x28,
	};
	const EncodedPacketView packet = {
		header, sizeof(header), -1, false, true, false,
	};

	sink.consume(packet);
	sink.throw_on_bus_error();
	sink.end_of_stream();
	require(sink.queue_overruns() == 0, "fresh pipeline reported overrun");
}

} // namespace

int main(int argc, char **argv)
{
	gst_init(&argc, &argv);
	try {
		test_codec_config_buffer();
		test_timed_keyframe_buffer();
		test_timed_delta_buffer();
		test_pipeline_lifecycle();
		std::cout << "PASS: GstRtpSink buffer conversion\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
