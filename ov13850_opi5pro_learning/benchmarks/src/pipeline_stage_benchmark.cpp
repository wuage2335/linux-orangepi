#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <time.h>

#include "gst_rtp_sink.hpp"
#include "mpp_encoder_core.hpp"
#include "pipeline_benchmark_config.hpp"
#include "timing_stats.hpp"
#include "v4l2_capture.hpp"

namespace {

using camera_mpp::CapturedFrame;
using camera_mpp::EncodedPacketSink;
using camera_mpp::EncodedPacketView;
using camera_mpp::EncoderConfig;
using camera_mpp::EncoderStats;
using camera_mpp::MppEncoder;
using camera_mpp::V4L2Capture;
using camera_mpp::V4L2MemoryMode;
using camera_streaming::GstRtpSink;
using camera_streaming::RtpSinkConfig;
using camera_timing::BenchmarkConfig;
using camera_timing::InputMode;
using camera_timing::SampleSeries;
using camera_timing::SinkMode;

std::uint64_t monotonic_ns()
{
	timespec value = {};
	if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
		throw std::runtime_error("clock_gettime(CLOCK_MONOTONIC) failed");
	return static_cast<std::uint64_t>(value.tv_sec) * 1000000000ULL +
	       static_cast<std::uint64_t>(value.tv_nsec);
}

double elapsed_us(std::uint64_t start_ns, std::uint64_t end_ns)
{
	return static_cast<double>(end_ns - start_ns) / 1000.0;
}

class TimedPacketSink final : public EncodedPacketSink {
public:
	explicit TimedPacketSink(const BenchmarkConfig &config)
	{
		if (config.sink == SinkMode::Rtp) {
			RtpSinkConfig rtp;
			rtp.host = config.host;
			rtp.port = config.port;
			rtp.mtu = config.mtu;
			rtp.queue_buffers = config.queue_buffers;
			rtp_ = std::make_unique<GstRtpSink>(rtp);
		}
	}

	void consume(const EncodedPacketView &packet) override
	{
		const std::uint64_t start = monotonic_ns();
		if (rtp_)
			rtp_->consume(packet);
		const std::uint64_t end = monotonic_ns();
		frame_push_us_ += elapsed_us(start, end);
		++packets_;
		bytes_ += packet.size;
	}

	void begin_frame()
	{
		frame_push_us_ = 0.0;
	}

	double frame_push_us() const
	{
		return frame_push_us_;
	}

	void finish()
	{
		if (rtp_) {
			rtp_->end_of_stream();
			rtp_->throw_on_bus_error();
		}
	}

	std::uint64_t queue_overruns() const
	{
		return rtp_ ? rtp_->queue_overruns() : 0;
	}

