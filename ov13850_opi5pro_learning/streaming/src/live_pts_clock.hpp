#pragma once

#include <algorithm>
#include <cstdint>

namespace camera_streaming {

/**
 * @brief 将单调时钟映射为从零开始且不回退的 RTSP 媒体时间轴。
 *
 * RTSP 必须跟随真实采集节奏，而不能假设传感器严格运行在 30.00 fps。当前实采
 * 约为 30.05 fps，若按帧序号生成固定步长 PTS，每分钟会累计约 95 ms 偏差并
 * 最终超出接收端抖动窗口。本类使用微秒单调时钟生成 PTS，同时防御时钟回退。
 */
class LivePtsClock {
public:
	/**
	 * @brief 把一个单调时钟采样映射为当前会话的媒体 PTS。
	 * @param monotonic_us 当前单调时钟，单位为微秒。
	 * @return 以首次采样为零点、保证不小于上次结果的 PTS，单位为微秒。
	 */
	std::int64_t map(std::int64_t monotonic_us)
	{
		if (!initialized_) {
			origin_us_ = monotonic_us;
			last_pts_us_ = 0;
			initialized_ = true;
			return 0;
		}

		const std::int64_t elapsed = monotonic_us - origin_us_;
		last_pts_us_ = std::max(last_pts_us_, elapsed);
		return last_pts_us_;
	}

	/**
	 * @brief 清除会话零点和上次 PTS，使下一次采样重新从零开始。
	 */
	void reset()
	{
		initialized_ = false;
		origin_us_ = 0;
		last_pts_us_ = 0;
	}

private:
	bool initialized_ = false;
	std::int64_t origin_us_ = 0;
	std::int64_t last_pts_us_ = 0;
};

} // 命名空间 camera_streaming
