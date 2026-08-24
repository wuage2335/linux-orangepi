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
#include <vector>

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_mpi_cmd.h"
#include "rk_venc_cfg.h"

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kHorStride = 1920;
constexpr int kVerStride = 1088;
constexpr int kFps = 30;
constexpr std::size_t kInputSize =
	static_cast<std::size_t>(kWidth) * kHeight * 3 / 2;
constexpr std::size_t kFrameSize =
	static_cast<std::size_t>(kHorStride) * kVerStride * 3 / 2;

enum class Codec {
	H264,
	H265,
};

enum class RateControl {
	Cbr,
	Vbr,
};

struct EncoderConfig {
	Codec codec = Codec::H264;
	RateControl rc = RateControl::Cbr;
	int bitrate = 8000000;
	int gop = 60;
	int frames = 300;
	int request_idr = -1;
};

const char *codec_name(Codec codec)
{
	return codec == Codec::H264 ? "h264" : "h265";
}

const char *rc_name(RateControl rc)
{
	return rc == RateControl::Cbr ? "cbr" : "vbr";
}

int parse_positive(const char *text, const char *option)
{
	const int value = std::stoi(text);
	if (value <= 0)
		throw std::runtime_error(std::string(option) + " must be positive");
	return value;
}

struct CommandLine {
	EncoderConfig config;
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
			command.config.frames = parse_positive(value, "--frames");
		} else if (option == "--request-idr") {
			command.config.request_idr = std::stoi(value);
			if (command.config.request_idr < 0)
				throw std::runtime_error("--request-idr must be non-negative");
		} else {
			throw std::runtime_error("unknown option: " + option);
		}
	}

	if (argc - index != 2)
		throw std::runtime_error("expected input and output paths");

	command.input = argv[index];
	command.output = argv[index + 1];
	if (command.config.request_idr >= command.config.frames)
		throw std::runtime_error("--request-idr must be smaller than --frames");

	return command;
}

void check_mpp(MPP_RET ret, const char *operation)
{
	if (ret != MPP_OK)
		throw std::runtime_error(std::string(operation) +
					" failed, MPP_RET=" + std::to_string(ret));
}

std::vector<unsigned char> read_input(const char *path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("cannot open input NV12 file");

	const std::streamoff size = input.tellg();
	if (size != static_cast<std::streamoff>(kInputSize))
		throw std::runtime_error("input size must be 3110400 bytes");

	std::vector<unsigned char> data(kInputSize);
	input.seekg(0, std::ios::beg);
	input.read(reinterpret_cast<char *>(data.data()),
		   static_cast<std::streamsize>(data.size()));
	if (!input)
		throw std::runtime_error("failed to read complete NV12 frame");

	return data;
}

void copy_nv12_to_strided(void *destination,
			  const std::vector<unsigned char> &source)
{
	auto *dst = static_cast<unsigned char *>(destination);
	std::memset(dst, 0, kFrameSize);

	for (int row = 0; row < kHeight; ++row)
		std::memcpy(dst + static_cast<std::size_t>(row) * kHorStride,
			    source.data() + static_cast<std::size_t>(row) * kWidth,
			    kWidth);

	const std::size_t src_uv_offset =
		static_cast<std::size_t>(kWidth) * kHeight;
	const std::size_t dst_uv_offset =
		static_cast<std::size_t>(kHorStride) * kVerStride;

	for (int row = 0; row < kHeight / 2; ++row)
		std::memcpy(dst + dst_uv_offset +
				    static_cast<std::size_t>(row) * kHorStride,
			    source.data() + src_uv_offset +
				    static_cast<std::size_t>(row) * kWidth,
			    kWidth);
}

class MppEncoder {
public:
	explicit MppEncoder(const EncoderConfig &config)
		: config_(config)
	{
		try {
			initialize();
		} catch (...) {
			cleanup();
			throw;
		}
	}

	~MppEncoder()
	{
		cleanup();
	}

	MppEncoder(const MppEncoder &) = delete;
	MppEncoder &operator=(const MppEncoder &) = delete;

	void load_frame(const std::vector<unsigned char> &input)
	{
		void *ptr = mpp_buffer_get_ptr(frame_buffer_);
		if (!ptr)
			throw std::runtime_error("MPP frame buffer has no CPU address");

		check_mpp(mpp_buffer_sync_begin(frame_buffer_),
			  "mpp_buffer_sync_begin");
		copy_nv12_to_strided(ptr, input);
		check_mpp(mpp_buffer_sync_end(frame_buffer_),
			  "mpp_buffer_sync_end");
	}

	void write_header(std::ofstream &output,
			  std::uint64_t &encoded_bytes)
	{
		MppPacket packet = nullptr;
		check_mpp(mpp_packet_init_with_buffer(&packet, packet_buffer_),
			  "mpp_packet_init_with_buffer(header)");
		mpp_packet_set_length(packet, 0);

		const MPP_RET ret =
			mpi_->control(ctx_, MPP_ENC_GET_HDR_SYNC, packet);
		if (ret != MPP_OK) {
			mpp_packet_deinit(&packet);
			check_mpp(ret, "MPP_ENC_GET_HDR_SYNC");
		}

		write_packet(output, packet, encoded_bytes);
		mpp_packet_deinit(&packet);
	}

