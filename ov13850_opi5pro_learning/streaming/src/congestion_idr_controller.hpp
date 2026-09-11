#pragma once

#include <cstdint>
#include <stdexcept>

namespace camera_streaming {

/**
 * @brief 将一次或多次发送队列溢出合并为限频的 IDR 请求。
 *
 * 队列溢出表示接收或网络侧已经落后。控制器会记住冷却期内尚未处理的请求，
 * 等到允许的帧序号再触发 IDR，既帮助解码端恢复，又避免连续请求关键帧。
 */
class CongestionIdrController {
public:
	/**
	 * @brief 创建拥塞恢复控制器。
	 * @param cooldown_frames 两次 IDR 请求之间至少间隔的编码帧数。
	 * @throws std::invalid_argument 当冷却帧数不为正数时抛出。
	 */
	explicit CongestionIdrController(std::int64_t cooldown_frames)
		: cooldown_frames_(cooldown_frames)
	{
		if (cooldown_frames_ <= 0)
			throw std::invalid_argument("IDR cooldown must be positive");
	}

	/**
	 * @brief 根据累计队列溢出数判断当前帧是否需要请求 IDR。
	 * @param total_overruns 发送队列自启动以来的累计溢出次数。
	 * @param frame_index 当前编码帧序号，用于执行冷却限频。
	 * @return 当前帧应请求 IDR 时返回 true，否则返回 false。
	 */
	bool observe(std::uint64_t total_overruns, std::int64_t frame_index)
	{
		if (total_overruns < last_overruns_) {
			last_overruns_ = total_overruns;
			pending_ = false;
			return false;
		}

		if (total_overruns > last_overruns_) {
			overrun_events_ += total_overruns - last_overruns_;
			last_overruns_ = total_overruns;
			pending_ = true;
		}

		if (!pending_)
			return false;
		if (have_last_request_ &&
		    frame_index - last_request_frame_ < cooldown_frames_)
			return false;

		pending_ = false;
		have_last_request_ = true;
		last_request_frame_ = frame_index;
		++idr_requests_;
		return true;
	}

	/**
	 * @brief 获取已经观察到的队列溢出事件总数。
	 * @return 相邻采样之间新增溢出次数的累计值。
	 */
	std::uint64_t overrun_events() const
	{
		return overrun_events_;
	}

	/**
	 * @brief 获取控制器已经触发的 IDR 请求次数。
	 * @return IDR 请求累计值。
	 */
	std::uint64_t idr_requests() const
	{
		return idr_requests_;
	}

private:
	std::int64_t cooldown_frames_;
	std::uint64_t last_overruns_ = 0;
	std::uint64_t overrun_events_ = 0;
	std::uint64_t idr_requests_ = 0;
	std::int64_t last_request_frame_ = 0;
	bool have_last_request_ = false;
	bool pending_ = false;
};

} // 命名空间 camera_streaming