	std::uint64_t packets() const { return packets_; }
	std::uint64_t bytes() const { return bytes_; }

private:
	std::unique_ptr<GstRtpSink> rtp_;
	double frame_push_us_ = 0.0;
	std::uint64_t packets_ = 0;
	std::uint64_t bytes_ = 0;
};

struct FrameTiming {
	int sample = 0;
	std::uint32_t sequence = 0;
	std::uint64_t kernel_timestamp_ns = 0;
	double frame_interval_us = -1.0;
	double dequeue_wait_us = 0.0;
	double sof_to_dequeue_us = -1.0;
	double copy_us = 0.0;
	double encode_call_us = 0.0;
	double sink_push_us = 0.0;
	double mpp_without_sink_us = 0.0;
	double requeue_us = 0.0;
	double post_dequeue_us = 0.0;
};

void process_frame(const BenchmarkConfig &config,
		   V4L2Capture &capture,
		   MppEncoder &encoder,
		   TimedPacketSink &sink,
		   EncoderStats &encoder_stats,
		   unsigned int &timeouts,
		   int frame_index,
		   FrameTiming *timing)
{
	const std::uint64_t wait_start = monotonic_ns();
	const CapturedFrame frame = capture.dequeue(timeouts);
	const std::uint64_t dequeued = monotonic_ns();

	if (timing) {
		timing->sequence = frame.sequence;
		timing->kernel_timestamp_ns = frame.timestamp_ns;
		timing->dequeue_wait_us = elapsed_us(wait_start, dequeued);
		if (frame.timestamp_ns && dequeued >= frame.timestamp_ns)
			timing->sof_to_dequeue_us =
				elapsed_us(frame.timestamp_ns, dequeued);
	}

	double copy_us = 0.0;
	double requeue_us = 0.0;
	if (config.mode == InputMode::Copy) {
		const std::uint64_t copy_start = monotonic_ns();
		encoder.load_nv12(frame.data, camera_mpp::kInputSize);
		copy_us = elapsed_us(copy_start, monotonic_ns());

		const std::uint64_t qbuf_start = monotonic_ns();
		capture.requeue(frame.index);
		requeue_us += elapsed_us(qbuf_start, monotonic_ns());
	}

	sink.begin_frame();
	const std::uint64_t encode_start = monotonic_ns();
	if (config.mode == InputMode::DmaBuf) {
		encoder.encode_external_frame(capture.mpp_buffer(frame.index),
					       frame_index, false, sink, encoder_stats);
	} else {
		encoder.encode_frame(frame_index, false, sink, encoder_stats);
	}
	const double encode_call_us = elapsed_us(encode_start, monotonic_ns());

	if (config.mode == InputMode::DmaBuf) {
		const std::uint64_t qbuf_start = monotonic_ns();
		capture.requeue(frame.index);
		requeue_us += elapsed_us(qbuf_start, monotonic_ns());
	}

	if (timing) {
		timing->copy_us = copy_us;
		timing->encode_call_us = encode_call_us;
		timing->sink_push_us = sink.frame_push_us();
		timing->mpp_without_sink_us =
			std::max(0.0, encode_call_us - timing->sink_push_us);
		timing->requeue_us = requeue_us;
		timing->post_dequeue_us = elapsed_us(dequeued, monotonic_ns());
	}
}

void write_csv(const std::string &path, const std::vector<FrameTiming> &rows)
{
	std::ofstream output(path, std::ios::trunc);
	if (!output)
		throw std::runtime_error("cannot create timing CSV: " + path);
	output << "sample,sequence,kernel_timestamp_ns,frame_interval_us,"
		  "dequeue_wait_us,sof_to_dequeue_us,copy_us,encode_call_us,"
		  "sink_push_us,mpp_without_sink_us,requeue_us,post_dequeue_us\n";
	output << std::fixed << std::setprecision(3);
	for (const FrameTiming &row : rows) {
		output << row.sample << ',' << row.sequence << ','
		       << row.kernel_timestamp_ns << ',' << row.frame_interval_us << ','
		       << row.dequeue_wait_us << ',' << row.sof_to_dequeue_us << ','
		       << row.copy_us << ',' << row.encode_call_us << ','
		       << row.sink_push_us << ',' << row.mpp_without_sink_us << ','
		       << row.requeue_us << ',' << row.post_dequeue_us << '\n';
	}
}

void print_series(const std::vector<FrameTiming> &rows)
{
	SampleSeries interval("frame_interval_us");
	SampleSeries wait("dequeue_wait_us");
	SampleSeries sof_to_dq("sof_to_dequeue_us");
	SampleSeries copy("copy_us");
	SampleSeries encode("encode_call_us");
	SampleSeries sink("sink_push_us");
	SampleSeries mpp("mpp_without_sink_us");
	SampleSeries requeue("requeue_us");
	SampleSeries post_dq("post_dequeue_us");

	for (const FrameTiming &row : rows) {
		if (row.frame_interval_us >= 0.0)
			interval.add(row.frame_interval_us);
		wait.add(row.dequeue_wait_us);
		if (row.sof_to_dequeue_us >= 0.0)
			sof_to_dq.add(row.sof_to_dequeue_us);
		copy.add(row.copy_us);
		encode.add(row.encode_call_us);
		sink.add(row.sink_push_us);
		mpp.add(row.mpp_without_sink_us);
		requeue.add(row.requeue_us);
		post_dq.add(row.post_dequeue_us);
	}

	interval.print(std::cout);
	wait.print(std::cout);
	sof_to_dq.print(std::cout);
	copy.print(std::cout);
	encode.print(std::cout);
	sink.print(std::cout);
	mpp.print(std::cout);
	requeue.print(std::cout);
	post_dq.print(std::cout);
}

} // namespace