	void request_idr()
	{
		check_mpp(mpi_->control(ctx_, MPP_ENC_SET_IDR_FRAME, nullptr),
			  "MPP_ENC_SET_IDR_FRAME");
	}

	bool encode_frame(int index,
			  bool end_of_stream,
			  std::ofstream &output,
			  std::uint64_t &encoded_bytes,
			  std::uint64_t &packet_count,
			  std::uint64_t &idr_count)
	{
		MppFrame frame = nullptr;
		MppPacket packet = nullptr;
		check_mpp(mpp_frame_init(&frame), "mpp_frame_init");

		mpp_frame_set_width(frame, kWidth);
		mpp_frame_set_height(frame, kHeight);
		mpp_frame_set_hor_stride(frame, kHorStride);
		mpp_frame_set_ver_stride(frame, kVerStride);
		mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
		mpp_frame_set_pts(frame,
			static_cast<RK_S64>(index) * 1000000 / kFps);
		mpp_frame_set_eos(frame, end_of_stream);
		mpp_frame_set_buffer(frame, frame_buffer_);

		check_mpp(mpp_packet_init_with_buffer(&packet, packet_buffer_),
			  "mpp_packet_init_with_buffer(frame)");
		mpp_packet_set_length(packet, 0);
		MppMeta meta = mpp_frame_get_meta(frame);
		check_mpp(mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet),
			  "mpp_meta_set_packet");

		MPP_RET ret = mpi_->encode_put_frame(ctx_, frame);
		mpp_frame_deinit(&frame);
		if (ret != MPP_OK) {
			mpp_packet_deinit(&packet);
			check_mpp(ret, "encode_put_frame");
		}

		ret = mpi_->encode_get_packet(ctx_, &packet);
		if (ret != MPP_OK) {
			mpp_packet_deinit(&packet);
			check_mpp(ret, "encode_get_packet");
		}
		if (!packet)
			throw std::runtime_error("encoder returned a null packet");

		MppMeta packet_meta = mpp_packet_get_meta(packet);
		RK_S32 is_intra = 0;
		if (packet_meta &&
		    mpp_meta_get_s32(packet_meta, KEY_OUTPUT_INTRA, &is_intra) == MPP_OK &&
		    is_intra)
			++idr_count;

		write_packet(output, packet, encoded_bytes);
		++packet_count;
		const bool eos = mpp_packet_get_eos(packet);
		mpp_packet_deinit(&packet);
		return eos;
	}

