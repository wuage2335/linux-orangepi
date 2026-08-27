#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "../src/live_pts_clock.hpp"

namespace {

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void test_tracks_real_monotonic_time_instead_of_nominal_frame_rate()
{
	camera_streaming::LivePtsClock clock;
	require(clock.map(1000000) == 0, "first live PTS must start at zero");
	require(clock.map(1033280) == 33280,
		"live PTS did not preserve the real frame interval");
	require(clock.map(1066560) == 66560,
		"live PTS drifted toward a nominal 30 fps interval");
}

void test_reset_starts_a_new_media_timeline()
{
	camera_streaming::LivePtsClock clock;
	clock.map(2000000);
	clock.map(2033300);
	clock.reset();
	require(clock.map(9000000) == 0,
		"reset media timeline did not restart from zero");
}

void test_regressing_sample_cannot_move_pts_backwards()
{
	camera_streaming::LivePtsClock clock;
	clock.map(5000000);
	require(clock.map(5033000) == 33000, "forward PTS mismatch");
	require(clock.map(5032000) == 33000, "live PTS moved backwards");
}

} // namespace

int main()
{
	try {
		test_tracks_real_monotonic_time_instead_of_nominal_frame_rate();
		test_reset_starts_a_new_media_timeline();
		test_regressing_sample_cannot_move_pts_backwards();
		std::cout << "PASS: live RTSP PTS clock\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
