#include <arpa/inet.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "congestion_idr_controller.hpp"
#include "gst_rtp_sink.hpp"
#include "mpp_encoder_core.hpp"
#include "v4l2_capture.hpp"

namespace {

using namespace camera_mpp;
using camera_streaming::GstRtpSink;
using camera_streaming::RtpSinkConfig;
using Clock = std::chrono::steady_clock;

constexpr unsigned int kSkipFrames = 3;
std::atomic<bool> stop_requested{false};

struct CommandLine {
	std::string device = "/dev/video11";
	std::string host;
	int port = 5004;
	int frames = 300;
	int bitrate = 8000000;
	int gop = 30;
	int mtu = 1200;
	int queue_buffers = 2;
	bool use_dmabuf = true;
};

void handle_signal(int)
{
	stop_requested.store(true, std::memory_order_relaxed);
}

int parse_integer(const char *text, const char *option, int minimum, int maximum)
{
	std::size_t consumed = 0;
	long long value;
	try {
		value = std::stoll(text, &consumed, 10);
	} catch (const std::exception &) {
		throw std::runtime_error(std::string(option) + " must be an integer");
	}
	if (text[consumed] != '\0' || value < minimum || value > maximum)
		throw std::runtime_error(std::string(option) + " is out of range");
	return static_cast<int>(value);
}

CommandLine parse_command_line(int argc, char **argv)
{
	CommandLine command;
	for (int index = 1; index < argc; index += 2) {
		if (index + 1 >= argc)
			throw std::runtime_error(std::string("missing value for ") + argv[index]);
		const std::string option = argv[index];
		const char *value = argv[index + 1];

		if (option == "--device")
			command.device = value;
		else if (option == "--host")
			command.host = value;
		else if (option == "--port")
			command.port = parse_integer(value, "--port", 1, 65535);
		else if (option == "--frames")
			command.frames = parse_integer(value, "--frames", 1, 1000000000);
		else if (option == "--bitrate")
			command.bitrate =
				parse_integer(value, "--bitrate", 1, 1000000000);
		else if (option == "--gop")
			command.gop = parse_integer(value, "--gop", 1, 1000000);
		else if (option == "--mtu")
			command.mtu = parse_integer(value, "--mtu", 256, 65535);
		else if (option == "--queue-buffers")
			command.queue_buffers =
				parse_integer(value, "--queue-buffers", 1, 1000);
		else if (option == "--mode") {
			const std::string mode = value;
			if (mode == "dmabuf")
				command.use_dmabuf = true;
			else if (mode == "copy")
				command.use_dmabuf = false;
			else
				throw std::runtime_error("--mode must be dmabuf or copy");
		} else {
			throw std::runtime_error("unknown option: " + option);
		}
	}

	if (command.host.empty())
		throw std::runtime_error("--host is required");
	in_addr address = {};
	if (inet_pton(AF_INET, command.host.c_str(), &address) != 1)
		throw std::runtime_error("--host must be an IPv4 address");
	return command;
}

void print_usage(const char *program)
{
	std::cerr << "usage: " << program << '\n'
		  << "  --host IPv4 [--port 5004] [--device /dev/video11]\n"
		  << "  [--frames 300] [--bitrate 8000000] [--gop 30]\n"
		  << "  [--mtu 1200] [--queue-buffers 2] [--mode dmabuf|copy]\n";
}

} // namespace

