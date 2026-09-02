#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace camera_timing {

class SampleSeries {
public:
	explicit SampleSeries(std::string name)
		: name_(std::move(name))
	{
	}

	void add(double value)
	{
		if (!std::isfinite(value))
			throw std::invalid_argument("timing sample must be finite");
		values_.push_back(value);
	}

	std::size_t count() const
	{
		return values_.size();
	}

	double minimum() const
	{
		require_values();
		return *std::min_element(values_.begin(), values_.end());
	}

	double maximum() const
	{
		require_values();
		return *std::max_element(values_.begin(), values_.end());
	}

	double mean() const
	{
		require_values();
		return std::accumulate(values_.begin(), values_.end(), 0.0) /
		       static_cast<double>(values_.size());
	}

	double percentile(double percent) const
	{
		require_values();
		if (percent < 0.0 || percent > 100.0)
			throw std::out_of_range("percentile must be in 0..100");

		std::vector<double> sorted = values_;
		std::sort(sorted.begin(), sorted.end());
		const double scaled = percent / 100.0 * sorted.size();
		const std::size_t rank = std::max<std::size_t>(
			1, static_cast<std::size_t>(std::ceil(scaled)));
		return sorted[rank - 1];
	}

	void print(std::ostream &output) const
	{
		output << std::fixed << std::setprecision(2)
		       << "metric=" << name_
		       << " count=" << count()
		       << " mean=" << mean()
		       << " min=" << minimum()
		       << " p50=" << percentile(50.0)
		       << " p95=" << percentile(95.0)
		       << " p99=" << percentile(99.0)
		       << " max=" << maximum() << '\n';
	}

private:
	void require_values() const
	{
		if (values_.empty())
			throw std::logic_error("timing series is empty");
	}

	std::string name_;
	std::vector<double> values_;
};

} // namespace camera_timing
