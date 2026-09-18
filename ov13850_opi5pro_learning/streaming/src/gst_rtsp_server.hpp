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

/**
 * @brief 共享 RTSP 服务的监听地址、挂载点和 RTP 打包参数。
 */
struct RtspServerConfig {
	std::string service = "8554";
	std::string mount = "/live";
	int payload_type = 96;
	int mtu = 1200;
	int queue_buffers = 2;
};

/**
 * @brief 为持续运行的一套摄像头/编码器提供共享 RTSP 输出。
 *
 * GStreamer 在 GLib 主循环线程管理 RTSP 会话，MPP 则从采集线程调用 consume()。
 * 本类因此显式持有当前已准备 appsrc 的引用，并用互斥量保护跨线程共享状态。
 * 编码包生产者既不拥有客户端会话，也不会因客户端重连而重启摄像头。
 */
class GstRtspServerSink final : public camera_mpp::EncodedPacketSink {
public:
	/**
	 * @brief 创建共享 RTSP 服务并注册 GLib/GStreamer 回调。
	 * @param config 服务监听与媒体打包配置。
	 */
	explicit GstRtspServerSink(const RtspServerConfig &config);

	/**
	 * @brief 停止服务、关闭客户端并释放全部 GLib/GStreamer 对象。
	 */
	~GstRtspServerSink() override;

	GstRtspServerSink(const GstRtspServerSink &) = delete;
	GstRtspServerSink &operator=(const GstRtspServerSink &) = delete;

	/**
	 * @brief 从编码线程接收一个 H.264 包并推送给当前共享 appsrc。
	 * @param packet MPP 输出的编码包；配置头会缓存供新客户端重放。
	 */
	void consume(const camera_mpp::EncodedPacketView &packet) override;

	/**
	 * @brief 在调用线程运行 GLib 主循环，直到收到停止或错误请求。
	 */
	void run();

	/**
	 * @brief 以线程安全方式请求退出 GLib 主循环。
	 */
	void request_stop();

	/**
	 * @brief 取走一次由新客户端连接产生的 IDR 请求。
	 * @return appsrc 已就绪且存在待处理请求时返回 true。
	 */
	bool take_client_idr_request();

	/**
	 * @brief 记录采集/编码 worker 的错误并唤醒 RTSP 主循环。
	 * @param message worker 提供的根因说明。
	 */
	void report_worker_error(const std::string &message);

	/**
	 * @brief 检查跨线程保存的首个致命错误。
	 * @throws std::runtime_error 已记录致命错误时抛出。
	 */
	void throw_on_error() const;

	/** @brief 获取成功推送给 appsrc 的普通编码包数量。 */
	std::uint64_t pushed_packets() const;
	/** @brief 获取因无客户端或媒体正在刷新而丢弃的编码包数量。 */
	std::uint64_t dropped_packets() const;
	/** @brief 获取服务启动以来的客户端连接累计次数。 */
	std::uint64_t client_connections() const;
	/** @brief 获取服务启动以来的客户端断开累计次数。 */
	std::uint64_t client_disconnects() const;
	/** @brief 获取监听服务端口字符串。 */
	const std::string &service() const;
	/** @brief 获取 RTSP URL 挂载路径。 */
	const std::string &mount() const;

private:
	/**
	 * @brief 把 GStreamer media-configure 信号转发到实例方法。
	 * @param factory 触发回调的共享媒体工厂。
	 * @param media 本次准备的媒体对象。
	 * @param user_data 指向当前服务实例。
	 */
	static void on_media_configure(GstRTSPMediaFactory *factory,
				       GstRTSPMedia *media,
				       gpointer user_data);
	/**
	 * @brief 把媒体停止准备信号转发到实例清理方法。
	 * @param media 即将失效的媒体对象。
	 * @param user_data 指向当前服务实例。
	 */
	static void on_media_unprepared(GstRTSPMedia *media, gpointer user_data);

	/**
	 * @brief 把客户端连接信号转发到实例登记方法。
	 * @param server 接受连接的 RTSP 服务对象。
	 * @param client 新建立的客户端对象。
	 * @param user_data 指向当前服务实例。
	 */
	static void on_client_connected(GstRTSPServer *server,
					GstRTSPClient *client,
					gpointer user_data);
	/**
	 * @brief 把客户端关闭信号转发到实例注销方法。
	 * @param client 已关闭的客户端对象。
	 * @param user_data 指向当前服务实例。
	 */
	static void on_client_closed(GstRTSPClient *client, gpointer user_data);

	/**
	 * @brief 读取 GStreamer bus 错误并转交实例保存根因。
	 * @param bus 产生错误消息的消息总线。
	 * @param message GStreamer 错误消息。
	 * @param user_data 指向当前服务实例。
	 */
	static void on_bus_error(GstBus *bus,
			 GstMessage *message,
			 gpointer user_data);

	/** @brief 配置新建 media 的 appsrc、caps、bus 和生命周期回调。 */
	void configure_media(GstRTSPMedia *media);
	/** @brief 摘除停止的 media 并释放所持 appsrc、bus 和 media 引用。 */
	void clear_media(GstRTSPMedia *media) noexcept;
	/** @brief 登记新客户端，并安排重发编码头与请求 IDR。 */
	void add_client(GstRTSPClient *client);
	/** @brief 注销已关闭客户端，但保持唯一采集/编码 worker 运行。 */
	void remove_client(GstRTSPClient *client) noexcept;
	/** @brief 只保存首个致命错误，并请求主循环退出。 */
	void record_error(const std::string &message) noexcept;
	/** @brief 校验配置并创建、挂载、启动共享 RTSP 服务。 */
	void initialize(const RtspServerConfig &config);
	/** @brief 阻止新回调并按所有权顺序释放全部服务资源。 */
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
	camera_mpp::EncodedPacketView codec_header_{};
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

} // 命名空间 camera_streaming
