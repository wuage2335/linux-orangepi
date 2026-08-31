#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "gst_rtsp_server.hpp"
#include "mpp_encoder_core.hpp"
#include "v4l2_capture.hpp"

/*
 * 这是共享 RTSP 服务的组合入口。程序内部只有一套 V4L2 capture 和一套 MPP
 * encoder：后台 worker 持续采集编码，主线程运行 GLib/RTSP 事件循环。
 * 客户端只是订阅当前码流，不拥有也不重启摄像头。
 */
namespace {

using namespace camera_mpp;
using camera_streaming::GstRtspServerSink;
using camera_streaming::RtspServerConfig;
using Clock = std::chrono::steady_clock;

constexpr unsigned int kSkipFrames = 3;
std::atomic<bool> signal_stop_requested{false};

struct CommandLine {
	std::string device = "/dev/video11";
	int service = 8554;
	std::string mount = "/live";
	int bitrate = 8000000;
	int gop = 30;
	int mtu = 1200;
	int queue_buffers = 2;
	bool use_dmabuf = true;
};

struct WorkerResult {
	/* worker 结束后把统计和异常统一交还 main，避免跨线程直接抛异常。 */
	EncoderStats encoder;
	std::uint64_t frames = 0;
	std::uint64_t dropped = 0;
	std::uint64_t idr_requests = 0;
	unsigned int timeouts = 0;
	double elapsed_seconds = 0.0;
	std::exception_ptr error;
};

void handle_signal(int)
{
	signal_stop_requested.store(true, std::memory_order_relaxed);
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
			throw std::runtime_error(std::string("missing value for ") +
						 argv[index]);
		const std::string option = argv[index];
		const char *value = argv[index + 1];

		if (option == "--device")
			command.device = value;
		else if (option == "--service")
			command.service =
				parse_integer(value, "--service", 1, 65535);
		else if (option == "--mount")
			command.mount = value;
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
	if (command.mount.empty() || command.mount.front() != '/')
		throw std::runtime_error("--mount must start with /");
	return command;
}

void print_usage(const char *program)
{
	std::cerr << "usage: " << program << '\n'
		  << "  [--device /dev/video11] [--service 8554] [--mount /live]\n"
		  << "  [--bitrate 8000000] [--gop 30] [--mtu 1200]\n"
		  << "  [--queue-buffers 2] [--mode dmabuf|copy]\n";
}

void run_capture_worker(const CommandLine &command,
			GstRtspServerSink &sink,
			std::atomic<bool> &worker_stop,
			WorkerResult &result)
{
	/*
	 * worker 是唯一允许调用 V4L2 和 MPP 的线程。这样 encoder control、输入帧和
	 * DMA-BUF 所有权天然串行；RTSP 回调只发布“需要 IDR”等轻量请求。
	 */
	try {
		EncoderConfig encoder_config;
		encoder_config.bitrate = command.bitrate;
		encoder_config.gop = command.gop;
		encoder_config.ver_stride = command.use_dmabuf ? kHeight : kVerStride;

		const V4L2MemoryMode memory_mode = command.use_dmabuf ?
			V4L2MemoryMode::DmaBufExport : V4L2MemoryMode::MmapOnly;
		V4L2Capture capture(command.device.c_str(), memory_mode);
		MppEncoder encoder(encoder_config);
		encoder.write_header(sink, result.encoder);
		capture.start();

		for (unsigned int index = 0; index < kSkipFrames; ++index) {
			const CapturedFrame frame = capture.dequeue(result.timeouts);
			capture.requeue(frame.index);
		}

		std::uint32_t previous_sequence = 0;
		bool have_previous = false;
		const auto start = Clock::now();
		int frame_index = 0;

		while (!worker_stop.load(std::memory_order_relaxed) &&
		       !signal_stop_requested.load(std::memory_order_relaxed)) {
			/* RTSP 回调发布请求，编码线程在下一帧前串行调用 MPP control。 */
			if (sink.take_client_idr_request()) {
				encoder.request_idr();
				++result.idr_requests;
				std::cout << "IDR_REQUESTED reason=client-connect count="
					  << result.idr_requests << std::endl;
			}

			const CapturedFrame frame = capture.dequeue(result.timeouts);
			if (have_previous) {
				const std::uint32_t delta = frame.sequence - previous_sequence;
				if (delta == 0)
					++result.dropped;
				else if (delta > 1)
					result.dropped += delta - 1;
			}
			previous_sequence = frame.sequence;
			have_previous = true;

			if (!command.use_dmabuf) {
				encoder.load_nv12(frame.data, kInputSize);
				capture.requeue(frame.index);
			}

			if (command.use_dmabuf) {
				encoder.encode_external_frame(
					capture.mpp_buffer(frame.index), frame_index,
					false, sink, result.encoder);
				capture.requeue(frame.index);
			} else {
				encoder.encode_frame(frame_index, false, sink,
						     result.encoder);
			}
			++frame_index;
			++result.frames;
		}

		capture.stop();
		result.elapsed_seconds =
			std::chrono::duration<double>(Clock::now() - start).count();
	} catch (const std::exception &error) {
		/* 保存原异常，同时让 RTSP 主循环退出；main join 后重新抛出根因。 */
		result.error = std::current_exception();
		sink.report_worker_error(std::string("capture worker: ") + error.what());
	} catch (...) {
		result.error = std::current_exception();
		sink.report_worker_error("capture worker: unknown failure");
	}
	sink.request_stop();
}

} // namespace

