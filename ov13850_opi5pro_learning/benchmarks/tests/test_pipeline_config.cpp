#include <iostream>
#include <stdexcept>

#include "pipeline_benchmark_config.hpp"

namespace {

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

camera_timing::BenchmarkConfig parse(std::initializer_list<const char *> args)
{
	char *argv[32] = {};
	int argc = 0;
	for (const char *arg : args)
		argv[argc++] = const_cast<char *>(arg);
	return camera_timing::parse_benchmark_config(argc, argv);
}

} // namespace

int main()
{
	const auto defaults = parse({"benchmark"});
	require(defaults.device == "/dev/video11", "default device");
	require(defaults.mode == camera_timing::InputMode::DmaBuf, "default mode");
	require(defaults.sink == camera_timing::SinkMode::Null, "default sink");
	require(defaults.warmup == 30, "default warmup");
	require(defaults.frames == 300, "default frames");

	const auto custom = parse({
		"benchmark", "--mode", "copy", "--sink", "rtp",
		"--host", "192.168.1.9", "--port", "6000",
		"--warmup", "5", "--frames", "40", "--bitrate", "4000000",
		"--gop", "15", "--csv", "/tmp/result.csv",
	});
	require(custom.mode == camera_timing::InputMode::Copy, "copy mode");
	require(custom.sink == camera_timing::SinkMode::Rtp, "rtp sink");
	require(custom.host == "192.168.1.9", "host");
	require(custom.port == 6000, "port");
	require(custom.warmup == 5 && custom.frames == 40, "frame counts");
	require(custom.bitrate == 4000000 && custom.gop == 15, "encoder config");
	require(custom.csv_path == "/tmp/result.csv", "csv path");

	bool rejected = false;
	try {
		(void)parse({"benchmark", "--frames", "0"});
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	require(rejected, "zero frames must be rejected");

	std::cout << "PASS: pipeline benchmark config\n";
	return 0;
}
