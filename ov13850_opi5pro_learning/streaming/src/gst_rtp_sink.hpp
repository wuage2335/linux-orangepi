#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "encoded_packet_sink.hpp"

namespace camera_streaming {

/**
 * @brief RTP/UDP 输出管线的运行参数。
 */
struct RtpSinkConfig {
	std::string host;
	int port = 5004;
	int payload_type = 96;
	int mtu = 1200;
	int queue_buffers = 2;
};

/**
 * @brief 限制appsrc内部队列并在满载时丢弃最旧buffer。
 * @param appsrc 待配置的GStreamer appsrc元素。
 * @param max_buffers 允许appsrc内部持有的最大buffer数量。
 */
void configure_bounded_appsrc(GstElement *appsrc, int max_buffers);

/**
 * @brief 把编码包转换为 GStreamer 缓冲区。
 * @param packet MPP 输出的 H.264 编码包视图。
 * @return 新建的 GstBuffer；带 owner 时共享原存储，否则复制数据。
 * @throws std::invalid_argument 编码包描述无效时抛出。
 * @throws std::runtime_error 分配、包装或复制 GstBuffer 失败时抛出。
 */
GstBuffer *make_gst_buffer(const camera_mpp::EncodedPacketView &packet);

/**
 * @brief 将 MPP 的 H.264 编码包送入 GStreamer RTP/UDP 管线。
 *
 * 类实例拥有 pipeline、appsrc、queue 和 bus 的引用，并把网络侧异步错误转换为
 * 编码循环可处理的 C++ 异常。
 */
class GstRtpSink final : public camera_mpp::EncodedPacketSink {
public:
	/**
	 * @brief 按给定地址、端口和队列参数创建并启动 RTP 管线。
	 * @param config RTP 输出配置。
	 * @throws std::invalid_argument 配置超出允许范围时抛出。
	 * @throws std::runtime_error GStreamer 元素创建、连接或启动失败时抛出。
	 */
	explicit GstRtpSink(const RtpSinkConfig &config);

	/**
	 * @brief 停止 GStreamer 管线并释放全部引用。
	 */
	~GstRtpSink() override;

	GstRtpSink(const GstRtpSink &) = delete;
	GstRtpSink &operator=(const GstRtpSink &) = delete;

	/**
	 * @brief 将一个编码包推送给 appsrc。
	 * @param packet 待发送的 H.264 编码包。
	 * @throws std::runtime_error 管线报告错误或推送失败时抛出。
	 */
	void consume(const camera_mpp::EncodedPacketView &packet) override;

	/**
	 * @brief 通知 appsrc 输入结束，使管线发送流结束事件。
	 */
	void end_of_stream();

	/**
	 * @brief 检查 GStreamer bus，并把首个异步错误转换为异常。
	 * @throws std::runtime_error bus 中存在错误消息时抛出。
	 */
	void throw_on_bus_error();

	/**
	 * @brief 获取低延迟队列发生溢出的累计次数。
	 * @return queue 的 overrun 信号计数。
	 */
	std::uint64_t queue_overruns() const;

private:
	/**
	 * @brief 处理 GStreamer queue 的 overrun 信号并增加原子计数。
	 * @param queue 触发信号的队列元素，本实现不直接使用。
	 * @param user_data 指向当前 GstRtpSink 实例。
	 */
	static void on_queue_overrun(GstElement *queue, gpointer user_data);

	/**
	 * @brief 校验配置、创建元素、连接并启动 RTP 管线。
	 * @param config RTP 输出配置。
	 */
	void initialize(const RtpSinkConfig &config);

	/**
	 * @brief 按所有权顺序停止管线并释放 bus、pipeline 等资源。
	 */
	void cleanup() noexcept;

	GstElement *pipeline_ = nullptr;
	GstElement *appsrc_ = nullptr;
	GstElement *queue_ = nullptr;
	GstBus *bus_ = nullptr;
	std::atomic<std::uint64_t> queue_overruns_{0};
};

} // 命名空间 camera_streaming
