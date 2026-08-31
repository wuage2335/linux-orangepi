#include "gst_rtsp_server.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "gst_rtp_sink.hpp"

/*
 * 本文件实现“共享 RTSP 出口”。摄像头和 MPP 编码器由外部 worker 持续运行，
 * RTSP 客户端可以随时连接或断开，而不会为每个客户端重新打开摄像头：
 *
 *   MPP packet -> shared appsrc -> h264parse -> rtph264pay -> RTSP client
 *
 * 没有客户端时 packet 直接丢弃，防止形成历史画面积压；新客户端连接后先补
 * codec header，再请求一幅 IDR，使播放器能从当前时刻开始解码。
 */
namespace camera_streaming {

namespace {

void validate_config(const RtspServerConfig &config)
{
	if (config.service.empty())
		throw std::invalid_argument("RTSP service is empty");
	if (config.mount.empty() || config.mount.front() != '/')
		throw std::invalid_argument("RTSP mount must start with /");
	if (config.payload_type < 0 || config.payload_type > 127)
		throw std::invalid_argument("RTSP payload type must be in 0..127");
	if (config.mtu < 256 || config.mtu > 65535)
		throw std::invalid_argument("RTSP MTU must be in 256..65535");
	if (config.queue_buffers < 1)
		throw std::invalid_argument("RTSP queue buffer count must be positive");
}

GstFlowReturn push_packet(GstElement *appsrc,
			  const camera_mpp::EncodedPacketView &packet)
{
	GstBuffer *buffer = make_gst_buffer(packet);
	return gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
}

} // namespace

GstRtspServerSink::GstRtspServerSink(const RtspServerConfig &config)
{
	try {
		initialize(config);
	} catch (...) {
		cleanup();
		throw;
	}
}

GstRtspServerSink::~GstRtspServerSink()
{
	cleanup();
}

void GstRtspServerSink::consume(const camera_mpp::EncodedPacketView &packet)
{
	/*
	 * consume 在编码线程调用，RTSP 回调在 GLib 主循环线程调用，所以共享的
	 * appsrc、客户端数、header 和时钟都受 state_mutex_ 保护。锁内只复制引用
	 * 和小段状态，真正 push 在锁外完成，避免网络阻塞卡住连接/断开回调。
	 */
	if (packet.size && !packet.data)
		throw std::invalid_argument("nonempty RTSP packet has no data");
	if (!packet.size)
		return;

	GstElement *appsrc = nullptr;
	std::vector<std::uint8_t> header;
	std::int64_t adjusted_pts = 0;

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (packet.codec_config) {
			codec_header_.assign(packet.data, packet.data + packet.size);
			return;
		}

		/*
		 * 无观众时仍保持 sensor/encoder 热运行，但立即丢弃输出。这样重连快，
		 * 又不会把无人观看期间的旧画面堆成数秒延迟。
		 */
		if (!appsrc_ || active_clients_ == 0) {
			dropped_packets_.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		appsrc = GST_ELEMENT(gst_object_ref(appsrc_));
		if (header_pending_ && !codec_header_.empty()) {
			header = codec_header_;
			header_pending_ = false;
		}
		adjusted_pts = live_pts_clock_.map(
			static_cast<std::int64_t>(gst_util_get_timestamp() / GST_USECOND));
	}
	/*
	 * LivePtsClock 使用真实单调时间重新建立会话 PTS。它修复了实采30.05fps与
	 * 名义30fps固定步长之间的长期漂移，系统时间校准也不会让 PTS 倒退。
	 */

	if (!header.empty()) {
		const camera_mpp::EncodedPacketView header_packet = {
			header.data(), header.size(), -1, false, true, false,
		};
		const GstFlowReturn flow = push_packet(appsrc, header_packet);
		if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING &&
		    flow != GST_FLOW_EOS) {
			gst_object_unref(appsrc);
			throw std::runtime_error("RTSP header push failed: " +
						 std::to_string(flow));
		}
	}

	const camera_mpp::EncodedPacketView adjusted = {
		packet.data,
		packet.size,
		adjusted_pts,
		packet.keyframe,
		false,
		packet.eos,
	};
	const GstFlowReturn flow = push_packet(appsrc, adjusted);
	gst_object_unref(appsrc);