private:
	static void write_packet(std::ofstream &output,
				 MppPacket packet,
				 std::uint64_t &encoded_bytes)
	{
		const std::size_t length = mpp_packet_get_length(packet);
		if (!length)
			return;

		void *position = mpp_packet_get_pos(packet);
		if (!position)
			throw std::runtime_error("packet has no data pointer");

		output.write(static_cast<const char *>(position),
			     static_cast<std::streamsize>(length));
		if (!output)
			throw std::runtime_error("failed to write encoded packet");
		encoded_bytes += length;
	}

	void initialize()
	{
		const MppCodingType coding = config_.codec == Codec::H264 ?
			MPP_VIDEO_CodingAVC : MPP_VIDEO_CodingHEVC;

		check_mpp(mpp_create(&ctx_, &mpi_), "mpp_create");
		check_mpp(mpp_init(ctx_, MPP_CTX_ENC, coding), "mpp_init(encoder)");

		MppPollType timeout = MPP_POLL_BLOCK;
		check_mpp(mpi_->control(ctx_, MPP_SET_OUTPUT_TIMEOUT, &timeout),
			  "MPP_SET_OUTPUT_TIMEOUT");

		check_mpp(mpp_enc_cfg_init(&cfg_), "mpp_enc_cfg_init");
		check_mpp(mpi_->control(ctx_, MPP_ENC_GET_CFG, cfg_),
			  "MPP_ENC_GET_CFG");

		set_s32("prep:width", kWidth);
		set_s32("prep:height", kHeight);
		set_s32("prep:hor_stride", kHorStride);
		set_s32("prep:ver_stride", kVerStride);
		set_s32("prep:format", MPP_FMT_YUV420SP);

		set_s32("rc:mode", config_.rc == RateControl::Cbr ?
			MPP_ENC_RC_MODE_CBR : MPP_ENC_RC_MODE_VBR);
		set_s32("rc:fps_in_flex", 0);
		set_s32("rc:fps_in_num", kFps);
		set_s32("rc:fps_in_denom", 1);
		set_s32("rc:fps_out_flex", 0);
		set_s32("rc:fps_out_num", kFps);
		set_s32("rc:fps_out_denom", 1);
		set_s32("rc:bps_target", config_.bitrate);
		set_s32("rc:bps_max", config_.bitrate * 17 / 16);
		set_s32("rc:bps_min", config_.rc == RateControl::Cbr ?
			config_.bitrate * 15 / 16 : config_.bitrate / 16);
		set_s32("rc:gop", config_.gop);
		set_s32("rc:qp_init", -1);
		set_s32("rc:qp_max", 51);
		set_s32("rc:qp_min", 10);
		set_s32("rc:qp_max_i", 51);
		set_s32("rc:qp_min_i", 10);
		set_s32("rc:qp_ip", 2);

		if (config_.codec == Codec::H264) {
			set_s32("h264:profile", 100);
			set_s32("h264:level", 40);
			set_s32("h264:cabac_en", 1);
			set_s32("h264:cabac_idc", 0);
			set_s32("h264:trans8x8", 1);
			set_s32("h264:vui_en", 1);
		} else {
			set_s32("h265:diff_cu_qp_delta_depth", 0);
			set_s32("h265:vui_en", 1);
		}

		check_mpp(mpi_->control(ctx_, MPP_ENC_SET_CFG, cfg_),
			  "MPP_ENC_SET_CFG");

		MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
		check_mpp(mpi_->control(ctx_, MPP_ENC_SET_HEADER_MODE, &header_mode),
			  "MPP_ENC_SET_HEADER_MODE");

		check_mpp(mpp_buffer_group_get_internal(&group_, MPP_BUFFER_TYPE_DRM),
			  "mpp_buffer_group_get_internal");
		check_mpp(mpp_buffer_get(group_, &frame_buffer_, kFrameSize),
			  "mpp_buffer_get(frame)");
		check_mpp(mpp_buffer_get(group_, &packet_buffer_, kFrameSize),
			  "mpp_buffer_get(packet)");
	}

	void set_s32(const char *name, RK_S32 value)
	{
		check_mpp(mpp_enc_cfg_set_s32(cfg_, name, value), name);
	}

	void cleanup() noexcept
	{
		if (frame_buffer_)
			mpp_buffer_put(frame_buffer_);
		if (packet_buffer_)
			mpp_buffer_put(packet_buffer_);
		frame_buffer_ = nullptr;
		packet_buffer_ = nullptr;

		if (group_)
			mpp_buffer_group_put(group_);
		group_ = nullptr;

		if (cfg_)
			mpp_enc_cfg_deinit(cfg_);
		cfg_ = nullptr;

		if (ctx_) {
			if (mpi_)
				mpi_->reset(ctx_);
			mpp_destroy(ctx_);
		}
		ctx_ = nullptr;
		mpi_ = nullptr;
	}

	MppCtx ctx_ = nullptr;
	MppApi *mpi_ = nullptr;
	MppEncCfg cfg_ = nullptr;
	MppBufferGroup group_ = nullptr;
	MppBuffer frame_buffer_ = nullptr;
	MppBuffer packet_buffer_ = nullptr;
	EncoderConfig config_;
};

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
		encoder.load_frame(input);

		std::uint64_t encoded_bytes = 0;
		std::uint64_t packets = 0;
		std::uint64_t idr_frames = 0;
		encoder.write_header(output, encoded_bytes);

		const auto start = std::chrono::steady_clock::now();
		int frames_out = 0;
		for (int index = 0; index < command.config.frames; ++index) {
			if (index == command.config.request_idr)
				encoder.request_idr();
			const bool eos = encoder.encode_frame(
				index, index == command.config.frames - 1,
				output, encoded_bytes, packets, idr_frames);
			++frames_out;
			if (index == command.config.frames - 1 && !eos)
				throw std::runtime_error("last packet did not carry EOS");
		}
		const auto end = std::chrono::steady_clock::now();

		output.close();
		if (!output)
			throw std::runtime_error("failed to finalize H.264 file");

		const double elapsed =
			std::chrono::duration<double>(end - start).count();
		const double encode_fps = elapsed > 0.0 ?
			command.config.frames / elapsed : 0.0;

		std::cout << "codec=" << codec_name(command.config.codec)
			  << " rc=" << rc_name(command.config.rc)
			  << " width=" << kWidth
			  << " height=" << kHeight
			  << " fps=" << kFps
			  << " bitrate=" << command.config.bitrate
			  << " gop=" << command.config.gop << '\n';
		std::cout << "hor_stride=" << kHorStride
			  << " ver_stride=" << kVerStride
			  << " frame_buffer_bytes=" << kFrameSize << '\n';
		std::cout << "frames_in=" << command.config.frames
			  << " frames_out=" << frames_out
			  << " packets=" << packets
			  << " idr_frames=" << idr_frames
			  << " encoded_bytes=" << encoded_bytes << '\n';
		std::cout << std::fixed << std::setprecision(2)
			  << "elapsed_s=" << elapsed
			  << " encode_fps=" << encode_fps << '\n';
		std::cout << "MPP_FILE_ENCODE_OK\n";
	} catch (const std::exception &error) {
		std::remove(command.output);
		std::cerr << "ERROR: " << error.what() << '\n';
		return 1;
	}

	return 0;
}
