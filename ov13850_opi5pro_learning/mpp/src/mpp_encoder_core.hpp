#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <string>

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_mpi_cmd.h"
#include "rk_venc_cfg.h"

namespace camera_mpp {

/*
 * Stage 4 的公共编码核心。
 *
 * 两个前端共享这里的编码策略：文件前端把紧凑 NV12 复制到 MPP 内部缓冲区，
 * 实时前端既可走相同 copy 路径，也可把 V4L2 导出的 DMA-BUF 直接交给 MPP。
 * 这样可以只改变输入缓冲区来源，在完全相同的码控、GOP 和 packet 流程下比较
 * copy 与 DMA-BUF，避免把不同编码参数误判成零拷贝带来的差异。
 */

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
	int ver_stride = kVerStride;
};

struct EncoderStats {
	std::uint64_t encoded_bytes = 0;
	std::uint64_t packets = 0;
	std::uint64_t idr_frames = 0;
};

inline const char *codec_name(Codec codec)
{
	return codec == Codec::H264 ? "h264" : "h265";
}

inline const char *rc_name(RateControl rc)
{
	return rc == RateControl::Cbr ? "cbr" : "vbr";
}

inline void check_mpp(MPP_RET ret, const char *operation)
{
	if (ret != MPP_OK)
		throw std::runtime_error(std::string(operation) +
					" failed, MPP_RET=" + std::to_string(ret));
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

	/*
	 * 文件和 copy 路径收到的是 1920x1080 紧凑 NV12；MPP 内部缓冲区按
	 * 1920x1088 分配。逐行复制会把 UV 起点从 1080 行移动到 1088 行，
	 * 其余对齐区清零，防止编码器把未初始化 padding 当成图像数据。
	 */
	void load_nv12(const unsigned char *source, std::size_t size)
	{
		if (!source || size != kInputSize)
			throw std::runtime_error("NV12 input must be exactly 3110400 bytes");

		void *ptr = mpp_buffer_get_ptr(frame_buffer_);
		if (!ptr)
			throw std::runtime_error("MPP frame buffer has no CPU address");

		check_mpp(mpp_buffer_sync_begin(frame_buffer_),
			  "mpp_buffer_sync_begin");
		copy_nv12_to_strided(ptr, source, config_.ver_stride);
		check_mpp(mpp_buffer_sync_end(frame_buffer_),
			  "mpp_buffer_sync_end");
	}

	void write_header(std::ostream &output, EncoderStats &stats)
	{
		/* Annex-B 裸流必须先写 SPS/PPS 或 VPS/SPS/PPS，独立解码器才能起播。 */
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

		write_packet(output, packet, stats);
		mpp_packet_deinit(&packet);
	}

	void request_idr()
	{
		check_mpp(mpi_->control(ctx_, MPP_ENC_SET_IDR_FRAME, nullptr),
			  "MPP_ENC_SET_IDR_FRAME");
	}

	bool encode_frame(int index,
			  bool end_of_stream,
			  std::ostream &output,
			  EncoderStats &stats)
	{
		return encode_buffer(frame_buffer_, index, end_of_stream, output, stats);
	}

	bool encode_external_frame(MppBuffer input_buffer,
				   int index,
				   bool end_of_stream,
				   std::ostream &output,
				   EncoderStats &stats)
	{
		/* 外部 MppBuffer 的所有权仍属于调用方，本类只在本次提交中借用。 */
		if (!input_buffer)
			throw std::runtime_error("external MPP buffer is null");
		return encode_buffer(input_buffer, index, end_of_stream, output, stats);
	}

private:
	bool encode_buffer(MppBuffer input_buffer,
			   int index,
			  bool end_of_stream,
			  std::ostream &output,
			  EncoderStats &stats)
	{
		/*
		 * 每个输入帧创建一个轻量 MppFrame 描述符，并复用预分配的 packet
		 * 缓冲区。阻塞输出模式保证返回前硬件已经消费当前输入，因此实时
		 * DMA-BUF 路径可以在此函数返回后安全地把 V4L2 buffer 重新 QBUF。
		 */
		MppFrame frame = nullptr;
		MppPacket packet = nullptr;
		check_mpp(mpp_frame_init(&frame), "mpp_frame_init");

		mpp_frame_set_width(frame, kWidth);
		mpp_frame_set_height(frame, kHeight);
		mpp_frame_set_hor_stride(frame, kHorStride);
		mpp_frame_set_ver_stride(frame, config_.ver_stride);
		mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
		mpp_frame_set_pts(frame,
			static_cast<RK_S64>(index) * 1000000 / kFps);
		mpp_frame_set_eos(frame, end_of_stream);
		mpp_frame_set_buffer(frame, input_buffer);

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
			++stats.idr_frames;

		write_packet(output, packet, stats);
		++stats.packets;
		const bool eos = mpp_packet_get_eos(packet);
		mpp_packet_deinit(&packet);
		return eos;
	}

private:
	static void copy_nv12_to_strided(void *destination,
					const unsigned char *source,
					int ver_stride)
	{
		auto *dst = static_cast<unsigned char *>(destination);
		std::memset(dst, 0, kFrameSize);

		for (int row = 0; row < kHeight; ++row)
			std::memcpy(dst + static_cast<std::size_t>(row) * kHorStride,
				    source + static_cast<std::size_t>(row) * kWidth,
				    kWidth);

		const std::size_t src_uv_offset =
			static_cast<std::size_t>(kWidth) * kHeight;
		const std::size_t dst_uv_offset =
			static_cast<std::size_t>(kHorStride) * ver_stride;

		for (int row = 0; row < kHeight / 2; ++row)
			std::memcpy(dst + dst_uv_offset +
					    static_cast<std::size_t>(row) * kHorStride,
				    source + src_uv_offset +
					    static_cast<std::size_t>(row) * kWidth,
				    kWidth);
	}

	static void write_packet(std::ostream &output,
				 MppPacket packet,
				 EncoderStats &stats)
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
		stats.encoded_bytes += length;
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

		/* prep 描述内存布局；它必须与传入缓冲区的真实 UV 偏移完全一致。 */
		set_s32("prep:width", kWidth);
		set_s32("prep:height", kHeight);
		set_s32("prep:hor_stride", kHorStride);
		set_s32("prep:ver_stride", config_.ver_stride);
		set_s32("prep:format", MPP_FMT_YUV420SP);

		/* rc 参数固定输入/输出为 30 fps，码率与 GOP 由前端参数化。 */
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

		/* 每个 IDR 重复参数集，便于 Stage 5 的接收端从任意关键帧恢复。 */
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

	EncoderConfig config_;
	MppCtx ctx_ = nullptr;
	MppApi *mpi_ = nullptr;
	MppEncCfg cfg_ = nullptr;
	MppBufferGroup group_ = nullptr;
	MppBuffer frame_buffer_ = nullptr;
	MppBuffer packet_buffer_ = nullptr;
};

} // namespace camera_mpp