	if (flow == GST_FLOW_OK) {
		pushed_packets_.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (flow == GST_FLOW_FLUSHING || flow == GST_FLOW_EOS) {
		dropped_packets_.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	throw std::runtime_error("RTSP packet push failed: " +
				 std::to_string(flow));
}

void GstRtspServerSink::run()
{
	/* GLib main loop 负责 RTSP socket、客户端和 media 回调，不执行摄像头采集。 */
	throw_on_error();
	std::cout << "RTSP_SERVER_READY service=" << config_.service
		  << " mount=" << config_.mount << std::endl;
	g_main_loop_run(main_loop_);
	throw_on_error();
}

void GstRtspServerSink::request_stop()
{
	if (main_loop_)
		g_main_loop_quit(main_loop_);
}

bool GstRtspServerSink::take_client_idr_request()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	/* Wait until appsrc exists so the requested IDR cannot be discarded. */
	if (!idr_pending_ || !appsrc_ || active_clients_ == 0)
		return false;
	idr_pending_ = false;
	return true;
}

void GstRtspServerSink::report_worker_error(const std::string &message)
{
	record_error(message);
}

void GstRtspServerSink::throw_on_error() const
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (!fatal_error_.empty())
		throw std::runtime_error(fatal_error_);
}

std::uint64_t GstRtspServerSink::pushed_packets() const
{
	return pushed_packets_.load(std::memory_order_relaxed);
}

std::uint64_t GstRtspServerSink::dropped_packets() const
{
	return dropped_packets_.load(std::memory_order_relaxed);
}

std::uint64_t GstRtspServerSink::client_connections() const
{
	return client_connections_.load(std::memory_order_relaxed);
}

std::uint64_t GstRtspServerSink::client_disconnects() const
{
	return client_disconnects_.load(std::memory_order_relaxed);
}

const std::string &GstRtspServerSink::service() const
{
	return config_.service;
}

const std::string &GstRtspServerSink::mount() const
{
	return config_.mount;
}

void GstRtspServerSink::on_media_configure(GstRTSPMediaFactory *,
					    GstRTSPMedia *media,
					    gpointer user_data)
{
	static_cast<GstRtspServerSink *>(user_data)->configure_media(media);
}

void GstRtspServerSink::on_media_unprepared(GstRTSPMedia *media,
					     gpointer user_data)
{
	static_cast<GstRtspServerSink *>(user_data)->clear_media(media);
}

void GstRtspServerSink::on_client_connected(GstRTSPServer *,
					     GstRTSPClient *client,
					     gpointer user_data)
{
	static_cast<GstRtspServerSink *>(user_data)->add_client(client);
}

void GstRtspServerSink::on_client_closed(GstRTSPClient *client,
					  gpointer user_data)
{
	static_cast<GstRtspServerSink *>(user_data)->remove_client(client);
}

void GstRtspServerSink::on_bus_error(GstBus *,
				      GstMessage *message,
				      gpointer user_data)
{
	GError *error = nullptr;
	gchar *debug = nullptr;
	gst_message_parse_error(message, &error, &debug);
	std::ostringstream text;
	text << "RTSP GStreamer error";
	if (error && error->message)
		text << ": " << error->message;
	if (debug)
		text << " (" << debug << ')';
	if (error)
		g_error_free(error);
	g_free(debug);
	static_cast<GstRtspServerSink *>(user_data)->record_error(text.str());
}

