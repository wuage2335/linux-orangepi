#include <cmath>
#include <iostream>
#include <stdexcept>

#include "timing_stats.hpp"

namespace {

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

} // namespace

int main()
{
	camera_timing::SampleSeries samples("example_us");
	for (int value = 100; value >= 1; --value)
		samples.add(static_cast<double>(value));

	require(samples.count() == 100, "count mismatch");
	require(samples.minimum() == 1.0, "minimum mismatch");
	require(samples.maximum() == 100.0, "maximum mismatch");
	require(std::abs(samples.mean() - 50.5) < 0.0001, "mean mismatch");
	require(samples.percentile(50.0) == 50.0, "p50 mismatch");
	require(samples.percentile(95.0) == 95.0, "p95 mismatch");
	require(samples.percentile(99.0) == 99.0, "p99 mismatch");

	bool rejected_empty = false;
	try {
		camera_timing::SampleSeries empty("empty");
		(void)empty.mean();
	} catch (const std::logic_error &) {
		rejected_empty = true;
	}
	require(rejected_empty, "empty series must reject summary");

	samples.print(std::cout);
	std::cout << "PASS: timing statistics\n";
	return 0;
}
