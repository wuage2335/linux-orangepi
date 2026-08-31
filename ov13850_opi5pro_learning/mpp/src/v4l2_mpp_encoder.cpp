#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "mpp_encoder_core.hpp"
#include "v4l2_capture.hpp"

namespace {

/*
 * 实时前端连接 RKISP mainpath 与 MPP：
 *   copy   : V4L2 MMAP -> CPU 逐行复制 -> MPP 内部 DRM buffer；
 *   dmabuf : V4L2 MMAP/EXPBUF -> MPP 导入同一 DMA-BUF。
 * 两条路径使用同一编码核心和 300 帧验收窗口，以便比较搬运成本而不是比较
 * 两套编码器。当前工具固定接收已配置好的 1920x1080 NV12 /dev/video11。
 * 输出仍是文件而不是网络流，这样阶段 4 可以先验证实时采集与硬件编码，再把
 * packet sink 替换为阶段 5 的 RTP/RTSP sink。
 */

using namespace camera_mpp;
using Clock = std::chrono::steady_clock;

constexpr unsigned int kSkipFrames = 3;
constexpr unsigned int kFrames = 300;

static_assert(kCaptureWidth == kWidth);
static_assert(kCaptureHeight == kHeight);
static_assert(kCaptureInputSize == kInputSize);

double elapsed_us(const Clock::time_point &start, const Clock::time_point &end)
{
	return std::chrono::duration<double, std::micro>(end - start).count();
}

} // namespace

int main(int argc, char **argv)
{
	/*
	 * 每一帧都经历 DQBUF -> MPP -> QBUF。copy 模式在复制完成后就能 QBUF；
	 * DMA-BUF 模式中 MPP 仍读取同一块采集内存，所以必须等同步取回编码 packet
	 * 后再 QBUF。过早归还会让 ISP 覆盖编码器尚未读完的画面，形成随机花屏。
	 */
	bool use_dmabuf = false;
	const char *device = nullptr;
	const char *output_path = nullptr;
	if (argc == 3) {
		device = argv[1];
		output_path = argv[2];
	} else if (argc == 4 && !std::strcmp(argv[1], "--dmabuf")) {
		use_dmabuf = true;
		device = argv[2];
		output_path = argv[3];
	} else {
		std::cerr << "ERROR: usage: " << argv[0]
			  << " [--dmabuf] <video-device> <output.h264>\n";
		return 2;
	}
	std::remove(output_path);

	try {
		EncoderConfig config;
		/*
		 * 关键布局差异：copy 目标由我们按 1088 行构造；V4L2 导出的
		 * 单平面 NV12 的 UV 实际紧跟在第 1080 行后。DMA-BUF 若错误声明
		 * 为 1088，会让 MPP 从错误位置读取 UV，虽然编码调用仍可能成功。
		 */
		config.ver_stride = use_dmabuf ? kHeight : kVerStride;
		std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("cannot create H.264 output");
		const V4L2MemoryMode memory_mode = use_dmabuf ?
			V4L2MemoryMode::DmaBufExport : V4L2MemoryMode::MmapOnly;
		V4L2Capture capture(device, memory_mode);
		MppEncoder encoder(config);
		OstreamPacketSink output_sink(output);
		EncoderStats stats;
		encoder.write_header(output_sink, stats);
		capture.start();

		/* 前三帧用于等待 sensor 曝光、ISP 和多级队列稳定，不进入结果统计。 */
		unsigned int timeouts = 0;
		for (unsigned int i = 0; i < kSkipFrames; ++i) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			capture.requeue(frame.index);
		}

		double copy_total_us = 0.0;
		double mpp_total_us = 0.0;
		std::uint64_t dropped = 0;
		std::uint32_t previous_sequence = 0;
		bool have_previous = false;
		const auto loop_start = Clock::now();

		for (unsigned int index = 0; index < kFrames; ++index) {
			const CapturedFrame frame = capture.dequeue(timeouts);
			/*
			 * sequence 由 V4L2 驱动逐帧递增。与上一帧的差值大于 1 表示中间有帧
			 * 没送到本程序；这和编码器主动压缩帧内容不是同一个概念。
			 */
			if (have_previous) {
				const std::uint32_t delta = frame.sequence - previous_sequence;
				if (delta == 0)
					++dropped;
				else if (delta > 1)
					dropped += delta - 1;
			}
			previous_sequence = frame.sequence;
			have_previous = true;

			if (!use_dmabuf) {
				/* copy 完成后 V4L2 原 buffer 已无引用，可立即归还采集队列。 */
				const auto copy_start = Clock::now();
				encoder.load_nv12(frame.data, kInputSize);
				const auto copy_end = Clock::now();
				copy_total_us += elapsed_us(copy_start, copy_end);
				capture.requeue(frame.index);
			}

			const auto mpp_start = Clock::now();
			bool eos;
			if (use_dmabuf) {
				eos = encoder.encode_external_frame(
					capture.mpp_buffer(frame.index), index,
					index == kFrames - 1, output_sink, stats);
			} else {
				eos = encoder.encode_frame(
					index, index == kFrames - 1, output_sink, stats);
			}
			const auto mpp_end = Clock::now();
			mpp_total_us += elapsed_us(mpp_start, mpp_end);
			if (use_dmabuf)
				/* MPP 阻塞取回 packet 后才归还，避免 ISP 覆盖正在编码的帧。 */
				capture.requeue(frame.index);
			if (index == kFrames - 1 && !eos)
				throw std::runtime_error("last packet did not carry EOS");
		}

		const auto loop_end = Clock::now();
		capture.stop();
		output.close();
		if (!output)
			throw std::runtime_error("failed to finalize H.264 output");

		const double loop_seconds =
			std::chrono::duration<double>(loop_end - loop_start).count();
		std::cout << "codec=h264 mode="
			  << (use_dmabuf ? "dmabuf" : "copy")
			  << " bitrate=8000000 gop=60\n";
		std::cout << "pre_skipped=3 frames_in=300 frames_out=300"
			  << " timeouts=" << timeouts << " dropped=" << dropped << '\n';
		std::cout << "packets=" << stats.packets
			  << " idr_frames=" << stats.idr_frames
			  << " encoded_bytes=" << stats.encoded_bytes << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "copy_average_us=" << copy_total_us / kFrames
			  << " mpp_average_us=" << mpp_total_us / kFrames
			  << " loop_fps=" << kFrames / loop_seconds << '\n';
		std::cout << "MPP_V4L2_ENCODE_OK\n";
	} catch (const std::exception &error) {
		std::remove(output_path);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
