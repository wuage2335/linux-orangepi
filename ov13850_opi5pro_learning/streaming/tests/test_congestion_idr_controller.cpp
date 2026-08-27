#include <iostream>
#include <stdexcept>

#include "../src/congestion_idr_controller.hpp"

namespace {

using camera_streaming::CongestionIdrController;

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void test_coalesces_overruns_with_cooldown()
{
	CongestionIdrController controller(30);

	require(!controller.observe(0, 0), "requested IDR without overrun");
	require(controller.observe(1, 10), "first overrun did not request IDR");
	require(!controller.observe(1, 11), "same overrun requested twice");
	require(!controller.observe(2, 20), "cooldown was ignored");
	require(!controller.observe(2, 39), "pending request fired too early");
	require(controller.observe(2, 40), "pending request was lost");
	require(controller.idr_requests() == 2, "unexpected IDR request count");
	require(controller.overrun_events() == 2, "unexpected overrun event count");
}

void test_handles_counter_reset()
{
	CongestionIdrController controller(10);
	require(controller.observe(3, 5), "initial accumulated overruns not handled");
	require(!controller.observe(0, 6), "counter reset requested IDR");
	require(controller.observe(1, 15), "post-reset overrun not handled");
	require(controller.overrun_events() == 4, "counter reset event total mismatch");
}

} // namespace

int main()
{
	try {
		test_coalesces_overruns_with_cooldown();
		test_handles_counter_reset();
		std::cout << "PASS: congestion IDR controller\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
