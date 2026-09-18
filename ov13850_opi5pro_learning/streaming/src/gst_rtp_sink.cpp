#include "gst_rtp_sink.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>

/**
 * @file gst_rtp_sink.cpp
 * @brief 实现从 MPP 编码包到 GStreamer RTP/UDP 输出的适配层。
 *
 * 本文件把 MPP 产生的 H.264 编码 packet 送进 GStreamer，再由 GStreamer 添加
 * RTP 头并通过 UDP 发到 PC：
 *
 *   MPP 编码包 -> appsrc -> queue -> h264parse -> rtph264pay -> udpsink
 *
 * MPP 只负责压缩图像，不知道网络地址；GStreamer 只接收已经压缩的数据，不
 * 操作摄像头。GstRtpSink 是两者之间的适配器，也是网络阻塞与编码线程之间的
 * 缓冲边界。
 */
namespace camera_streaming {

namespace {

/**
 * @brief 确认一个 GStreamer 元素已经成功创建。
 * @param element 待检查的元素指针。
 * @param name 用于错误信息的元素名称。
 * @throws std::runtime_error 元素为空时抛出。
 */
void require_element(GstElement *element, const char *name)
{
	if (!element)
		throw std::runtime_error(std::string("missing GStreamer element: ") +
					 name);
}

struct GstElementUnref {
	void operator()(GstElement *element) const
	{
		if (element)
			gst_object_unref(element);
	}
};

using OwnedGstElement = std::unique_ptr<GstElement, GstElementUnref>;

OwnedGstElement make_owned_element(const char *factory, const char *name)
{
	OwnedGstElement element(gst_element_factory_make(factory, name));
	require_element(element.get(), name);
	return element;
}

GstElement *add_owned_element(GstElement *pipeline,
			      OwnedGstElement element,
			      const char *name)
{
	GstElement *raw = element.get();
	if (!gst_bin_add(GST_BIN(pipeline), raw))
		throw std::runtime_error(std::string("failed to add element: ") + name);
	element.release();
	return raw;
}

} // 匿名命名空间

void configure_bounded_appsrc(GstElement *appsrc, int max_buffers)
{
	if (!appsrc || !GST_IS_APP_SRC(appsrc))
		throw std::invalid_argument("bounded source must be an appsrc");
	if (max_buffers < 1)
		throw std::invalid_argument("appsrc max-buffers must be positive");

	g_object_set(appsrc,
		     "block", FALSE,
		     "max-buffers", static_cast<guint64>(max_buffers),
		     "max-bytes", static_cast<guint64>(0),
		     "max-time", static_cast<guint64>(0),
		     "leaky-type", GST_APP_LEAKY_TYPE_DOWNSTREAM,
		     nullptr);
}

GstBuffer *make_gst_buffer(const camera_mpp::EncodedPacketView &packet)
{
	/*
	 * 带 owner 的异步 packet 直接包装为只读 GstMemory。释放回调持有 owner，
	 * 所以下游最后一个 GstBuffer 引用销毁前，生产者不会复用底层存储。没有
	 * owner 的同步 packet 保留复制路径，避免包装调用栈上的短生命周期内存。
	 */
	if (packet.size && !packet.data)
		throw std::invalid_argument("nonempty encoded packet has no data");

	GstBuffer *buffer = nullptr;
	if (packet.owner) {
		auto *owner = new std::shared_ptr<const void>(packet.owner);
		buffer = gst_buffer_new_wrapped_full(
			GST_MEMORY_FLAG_READONLY,
			const_cast<std::uint8_t *>(packet.data),
			packet.size,
			0,
			packet.size,
			owner,
			[](gpointer data) {
				delete static_cast<std::shared_ptr<const void> *>(data);
			});
		if (!buffer) {
			delete owner;
			throw std::runtime_error("gst_buffer_new_wrapped_full failed");
		}
	} else {
		buffer = gst_buffer_new_allocate(nullptr, packet.size, nullptr);
		if (!buffer)
			throw std::runtime_error("gst_buffer_new_allocate failed");
		if (packet.size &&
		    gst_buffer_fill(buffer, 0, packet.data, packet.size) != packet.size) {
			gst_buffer_unref(buffer);
			throw std::runtime_error("gst_buffer_fill wrote incomplete packet");
		}
	}

	if (packet.codec_config) {
		GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_HEADER);
		GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;
		GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
		GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
	} else {
		if (packet.pts_us < 0) {
			gst_buffer_unref(buffer);
			throw std::invalid_argument("timed packet has negative PTS");
		}
		GST_BUFFER_PTS(buffer) =
			static_cast<GstClockTime>(packet.pts_us) * GST_USECOND;
		GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
		GST_BUFFER_DURATION(buffer) =
			gst_util_uint64_scale_int(1, GST_SECOND, 30);
		if (!packet.keyframe)
			GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
	}

