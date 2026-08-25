#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "encoded_packet_sink.hpp"

namespace camera_streaming {

struct RtpSinkConfig {
	std::string host;
	int port = 5004;
	int payload_type = 96;
	int mtu = 1200;
	int queue_buffers = 2;
};

GstBuffer *make_gst_buffer(const camera_mpp::EncodedPacketView &packet);

class GstRtpSink final : public camera_mpp::EncodedPacketSink {
public:
	explicit GstRtpSink(const RtpSinkConfig &config);
	~GstRtpSink() override;

	GstRtpSink(const GstRtpSink &) = delete;
	GstRtpSink &operator=(const GstRtpSink &) = delete;

	void consume(const camera_mpp::EncodedPacketView &packet) override;
	void end_of_stream();
	void throw_on_bus_error();
	std::uint64_t queue_overruns() const;

private:
	static void on_queue_overrun(GstElement *queue, gpointer user_data);
	void initialize(const RtpSinkConfig &config);
	void cleanup() noexcept;

	GstElement *pipeline_ = nullptr;
	GstElement *appsrc_ = nullptr;
	GstElement *queue_ = nullptr;
	GstBus *bus_ = nullptr;
	std::atomic<std::uint64_t> queue_overruns_{0};
};

} // namespace camera_streaming