void GstRtspServerSink::configure_media(GstRTSPMedia *media)
{
	/*
	 * 客户端 DESCRIBE/PLAY 后，GStreamer 为 shared factory 准备 media pipeline。
	 * 这里找到名为 source 的 appsrc、设置 H.264 caps，并保存带引用的对象给编码
	 * 线程使用。media 未准备好之前即使有客户端也不能安全 push。
	 */
	GstElement *pipeline = gst_rtsp_media_get_element(media);
	if (!pipeline) {
		record_error("gst_rtsp_media_get_element returned null");
		return;
	}
	GstElement *source =
		gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "source");
	GstBus *bus = gst_element_get_bus(pipeline);
	gst_object_unref(pipeline);
	if (!source || !bus) {
		if (source)
			gst_object_unref(source);
		if (bus)
			gst_object_unref(bus);
		record_error("RTSP media is missing appsrc or bus");
		return;
	}

	GstCaps *caps = gst_caps_new_simple(
		"video/x-h264",
		"stream-format", G_TYPE_STRING, "byte-stream",
		"alignment", G_TYPE_STRING, "au",
		"width", G_TYPE_INT, 1920,
		"height", G_TYPE_INT, 1080,
		"framerate", GST_TYPE_FRACTION, 30, 1,
		nullptr);
	if (!caps) {
		gst_object_unref(source);
		gst_object_unref(bus);
		record_error("failed to create RTSP appsrc caps");
		return;
	}
	g_object_set(source,
		     "caps", caps,
		     "is-live", TRUE,
		     "format", GST_FORMAT_TIME,
		     "block", FALSE,
		     "do-timestamp", FALSE,
		     "stream-type", GST_APP_STREAM_TYPE_STREAM,
		     nullptr);
	gst_caps_unref(caps);

	gst_bus_add_signal_watch(bus);
	g_signal_connect(bus, "message::error", G_CALLBACK(on_bus_error), this);
	g_signal_connect(media, "unprepared", G_CALLBACK(on_media_unprepared), this);
	g_object_ref(media);

	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (shutting_down_) {
			g_signal_handlers_disconnect_by_data(bus, this);
			gst_bus_remove_signal_watch(bus);
			gst_object_unref(bus);
			gst_object_unref(source);
			g_signal_handlers_disconnect_by_data(media, this);
			g_object_unref(media);
			return;
		}
		media_ = media;
		appsrc_ = source;
		bus_ = bus;
		live_pts_clock_.reset();
		header_pending_ = true;
	}
}

void GstRtspServerSink::clear_media(GstRTSPMedia *media) noexcept
{
	/* media 停止时先从共享状态摘除指针，再断开 signal 并释放引用。 */
	GstElement *source = nullptr;
	GstBus *bus = nullptr;
	GstRTSPMedia *owned_media = nullptr;
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (media_ != media)
			return;
		source = appsrc_;
		bus = bus_;
		owned_media = media_;
		appsrc_ = nullptr;
		bus_ = nullptr;
		media_ = nullptr;
		live_pts_clock_.reset();
		header_pending_ = true;
	}

	if (bus) {
		g_signal_handlers_disconnect_by_data(bus, this);
		gst_bus_remove_signal_watch(bus);
		gst_object_unref(bus);
	}
	if (source)
		gst_object_unref(source);
	if (owned_media) {
		g_signal_handlers_disconnect_by_data(owned_media, this);
		g_object_unref(owned_media);
	}
}

void GstRtspServerSink::add_client(GstRTSPClient *client)
{
	/*
	 * 每个新客户端都需要重新发送 header 并请求 IDR；否则它可能从 GOP 中间
	 * 加入，只收到依赖旧参考帧的 P 帧，表现为黑屏或花屏直到下一关键帧。
	 */
	g_object_ref(client);
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		clients_.push_back(client);
	}
	g_signal_connect(client, "closed", G_CALLBACK(on_client_closed), this);

	unsigned int active;
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		++active_clients_;
		header_pending_ = true;
		idr_pending_ = true;
		active = active_clients_;
	}
	client_connections_.fetch_add(1, std::memory_order_relaxed);
	std::cout << "RTSP_CLIENT_CONNECTED active=" << active << std::endl;
}

void GstRtspServerSink::remove_client(GstRTSPClient *client) noexcept
{
	/* 连接关闭只减少观看者计数，不停止唯一的采集/编码 worker。 */
	bool found = false;
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		const auto item = std::find(clients_.begin(), clients_.end(), client);
		if (item != clients_.end()) {
			clients_.erase(item);
			found = true;
		}
	}
	if (!found)
		return;

	unsigned int active;
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (active_clients_)
			--active_clients_;
		active = active_clients_;
	}
	client_disconnects_.fetch_add(1, std::memory_order_relaxed);
	std::cout << "RTSP_CLIENT_DISCONNECTED active=" << active << std::endl;
	g_object_unref(client);
}

