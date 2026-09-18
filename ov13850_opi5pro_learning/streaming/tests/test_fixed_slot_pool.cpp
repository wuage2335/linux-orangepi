#include <iostream>
#include <stdexcept>
#include <thread>

#include "fixed_slot_pool.hpp"

namespace {

using camera_mpp::FixedSlotPool;

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void test_two_slots_exhaust_and_reuse()
{
	FixedSlotPool pool(2);
	const auto first = pool.try_acquire();
	const auto second = pool.try_acquire();
	const auto third = pool.try_acquire();

	require(first.has_value(), "first slot acquisition failed");
	require(second.has_value(), "second slot acquisition failed");
	require(*first != *second, "pool returned the same active slot twice");
	require(!third.has_value(), "two-slot pool exceeded capacity");
	require(pool.capacity() == 2, "pool capacity changed");
	require(pool.in_flight() == 2, "in-flight count mismatch");
	require(pool.peak_in_flight() == 2, "peak in-flight mismatch");
	require(pool.acquire_misses() == 1, "acquire miss count mismatch");

	pool.release(*first);
	const auto reused = pool.try_acquire();
	require(reused.has_value() && *reused == *first,
		"released slot was not reused");
	require(pool.in_flight() == 2, "reuse changed in-flight count");

	pool.release(*second);
	pool.release(*reused);
	require(pool.in_flight() == 0, "released pool still has active slots");
}

void test_invalid_release_is_rejected()
{
	FixedSlotPool pool(2);
	const auto slot = pool.try_acquire();
	require(slot.has_value(), "slot acquisition failed");
	pool.release(*slot);

	bool duplicate_rejected = false;
	try {
		pool.release(*slot);
	} catch (const std::logic_error &) {
		duplicate_rejected = true;
	}
	require(duplicate_rejected, "duplicate release was accepted");

	bool range_rejected = false;
	try {
		pool.release(2);
	} catch (const std::out_of_range &) {
		range_rejected = true;
	}
	require(range_rejected, "out-of-range release was accepted");
}

void test_slots_can_be_released_from_other_threads()
{
	FixedSlotPool pool(2);
	const auto first = pool.try_acquire();
	const auto second = pool.try_acquire();
	require(first && second, "cross-thread test could not acquire slots");

	std::thread release_first([&] { pool.release(*first); });
	std::thread release_second([&] { pool.release(*second); });
	release_first.join();
	release_second.join();

	require(pool.in_flight() == 0, "cross-thread releases were not recorded");
	const auto reused_first = pool.try_acquire();
	const auto reused_second = pool.try_acquire();
	require(reused_first && reused_second,
		"cross-thread releases did not return both slots");
	pool.release(*reused_first);
	pool.release(*reused_second);
}

} // namespace

int main()
{
	try {
		test_two_slots_exhaust_and_reuse();
		test_invalid_release_is_rejected();
		test_slots_can_be_released_from_other_threads();
		std::cout << "PASS: fixed slot pool\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
