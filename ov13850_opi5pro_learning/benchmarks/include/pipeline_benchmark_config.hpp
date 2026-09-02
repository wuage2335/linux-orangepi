#pragma once

#include <stdexcept>
#include <string>

namespace camera_timing {

enum class InputMode {
	Copy,
	DmaBuf,
};

enum class SinkMode {
	Null,
	Rtp,
};

struct BenchmarkConfig {
	std::string device = "/dev/video11";
	InputMode mode = InputMode::DmaBuf;
	SinkMode sink = SinkMode::Null;
	std::string host = "127.0.0.1";
	int port = 5004;
	int warmup = 30;
	int frames = 300;
	int bitrate = 8000000;
	int gop = 30;
	int mtu = 1200;
	int queue_buffers = 2;
	std::string csv_path = "pipeline-timing.csv";
};

inline int parse_positive_integer(const char *text, const char *option,
				  int maximum)
{
	std::size_t consumed = 0;
	long long value;
	try {
		value = std::stoll(text, &consumed, 10);
	} catch (const std::exception &) {
		throw std::invalid_argument(std::string(option) + " must be an integer");
	}
	if (text[consumed] != '\0' || value < 1 || value > maximum)
		throw std::invalid_argument(std::string(option) + " is out of range");
	return static_cast<int>(value);
}

inline BenchmarkConfig parse_benchmark_config(int argc, char **argv)
{
	BenchmarkConfig config;
	for (int index = 1; index < argc; index += 2) {
		if (index + 1 >= argc)
			throw std::invalid_argument(std::string("missing value for ") +
						    argv[index]);
		const std::string option = argv[index];
		const char *value = argv[index + 1];

		if (option == "--device")
			config.device = value;
		else if (option == "--mode") {
			const std::string mode = value;
			if (mode == "copy")
				config.mode = InputMode::Copy;
			else if (mode == "dmabuf")
				config.mode = InputMode::DmaBuf;
			else
				throw std::invalid_argument("--mode must be copy or dmabuf");
		} else if (option == "--sink") {
			const std::string sink = value;
			if (sink == "null")
				config.sink = SinkMode::Null;
			else if (sink == "rtp")
				config.sink = SinkMode::Rtp;
			else
				throw std::invalid_argument("--sink must be null or rtp");
		} else if (option == "--host")
			config.host = value;
		else if (option == "--port")
			config.port = parse_positive_integer(value, "--port", 65535);
		else if (option == "--warmup")
			config.warmup = parse_positive_integer(value, "--warmup", 1000000);
		else if (option == "--frames")
			config.frames = parse_positive_integer(value, "--frames", 1000000);
		else if (option == "--bitrate")
			config.bitrate = parse_positive_integer(value, "--bitrate", 1000000000);
		else if (option == "--gop")
			config.gop = parse_positive_integer(value, "--gop", 1000000);
		else if (option == "--mtu")
			config.mtu = parse_positive_integer(value, "--mtu", 65535);
		else if (option == "--queue-buffers")
			config.queue_buffers =
				parse_positive_integer(value, "--queue-buffers", 1000);
		else if (option == "--csv")
			config.csv_path = value;
		else
			throw std::invalid_argument("unknown option: " + option);
	}
	return config;
}

inline const char *input_mode_name(InputMode mode)
{
	return mode == InputMode::DmaBuf ? "dmabuf" : "copy";
}

inline const char *sink_mode_name(SinkMode mode)
{
	return mode == SinkMode::Rtp ? "rtp" : "null";
}

} // namespace camera_timing
