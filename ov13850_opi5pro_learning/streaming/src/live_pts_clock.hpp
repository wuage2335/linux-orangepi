#pragma once

#include <algorithm>
#include <cstdint>

namespace camera_streaming {

/*
 * RTSP must follow the real capture cadence, not an assumed exact 30.00 fps.
 * The sensor currently delivers about 30.05 fps; deriving PTS from frame index
 * makes media time gain roughly 95 ms per minute and eventually exceeds the
 * receiver jitter window. This mapper creates a zero-based media timeline from
 * a monotonic microsecond clock while preventing a defensive clock regression
 * from moving PTS backwards.
 */
class LivePtsClock {
public:
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

} // namespace camera_streaming
