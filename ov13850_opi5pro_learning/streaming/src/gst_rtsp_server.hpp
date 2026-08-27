#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "encoded_packet_sink.hpp"
#include "live_pts_clock.hpp"

namespace camera_streaming {

struct RtspServerConfig {
	std::string service = "8554";
	std::string mount = "/live";
	int payload_type = 96;
	int mtu = 1200;
	int queue_buffers = 2;
};

/*
 * Shared RTSP output for one continuously running camera/encoder pipeline.
 *
 * GStreamer owns RTSP sessions on the GLib main-loop thread while MPP calls
 * consume() from the capture thread. The class therefore retains an explicit
 * reference to the currently prepared appsrc and protects that reference with
 * a mutex. A packet producer never owns or restarts an RTSP client session.
 */
class GstRtspServerSink final : public camera_mpp::EncodedPacketSink {
public:
	explicit GstRtspServerSink(const RtspServerConfig &config);
	~GstRtspServerSink() override;

	GstRtspServerSink(const GstRtspServerSink &) = delete;
	GstRtspServerSink &operator=(const GstRtspServerSink &) = delete;

	void consume(const camera_mpp::EncodedPacketView &packet) override;
	void run();
	void request_stop();
	bool take_client_idr_request();
	void report_worker_error(const std::string &message);
	void throw_on_error() const;

	std::uint64_t pushed_packets() const;
	std::uint64_t dropped_packets() const;
	std::uint64_t client_connections() const;
	std::uint64_t client_disconnects() const;
	const std::string &service() const;
	const std::string &mount() const;

private:
	static void on_media_configure(GstRTSPMediaFactory *factory,
				       GstRTSPMedia *media,
				       gpointer user_data);
	static void on_media_unprepared(GstRTSPMedia *media, gpointer user_data);
	static void on_client_connected(GstRTSPServer *server,
					GstRTSPClient *client,
					gpointer user_data);
	static void on_client_closed(GstRTSPClient *client, gpointer user_data);
	static void on_bus_error(GstBus *bus,
			 GstMessage *message,
			 gpointer user_data);

	void configure_media(GstRTSPMedia *media);
	void clear_media(GstRTSPMedia *media) noexcept;
	void add_client(GstRTSPClient *client);
	void remove_client(GstRTSPClient *client) noexcept;
	void record_error(const std::string &message) noexcept;
	void initialize(const RtspServerConfig &config);
	void cleanup() noexcept;

	RtspServerConfig config_;
	GMainLoop *main_loop_ = nullptr;
	GstRTSPServer *server_ = nullptr;
	GstRTSPMediaFactory *factory_ = nullptr;
	guint server_source_id_ = 0;

	mutable std::mutex state_mutex_;
	GstRTSPMedia *media_ = nullptr;
	GstElement *appsrc_ = nullptr;
	GstBus *bus_ = nullptr;
	std::vector<std::uint8_t> codec_header_;
	LivePtsClock live_pts_clock_;
	std::string fatal_error_;
	unsigned int active_clients_ = 0;
	bool header_pending_ = false;
	bool idr_pending_ = false;
	bool shutting_down_ = false;

	mutable std::mutex clients_mutex_;
	std::vector<GstRTSPClient *> clients_;

	std::atomic<std::uint64_t> pushed_packets_{0};
	std::atomic<std::uint64_t> dropped_packets_{0};
	std::atomic<std::uint64_t> client_connections_{0};
	std::atomic<std::uint64_t> client_disconnects_{0};
};

} // namespace camera_streaming