void GstRtspServerSink::record_error(const std::string &message) noexcept
{
	/* 只保留第一个根因，并唤醒主线程退出；后续清理由 cleanup 统一完成。 */
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (fatal_error_.empty())
			fatal_error_ = message;
	}
	request_stop();
}

void GstRtspServerSink::initialize(const RtspServerConfig &config)
{
	/*
	 * factory 的 launch 字符串描述每个 RTSP media 的内部管线。shared=true 是
	 * 本项目的关键：所有客户端共享同一 media，而不是各自触发一套摄像头编码。
	 */
	validate_config(config);
	config_ = config;
	gst_init(nullptr, nullptr);
	main_loop_ = g_main_loop_new(nullptr, FALSE);
	server_ = gst_rtsp_server_new();
	factory_ = gst_rtsp_media_factory_new();
	if (!main_loop_ || !server_ || !factory_)
		throw std::runtime_error("failed to allocate RTSP server objects");

	gst_rtsp_server_set_service(server_, config_.service.c_str());
	std::ostringstream launch;
	launch << "( appsrc name=source is-live=true format=time block=false "
	       << "do-timestamp=false ! queue max-size-buffers="
	       << config_.queue_buffers
	       << " max-size-bytes=0 max-size-time=0 leaky=downstream "
	       << "! h264parse config-interval=-1 "
	       << "! rtph264pay name=pay0 pt=" << config_.payload_type
	       << " mtu=" << config_.mtu << " config-interval=1 )";
	gst_rtsp_media_factory_set_launch(factory_, launch.str().c_str());
	gst_rtsp_media_factory_set_shared(factory_, TRUE);
	g_signal_connect(factory_, "media-configure",
			 G_CALLBACK(on_media_configure), this);
	g_signal_connect(server_, "client-connected",
			 G_CALLBACK(on_client_connected), this);

	GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server_);
	if (!mounts)
		throw std::runtime_error("failed to get RTSP mount points");
	/* add_factory transfers ownership to the mount table. */
	gst_rtsp_mount_points_add_factory(mounts, config_.mount.c_str(), factory_);
	g_object_unref(mounts);

	server_source_id_ = gst_rtsp_server_attach(server_, nullptr);
	if (!server_source_id_)
		throw std::runtime_error("failed to attach RTSP server");
}

void GstRtspServerSink::cleanup() noexcept
{
	/*
	 * 清理顺序先阻止新回调，再关闭客户端，随后释放 media/appsrc/bus，最后释放
	 * server 和 main loop。函数必须 noexcept，才能安全服务于正常析构和异常回滚。
	 */
	request_stop();
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		shutting_down_ = true;
	}
	if (server_source_id_)
		g_source_remove(server_source_id_);
	server_source_id_ = 0;

	if (server_)
		g_signal_handlers_disconnect_by_data(server_, this);
	if (factory_)
		g_signal_handlers_disconnect_by_data(factory_, this);

	std::vector<GstRTSPClient *> clients;
	{
		std::lock_guard<std::mutex> lock(clients_mutex_);
		clients.swap(clients_);
	}
	for (GstRTSPClient *client : clients) {
		g_signal_handlers_disconnect_by_data(client, this);
		gst_rtsp_client_close(client);
		g_object_unref(client);
	}

	GstElement *source = nullptr;
	GstBus *bus = nullptr;
	GstRTSPMedia *media = nullptr;
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		source = appsrc_;
		bus = bus_;
		media = media_;
		appsrc_ = nullptr;
		bus_ = nullptr;
		media_ = nullptr;
	}
	if (bus) {
		g_signal_handlers_disconnect_by_data(bus, this);
		gst_bus_remove_signal_watch(bus);
		gst_object_unref(bus);
	}
	if (source)
		gst_object_unref(source);
	if (media) {
		g_signal_handlers_disconnect_by_data(media, this);
		g_object_unref(media);
	}

	if (server_)
		g_object_unref(server_);
	if (main_loop_)
		g_main_loop_unref(main_loop_);
	server_ = nullptr;
	factory_ = nullptr;
	main_loop_ = nullptr;
}

} // namespace camera_streaming