int main(int argc, char **argv)
{
	BenchmarkConfig config;
	try {
		config = camera_timing::parse_benchmark_config(argc, argv);
	} catch (const std::exception &error) {
		std::cerr << "ERROR: " << error.what() << '\n';
		return 2;
	}

	try {
		const std::uint64_t capture_init_start = monotonic_ns();
		const V4L2MemoryMode memory_mode = config.mode == InputMode::DmaBuf ?
			V4L2MemoryMode::DmaBufExport : V4L2MemoryMode::MmapOnly;
		V4L2Capture capture(config.device.c_str(), memory_mode);
		const double capture_init_us =
			elapsed_us(capture_init_start, monotonic_ns());

		EncoderConfig encoder_config;
		encoder_config.bitrate = config.bitrate;
		encoder_config.gop = config.gop;
		encoder_config.ver_stride = config.mode == InputMode::DmaBuf ?
			camera_mpp::kHeight : camera_mpp::kVerStride;
		const std::uint64_t encoder_init_start = monotonic_ns();
		MppEncoder encoder(encoder_config);
		const double encoder_init_us =
			elapsed_us(encoder_init_start, monotonic_ns());

		const std::uint64_t sink_init_start = monotonic_ns();
		TimedPacketSink sink(config);
		const double sink_init_us = elapsed_us(sink_init_start, monotonic_ns());

		const std::uint64_t header_start = monotonic_ns();
		EncoderStats encoder_stats;
		encoder.write_header(sink, encoder_stats);
		const double header_us = elapsed_us(header_start, monotonic_ns());

		const std::uint64_t stream_start = monotonic_ns();
		capture.start();
		const double streamon_us = elapsed_us(stream_start, monotonic_ns());
		unsigned int timeouts = 0;
		int frame_index = 0;
		FrameTiming first_warmup;
		for (int index = 0; index < config.warmup; ++index)
			process_frame(config, capture, encoder, sink, encoder_stats, timeouts,
				      frame_index++, index == 0 ? &first_warmup : nullptr);

		std::vector<FrameTiming> rows;
		rows.reserve(config.frames);
		std::uint64_t prior_timestamp = 0;
		std::uint32_t prior_sequence = 0;
		bool have_prior = false;
		std::uint64_t dropped = 0;
		for (int index = 0; index < config.frames; ++index) {
			FrameTiming row;
			row.sample = index;
			process_frame(config, capture, encoder, sink, encoder_stats, timeouts,
				      frame_index++, &row);
			if (prior_timestamp && row.kernel_timestamp_ns > prior_timestamp)
				row.frame_interval_us =
					elapsed_us(prior_timestamp, row.kernel_timestamp_ns);
			if (have_prior) {
				const std::uint32_t delta = row.sequence - prior_sequence;
				if (delta == 0)
					++dropped;
				else if (delta > 1)
					dropped += delta - 1;
			}
			prior_timestamp = row.kernel_timestamp_ns;
			prior_sequence = row.sequence;
			have_prior = true;
			rows.push_back(row);
		}
		capture.stop();
		sink.finish();
		const double run_seconds =
			elapsed_us(stream_start, monotonic_ns()) / 1000000.0;

		write_csv(config.csv_path, rows);
		std::cout << std::fixed << std::setprecision(2)
			  << "mode=" << camera_timing::input_mode_name(config.mode)
			  << " sink=" << camera_timing::sink_mode_name(config.sink)
			  << " warmup=" << config.warmup
			  << " frames=" << config.frames << '\n'
			  << "capture_init_us=" << capture_init_us
			  << " encoder_init_us=" << encoder_init_us
			  << " sink_init_us=" << sink_init_us
			  << " header_us=" << header_us
			  << " streamon_us=" << streamon_us
			  << " first_dq_wait_after_streamon_us="
			  << first_warmup.dequeue_wait_us << '\n'
			  << "run_seconds=" << run_seconds
			  << " timeouts=" << timeouts
			  << " dropped=" << dropped
			  << " packets=" << sink.packets()
			  << " encoded_bytes=" << sink.bytes()
			  << " queue_overruns=" << sink.queue_overruns() << '\n';
		print_series(rows);
		std::cout << "csv=" << config.csv_path << '\n'
			  << "PIPELINE_STAGE_BENCHMARK_OK\n";
	} catch (const std::exception &error) {
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