	if (packet.eos)
		GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_MARKER);
	return buffer;
}

GstRtpSink::GstRtpSink(const RtpSinkConfig &config)
{
	try {
		initialize(config);
	} catch (...) {
		cleanup();
		throw;
	}
}

GstRtpSink::~GstRtpSink()
{
	cleanup();
}

void GstRtpSink::consume(const camera_mpp::EncodedPacketView &packet)
{
	/* push_buffer 接管 GstBuffer 所有权，调用者不能再 unref 该 buffer。 */
	throw_on_bus_error();
	GstBuffer *buffer = make_gst_buffer(packet);
	const GstFlowReturn flow =
		gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
	if (flow != GST_FLOW_OK)
		throw std::runtime_error("gst_app_src_push_buffer failed: " +
					 std::to_string(flow));
	throw_on_bus_error();
}

void GstRtpSink::end_of_stream()
{
	const GstFlowReturn flow = gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
	if (flow != GST_FLOW_OK)
		throw std::runtime_error("gst_app_src_end_of_stream failed: " +
					 std::to_string(flow));
}

void GstRtpSink::throw_on_bus_error()
{
	/* GStreamer 异步线程的错误通过 bus 返回，在发送循环中转成 C++ 异常。 */
	if (!bus_)
		return;

	GstMessage *message = gst_bus_pop_filtered(bus_, GST_MESSAGE_ERROR);
	if (!message)
		return;

	GError *error = nullptr;
	gchar *debug = nullptr;
	gst_message_parse_error(message, &error, &debug);
	std::ostringstream text;
	text << "GStreamer error";
	if (error && error->message)
		text << ": " << error->message;
	if (debug)
		text << " (" << debug << ')';
	if (error)
		g_error_free(error);
	g_free(debug);
	gst_message_unref(message);
	throw std::runtime_error(text.str());
}

std::uint64_t GstRtpSink::queue_overruns() const
{
	return queue_overruns_.load(std::memory_order_relaxed);
}

void GstRtpSink::on_queue_overrun(GstElement *, gpointer user_data)
{
	/* 队列满意味着网络侧落后；计数会驱动上层合并请求一个新 IDR。 */
	auto *sink = static_cast<GstRtpSink *>(user_data);
	sink->queue_overruns_.fetch_add(1, std::memory_order_relaxed);
}

