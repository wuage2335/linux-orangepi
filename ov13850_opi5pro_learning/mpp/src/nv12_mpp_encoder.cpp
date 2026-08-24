#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mpp_encoder_core.hpp"

namespace {

/*
 * 文件编码前端用于先隔离验证 MPP 本身：输入是一帧 1920x1080 NV12，程序
 * 按指定次数重复提交并输出 H.264/H.265 Annex-B 裸流。它刻意不接 V4L2，
 * 因而编码失败时可以把问题限定在 MPP、参数或输入格式，而不是摄像头链路。
 */

using namespace camera_mpp;

int parse_positive(const char *text, const char *option)
{
	const int value = std::stoi(text);
	if (value <= 0)
		throw std::runtime_error(std::string(option) + " must be positive");
	return value;
}

struct CommandLine {
	EncoderConfig config;
	int frames = 300;
	int request_idr = -1;
	const char *input = nullptr;
	const char *output = nullptr;
};

CommandLine parse_command_line(int argc, char **argv)
{
	CommandLine command;
	int index = 1;

	while (index < argc - 2) {
		const std::string option = argv[index++];
		if (index >= argc - 1)
			throw std::runtime_error("missing value for " + option);
		const char *value = argv[index++];

		if (option == "--codec") {
			if (!std::strcmp(value, "h264"))
				command.config.codec = Codec::H264;
			else if (!std::strcmp(value, "h265"))
				command.config.codec = Codec::H265;
			else
				throw std::runtime_error("--codec must be h264 or h265");
		} else if (option == "--rc") {
			if (!std::strcmp(value, "cbr"))
				command.config.rc = RateControl::Cbr;
			else if (!std::strcmp(value, "vbr"))
				command.config.rc = RateControl::Vbr;
			else
				throw std::runtime_error("--rc must be cbr or vbr");
		} else if (option == "--bitrate") {
			command.config.bitrate = parse_positive(value, "--bitrate");
		} else if (option == "--gop") {
			command.config.gop = parse_positive(value, "--gop");
		} else if (option == "--frames") {
			command.frames = parse_positive(value, "--frames");
		} else if (option == "--request-idr") {
			command.request_idr = std::stoi(value);
			if (command.request_idr < 0)
				throw std::runtime_error("--request-idr must be non-negative");
		} else {
			throw std::runtime_error("unknown option: " + option);
		}
	}

	if (argc - index != 2)
		throw std::runtime_error("expected input and output paths");
	command.input = argv[index];
	command.output = argv[index + 1];
	if (command.request_idr >= command.frames)
		throw std::runtime_error("--request-idr must be smaller than --frames");
	return command;
}

std::vector<unsigned char> read_input(const char *path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("cannot open input NV12 file");
	if (input.tellg() != static_cast<std::streamoff>(kInputSize))
		throw std::runtime_error("input size must be 3110400 bytes");

	std::vector<unsigned char> data(kInputSize);
	input.seekg(0, std::ios::beg);
	input.read(reinterpret_cast<char *>(data.data()),
		   static_cast<std::streamsize>(data.size()));
	if (!input)
		throw std::runtime_error("failed to read complete NV12 frame");
	return data;
}

} // namespace

int main(int argc, char **argv)
{
	CommandLine command;
	try {
		command = parse_command_line(argc, argv);
	} catch (const std::exception &error) {
		std::cerr << "ERROR: " << error.what() << '\n'
			  << "usage: " << argv[0]
			  << " [--codec h264|h265] [--bitrate bps]"
			  << " [--gop frames] [--rc cbr|vbr] [--frames count]"
			  << " [--request-idr index] input.nv12 output\n";
		return 2;
	}

	std::remove(command.output);
	try {
		const std::vector<unsigned char> input = read_input(command.input);
		std::ofstream output(command.output, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot create output bitstream file");

		MppEncoder encoder(command.config);
		/* 静态文件只需装载一次；随后重复提交同一 MPP 内部缓冲区。 */
		encoder.load_nv12(input.data(), input.size());
		EncoderStats stats;
		encoder.write_header(output, stats);

		const auto start = std::chrono::steady_clock::now();
		int frames_out = 0;
		for (int index = 0; index < command.frames; ++index) {
			/* request-idr 用于验证运行中请求关键帧的控制路径。 */
			if (index == command.request_idr)
				encoder.request_idr();
			const bool eos = encoder.encode_frame(
				index, index == command.frames - 1, output, stats);
			++frames_out;
			if (index == command.frames - 1 && !eos)
				throw std::runtime_error("last packet did not carry EOS");
		}
		const auto end = std::chrono::steady_clock::now();

		output.close();
		if (!output)
			throw std::runtime_error("failed to finalize bitstream file");

		const double elapsed =
			std::chrono::duration<double>(end - start).count();
		const double encode_fps = elapsed > 0.0 ? command.frames / elapsed : 0.0;

		std::cout << "codec=" << codec_name(command.config.codec)
			  << " rc=" << rc_name(command.config.rc)
			  << " width=" << kWidth << " height=" << kHeight
			  << " fps=" << kFps
			  << " bitrate=" << command.config.bitrate
			  << " gop=" << command.config.gop << '\n';
		std::cout << "hor_stride=" << kHorStride
			  << " ver_stride=" << kVerStride
			  << " frame_buffer_bytes=" << kFrameSize << '\n';
		std::cout << "frames_in=" << command.frames
			  << " frames_out=" << frames_out
			  << " packets=" << stats.packets
			  << " idr_frames=" << stats.idr_frames
			  << " encoded_bytes=" << stats.encoded_bytes << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "elapsed_s=" << elapsed << " encode_fps=" << encode_fps << '\n';
		std::cout << "MPP_FILE_ENCODE_OK\n";
	} catch (const std::exception &error) {
		std::remove(command.output);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
