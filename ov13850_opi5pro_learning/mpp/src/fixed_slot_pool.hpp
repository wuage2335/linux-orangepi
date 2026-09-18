#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace camera_mpp {

/**
 * @brief 线程安全的固定容量slot索引池。
 *
 * 编码线程获取空闲slot，GStreamer释放线程归还slot。池只管理索引与统计，
 * 不知道slot中保存的是MPP buffer还是其他资源。
 */
class FixedSlotPool {
public:
	explicit FixedSlotPool(std::size_t capacity)
		: active_(capacity, false)
	{
		if (!capacity)
			throw std::invalid_argument("fixed slot pool capacity must be positive");
		free_slots_.reserve(capacity);
		for (std::size_t index = capacity; index > 0; --index)
			free_slots_.push_back(index - 1);
	}

	FixedSlotPool(const FixedSlotPool &) = delete;
	FixedSlotPool &operator=(const FixedSlotPool &) = delete;

	std::optional<std::size_t> try_acquire()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (free_slots_.empty()) {
			++acquire_misses_;
			return std::nullopt;
		}

		const std::size_t index = free_slots_.back();
		free_slots_.pop_back();
		active_[index] = true;
		++in_flight_;
		peak_in_flight_ = std::max(peak_in_flight_, in_flight_);
		return index;
	}

	void release(std::size_t index)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (index >= active_.size())
			throw std::out_of_range("fixed slot index is out of range");
		if (!active_[index])
			throw std::logic_error("fixed slot released more than once");

		active_[index] = false;
		--in_flight_;
		free_slots_.push_back(index);
	}

	std::size_t capacity() const
	{
		return active_.size();
	}

	std::size_t in_flight() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return in_flight_;
	}

	std::size_t peak_in_flight() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return peak_in_flight_;
	}

	std::uint64_t acquire_misses() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return acquire_misses_;
	}

private:
	mutable std::mutex mutex_;
	std::vector<std::size_t> free_slots_;
	std::vector<bool> active_;
	std::size_t in_flight_ = 0;
	std::size_t peak_in_flight_ = 0;
	std::uint64_t acquire_misses_ = 0;
};

} // namespace camera_mpp
