#pragma once

#include <cstdint>
#include <stdexcept>

namespace camera_streaming {

/*
 * Coalesces one or more queue-overrun observations into a bounded-rate IDR
 * request. A pending request survives the cooldown instead of being lost.
 */
class CongestionIdrController {
public:
	explicit CongestionIdrController(std::int64_t cooldown_frames)
		: cooldown_frames_(cooldown_frames)
	{
		if (cooldown_frames_ <= 0)
			throw std::invalid_argument("IDR cooldown must be positive");
	}

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

	std::uint64_t overrun_events() const
	{
		return overrun_events_;
	}

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

} // namespace camera_streaming