int main(int argc, char **argv)
{
	/*
	 * main 的关闭协议是：通知 worker -> 退出 GLib loop -> join worker -> 检查两边
	 * 的错误 -> 输出统计。无论 Ctrl+C、采集失败还是 GStreamer bus error，都走
	 * 同一清理路径，避免后台线程继续访问已经析构的 sink。
	 */
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
		RtspServerConfig rtsp_config;
		rtsp_config.service = std::to_string(command.service);
		rtsp_config.mount = command.mount;
		rtsp_config.mtu = command.mtu;
		rtsp_config.queue_buffers = command.queue_buffers;

		GstRtspServerSink sink(rtsp_config);
		std::atomic<bool> worker_stop{false};
		WorkerResult result;
		std::thread worker(run_capture_worker,
				   std::cref(command),
				   std::ref(sink),
				   std::ref(worker_stop),
				   std::ref(result));

		try {
			sink.run();
		} catch (...) {
			worker_stop.store(true, std::memory_order_relaxed);
			worker.join();
			throw;
		}
		worker_stop.store(true, std::memory_order_relaxed);
		worker.join();

		if (result.error)
			std::rethrow_exception(result.error);
		sink.throw_on_error();

		std::cout << "codec=h264 mode="
			  << (command.use_dmabuf ? "dmabuf" : "copy")
			  << " bitrate=" << command.bitrate
			  << " gop=" << command.gop
			  << " endpoint=rtsp://0.0.0.0:" << command.service
			  << command.mount << '\n';
		std::cout << "frames_in=" << result.frames
			  << " timeouts=" << result.timeouts
			  << " dropped=" << result.dropped << '\n';
		std::cout << "packets=" << result.encoder.packets
			  << " idr_frames=" << result.encoder.idr_frames
			  << " encoded_bytes=" << result.encoder.encoded_bytes << '\n';
		std::cout << "rtsp_pushed_packets=" << sink.pushed_packets()
			  << " rtsp_dropped_packets=" << sink.dropped_packets()
			  << " connections=" << sink.client_connections()
			  << " disconnects=" << sink.client_disconnects()
			  << " client_idr_requests=" << result.idr_requests << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "elapsed_s=" << result.elapsed_seconds
			  << " loop_fps="
			  << (result.elapsed_seconds > 0.0 ?
			      result.frames / result.elapsed_seconds : 0.0)
			  << '\n';
		std::cout << "RTSP_SERVER_STOPPED" << std::endl;
	} catch (const std::exception &error) {
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