int main(int argc, char **argv)
{
	CommandLine command;
	try {
		command = parse_command_line(argc, argv);
	} catch (const std::exception &error) {
		std::cerr << "ERROR: " << error.what() << '\n';
		print_usage(argv[0]);
		return 2;
	}

	std::signal(SIGINT, handle_signal);
	std::signal(SIGTERM, handle_signal);

	try {
		EncoderConfig encoder_config;
		encoder_config.bitrate = command.bitrate;
		encoder_config.gop = command.gop;
		encoder_config.ver_stride = command.use_dmabuf ? kHeight : kVerStride;

		RtpSinkConfig rtp_config;
		rtp_config.host = command.host;
		rtp_config.port = command.port;
		rtp_config.mtu = command.mtu;
		rtp_config.queue_buffers = command.queue_buffers;

		const V4L2MemoryMode memory_mode = command.use_dmabuf ?
			V4L2MemoryMode::DmaBufExport : V4L2MemoryMode::MmapOnly;
		V4L2Capture capture(command.device.c_str(), memory_mode);
		MppEncoder encoder(encoder_config);
		GstRtpSink rtp_sink(rtp_config);
		camera_streaming::CongestionIdrController congestion(30);
		EncoderStats stats;
		encoder.write_header(rtp_sink, stats);
		capture.start();

		unsigned int timeouts = 0;
		for (unsigned int index = 0; index < kSkipFrames; ++index) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			capture.requeue(frame.index);
		}

		std::uint64_t dropped = 0;
		std::uint32_t previous_sequence = 0;
		bool have_previous = false;
		int frames_sent = 0;
		const auto start = Clock::now();

		for (int index = 0;
		     index < command.frames &&
		     !stop_requested.load(std::memory_order_relaxed);
		     ++index) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			if (have_previous) {
				const std::uint32_t delta = frame.sequence - previous_sequence;
				if (delta == 0)
					++dropped;
				else if (delta > 1)
					dropped += delta - 1;
			}
			previous_sequence = frame.sequence;
			have_previous = true;

			if (!command.use_dmabuf) {
				encoder.load_nv12(frame.data, kInputSize);
				capture.requeue(frame.index);
			}

			const bool final_frame = index == command.frames - 1;
			if (command.use_dmabuf) {
				encoder.encode_external_frame(
					capture.mpp_buffer(frame.index), index,
					final_frame, rtp_sink, stats);
				capture.requeue(frame.index);
			} else {
				encoder.encode_frame(index, final_frame, rtp_sink, stats);
			}
			rtp_sink.throw_on_bus_error();
			if (congestion.observe(rtp_sink.queue_overruns(), index)) {
				encoder.request_idr();
				std::cerr << "RTP_QUEUE_CONGESTION frame=" << index
					  << " overruns=" << rtp_sink.queue_overruns()
					  << " request_idr=" << congestion.idr_requests()
					  << '\n';
			}
			++frames_sent;
		}

		const auto end = Clock::now();
		capture.stop();
		rtp_sink.end_of_stream();
		rtp_sink.throw_on_bus_error();

		const double seconds =
			std::chrono::duration<double>(end - start).count();
		std::cout << "codec=h264 mode="
			  << (command.use_dmabuf ? "dmabuf" : "copy")
			  << " bitrate=" << command.bitrate
			  << " gop=" << command.gop
			  << " destination=" << command.host << ':' << command.port
			  << '\n';
		std::cout << "frames_in=" << frames_sent
			  << " frames_sent=" << frames_sent
			  << " timeouts=" << timeouts
			  << " dropped=" << dropped << '\n';
		std::cout << "packets=" << stats.packets
			  << " idr_frames=" << stats.idr_frames
			  << " encoded_bytes=" << stats.encoded_bytes << '\n';
		std::cout << "rtp_clock_rate=90000 timestamp_step=3000"
			  << " queue_overruns=" << rtp_sink.queue_overruns() << '\n';
		std::cout << "congestion_events=" << congestion.overrun_events()
			  << " congestion_idr_requests=" << congestion.idr_requests()
			  << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "elapsed_s=" << seconds
			  << " loop_fps=" << (seconds > 0.0 ? frames_sent / seconds : 0.0)
			  << '\n';

		if (stop_requested.load(std::memory_order_relaxed)) {
			std::cout << "STREAM_RTP_INTERRUPTED\n";
			return 130;
		}
		if (frames_sent != command.frames)
			throw std::runtime_error("sender stopped before requested frame count");
		std::cout << "STREAM_RTP_OK\n";
	} catch (const std::exception &error) {
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