void GstRtpSink::initialize(const RtpSinkConfig &config)
{
	/*
	 * appsrc 声明输入是按 access unit 对齐的 H.264 Annex-B；leaky downstream
	 * queue 满时丢旧数据，优先维持低延迟而不是无限积压。rtph264pay 把大帧按
	 * MTU 分包，并使用 payload type 96 和 90kHz 视频时钟。
	 */
	if (config.host.empty())
		throw std::invalid_argument("RTP host is empty");
	if (config.port < 1 || config.port > 65535)
		throw std::invalid_argument("RTP port must be in 1..65535");
	if (config.payload_type < 0 || config.payload_type > 127)
		throw std::invalid_argument("RTP payload type must be in 0..127");
	if (config.mtu < 256 || config.mtu > 65535)
		throw std::invalid_argument("RTP MTU must be in 256..65535");
	if (config.queue_buffers < 1)
		throw std::invalid_argument("queue buffer count must be positive");

	gst_init(nullptr, nullptr);
	pipeline_ = gst_pipeline_new("mpp-rtp-pipeline");
	require_element(pipeline_, "pipeline");

	auto appsrc = make_owned_element("appsrc", "source");
	auto queue = make_owned_element("queue", "network-queue");
	auto parser_owner = make_owned_element("h264parse", "parser");
	auto payloader_owner = make_owned_element("rtph264pay", "payloader");
	auto udp_owner = make_owned_element("udpsink", "udp-sink");
	appsrc_ = add_owned_element(pipeline_, std::move(appsrc), "appsrc");
	queue_ = add_owned_element(pipeline_, std::move(queue), "queue");
	GstElement *parser =
		add_owned_element(pipeline_, std::move(parser_owner), "h264parse");
	GstElement *payloader =
		add_owned_element(pipeline_, std::move(payloader_owner), "rtph264pay");
	GstElement *udp =
		add_owned_element(pipeline_, std::move(udp_owner), "udpsink");

	GstCaps *caps = gst_caps_new_simple(
		"video/x-h264",
		"stream-format", G_TYPE_STRING, "byte-stream",
		"alignment", G_TYPE_STRING, "au",
		"width", G_TYPE_INT, 1920,
		"height", G_TYPE_INT, 1080,
		"framerate", GST_TYPE_FRACTION, 30, 1,
		nullptr);
	if (!caps)
		throw std::runtime_error("gst_caps_new_simple failed");
	g_object_set(appsrc_,
		     "caps", caps,
		     "is-live", TRUE,
		     "format", GST_FORMAT_TIME,
		     "do-timestamp", FALSE,
		     "stream-type", GST_APP_STREAM_TYPE_STREAM,
		     nullptr);
	gst_caps_unref(caps);
	configure_bounded_appsrc(appsrc_, config.queue_buffers);

	g_object_set(queue_,
		     "max-size-buffers", config.queue_buffers,
		     "max-size-bytes", 0,
		     "max-size-time", static_cast<guint64>(0),
		     "leaky", 2,
		     nullptr);
	g_signal_connect(queue_, "overrun", G_CALLBACK(on_queue_overrun), this);
	g_object_set(parser, "config-interval", -1, nullptr);
	g_object_set(payloader,
		     "pt", config.payload_type,
		     "mtu", config.mtu,
		     "config-interval", 1,
		     nullptr);
	g_object_set(udp,
		     "host", config.host.c_str(),
		     "port", config.port,
		     "sync", FALSE,
		     "async", FALSE,
		     nullptr);

	if (!gst_element_link_many(appsrc_, queue_, parser, payloader, udp, nullptr))
		throw std::runtime_error("failed to link RTP pipeline");

	bus_ = gst_element_get_bus(pipeline_);
	if (!bus_)
		throw std::runtime_error("failed to get GStreamer bus");
	if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) ==
	    GST_STATE_CHANGE_FAILURE)
		throw std::runtime_error("failed to start RTP pipeline");
}

void GstRtpSink::cleanup() noexcept
{
	/* 先把 pipeline 置 NULL 停止内部线程，再按引用所有权释放 bus 和 pipeline。 */
	if (pipeline_)
		gst_element_set_state(pipeline_, GST_STATE_NULL);
	if (bus_)
		gst_object_unref(bus_);
	if (pipeline_)
		gst_object_unref(pipeline_);
	bus_ = nullptr;
	pipeline_ = nullptr;
	appsrc_ = nullptr;
	queue_ = nullptr;
}

} // 命名空间 camera_streaming
