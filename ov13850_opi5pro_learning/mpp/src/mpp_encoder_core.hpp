#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_mpi_cmd.h"
#include "rk_venc_cfg.h"

#include "encoded_packet_sink.hpp"
#include "fixed_slot_pool.hpp"

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
	int packet_buffers = 2;
};

struct EncoderStats {
	std::uint64_t encoded_bytes = 0; // MPP总共输出的字节数
	std::uint64_t packets = 0; // MPP 总共输出的 packet 数量
	std::uint64_t idr_frames = 0; // 实际编码出来的IDR帧的数量
	std::size_t packet_pool_capacity = 0;
	std::size_t packet_pool_in_flight = 0;
	std::size_t packet_pool_peak = 0;
	std::uint64_t packet_pool_misses = 0;
	std::uint64_t packet_pool_recovery_idr_requests = 0;
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

struct MppBufferGroupOwner {
	~MppBufferGroupOwner()
	{
		if (group)
			mpp_buffer_group_put(group);
	}

	MppBufferGroup group = nullptr;
};

struct MppOwnedBufferLease {
	MppOwnedBufferLease(MppBuffer owned_buffer,
			    std::shared_ptr<MppBufferGroupOwner> owned_group)
		: buffer(owned_buffer), group(std::move(owned_group))
	{
	}

	~MppOwnedBufferLease()
	{
		if (buffer)
			mpp_buffer_put(buffer);
	}

	MppBuffer buffer;
	std::shared_ptr<MppBufferGroupOwner> group;
};

class ScopedMppBuffer {
public:
	ScopedMppBuffer(MppBufferGroup group, std::size_t size)
	{
		check_mpp(mpp_buffer_get(group, &buffer_, size),
			  "mpp_buffer_get(packet)");
	}

	~ScopedMppBuffer()
	{
		if (buffer_)
			mpp_buffer_put(buffer_);
	}

	ScopedMppBuffer(const ScopedMppBuffer &) = delete;
	ScopedMppBuffer &operator=(const ScopedMppBuffer &) = delete;

	MppBuffer get() const
	{
		return buffer_;
	}

private:
	MppBuffer buffer_ = nullptr;
};

/**
 * @brief 预分配固定数量MPP输出buffer，并通过lease归还slot。
 */
class MppPacketBufferPool :
	public std::enable_shared_from_this<MppPacketBufferPool> {
public:
	class Lease {
	public:
		Lease(std::shared_ptr<MppPacketBufferPool> pool, std::size_t index)
			: pool_(std::move(pool)), index_(index)
		{
		}

		~Lease()
		{
			pool_->slots_.release(index_);
		}

		Lease(const Lease &) = delete;
		Lease &operator=(const Lease &) = delete;

		MppBuffer buffer() const
		{
			return pool_->buffers_.at(index_);
		}

	private:
		std::shared_ptr<MppPacketBufferPool> pool_;
		std::size_t index_;
	};

	MppPacketBufferPool(std::shared_ptr<MppBufferGroupOwner> group_owner,
			    std::size_t capacity)
		: group_owner_(std::move(group_owner)), slots_(capacity)
	{
		buffers_.reserve(capacity);
		try {
			for (std::size_t index = 0; index < capacity; ++index) {
				MppBuffer buffer = nullptr;
				check_mpp(mpp_buffer_get(group_owner_->group, &buffer,
							 kFrameSize),
					  "mpp_buffer_get(packet pool)");
				buffers_.push_back(buffer);
			}
		} catch (...) {
			for (MppBuffer buffer : buffers_)
				mpp_buffer_put(buffer);
			buffers_.clear();
			throw;
		}
	}

	~MppPacketBufferPool()
	{
		for (MppBuffer buffer : buffers_)
			mpp_buffer_put(buffer);
	}

	MppPacketBufferPool(const MppPacketBufferPool &) = delete;
	MppPacketBufferPool &operator=(const MppPacketBufferPool &) = delete;

	std::shared_ptr<Lease> try_acquire()
	{
		const auto index = slots_.try_acquire();
		if (!index)
			return {};
		try {
			return std::make_shared<Lease>(shared_from_this(), *index);
		} catch (...) {
			slots_.release(*index);
			throw;
		}
	}

	std::size_t capacity() const { return slots_.capacity(); }
	std::size_t in_flight() const { return slots_.in_flight(); }
	std::size_t peak_in_flight() const { return slots_.peak_in_flight(); }
	std::uint64_t acquire_misses() const { return slots_.acquire_misses(); }

private:
	std::shared_ptr<MppBufferGroupOwner> group_owner_;
	FixedSlotPool slots_;
	std::vector<MppBuffer> buffers_;
};

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

	void write_header(EncodedPacketSink &sink, EncoderStats &stats)
	{
		/* Annex-B 裸流必须先写 SPS/PPS 或 VPS/SPS/PPS，独立解码器才能起播。 */
		ScopedMppBuffer output_buffer(group_owner_->group, kFrameSize);
		MppPacket packet = nullptr;
		check_mpp(mpp_packet_init_with_buffer(&packet, output_buffer.get()),
			  "mpp_packet_init_with_buffer(header)");
		mpp_packet_set_length(packet, 0);

		const MPP_RET ret =
			mpi_->control(ctx_, MPP_ENC_GET_HDR_SYNC, packet);
		if (ret != MPP_OK) {
			mpp_packet_deinit(&packet);
			check_mpp(ret, "MPP_ENC_GET_HDR_SYNC");
		}

		try {
			const auto owner = retain_packet_buffer(packet, group_owner_);
			deliver_packet(sink, packet, stats, -1, false, true,
				       owner);
		} catch (...) {
			mpp_packet_deinit(&packet);
			throw;
		}
		mpp_packet_deinit(&packet);
		update_packet_pool_stats(stats);
	}

	void request_idr()
	{
		check_mpp(mpi_->control(ctx_, MPP_ENC_SET_IDR_FRAME, nullptr),
			  "MPP_ENC_SET_IDR_FRAME");
	}

	void update_packet_pool_stats(EncoderStats &stats) const
	{
		if (!packet_pool_)
			return;
		stats.packet_pool_capacity = packet_pool_->capacity();
		stats.packet_pool_in_flight = packet_pool_->in_flight();
		stats.packet_pool_peak = packet_pool_->peak_in_flight();
		stats.packet_pool_misses = packet_pool_->acquire_misses();
	}

	bool encode_frame(int index,
			  bool end_of_stream,
			  EncodedPacketSink &sink,
			  EncoderStats &stats)
	{
		return encode_buffer(frame_buffer_, index, end_of_stream, sink, stats);
	}

	bool encode_external_frame(MppBuffer input_buffer,
				   int index,
				   bool end_of_stream,
				   EncodedPacketSink &sink,
				   EncoderStats &stats)
	{
		/* 外部 MppBuffer 的所有权仍属于调用方，本类只在本次提交中借用。 */
		if (!input_buffer)
			throw std::runtime_error("external MPP buffer is null");
		return encode_buffer(input_buffer, index, end_of_stream, sink, stats);
	}

private:
	bool encode_buffer(MppBuffer input_buffer,
			   int index,
			   bool end_of_stream,
			   EncodedPacketSink &sink,
			   EncoderStats &stats)
	{
		/*
		 * 每个输入帧从固定池借一个预分配输出buffer。GStreamer释放最后一个
		 * 引用后lease归还slot；池耗尽时跳过本帧，禁止覆盖仍在途的数据。
		 */
		auto output_lease = packet_pool_->try_acquire();
		if (!output_lease) {
			request_idr_after_pool_miss_ = true;
			update_packet_pool_stats(stats);
			return false;
		}
		if (request_idr_after_pool_miss_) {
			request_idr();
			request_idr_after_pool_miss_ = false;
			++stats.packet_pool_recovery_idr_requests;
		}

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

		MPP_RET ret = mpp_packet_init_with_buffer(&packet, output_lease->buffer());
		if (ret != MPP_OK) {
			mpp_frame_deinit(&frame);
			check_mpp(ret, "mpp_packet_init_with_buffer(frame)");
		}
		mpp_packet_set_length(packet, 0);
		MppMeta meta = mpp_frame_get_meta(frame);
		ret = mpp_meta_set_packet(meta, KEY_OUTPUT_PACKET, packet);
		if (ret != MPP_OK) {
			mpp_frame_deinit(&frame);
			mpp_packet_deinit(&packet);
			check_mpp(ret, "mpp_meta_set_packet");
		}

		ret = mpi_->encode_put_frame(ctx_, frame);
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

		const bool eos = mpp_packet_get_eos(packet);
		try {
			deliver_packet(sink, packet, stats,
				       static_cast<std::int64_t>(index) * 1000000 / kFps,
				       is_intra, false, output_lease);
			++stats.packets;
		} catch (...) {
			mpp_packet_deinit(&packet);
			throw;
		}
		mpp_packet_deinit(&packet);
		update_packet_pool_stats(stats);
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

	static void deliver_packet(EncodedPacketSink &sink,
				   MppPacket packet,
				   EncoderStats &stats,
				   std::int64_t pts_us,
				   bool keyframe,
				   bool codec_config,
				   std::shared_ptr<const void> owner)
	{
		const std::size_t length = mpp_packet_get_length(packet);
		if (!length)
			return;

		void *position = mpp_packet_get_pos(packet);
		if (!position)
			throw std::runtime_error("packet has no data pointer");

		const EncodedPacketView view = {
			reinterpret_cast<const std::uint8_t *>(position),
			length,
			pts_us,
			keyframe,
			codec_config,
			static_cast<bool>(mpp_packet_get_eos(packet)),
			std::move(owner),
		};
		sink.consume(view);
		stats.encoded_bytes += length;
	}

	static std::shared_ptr<const void> retain_packet_buffer(
		MppPacket packet,
		const std::shared_ptr<MppBufferGroupOwner> &group_owner)
	{
		MppBuffer packet_buffer = mpp_packet_get_buffer(packet);
		if (!packet_buffer)
			throw std::runtime_error("packet has no MPP buffer");
		check_mpp(mpp_buffer_inc_ref(packet_buffer),
			  "mpp_buffer_inc_ref(packet)");
		try {
			return std::make_shared<MppOwnedBufferLease>(packet_buffer,
							       group_owner);
		} catch (...) {
			mpp_buffer_put(packet_buffer);
			throw;
		}
	}

	void initialize()
	{
		if (config_.packet_buffers < 1 || config_.packet_buffers > 64)
			throw std::invalid_argument("packet buffer count must be in 1..64");
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

		group_owner_ = std::make_shared<MppBufferGroupOwner>();
		check_mpp(mpp_buffer_group_get_internal(&group_owner_->group,
						MPP_BUFFER_TYPE_DRM),
			  "mpp_buffer_group_get_internal");
		check_mpp(mpp_buffer_get(group_owner_->group, &frame_buffer_, kFrameSize),
			  "mpp_buffer_get(frame)");
		packet_pool_ = std::make_shared<MppPacketBufferPool>(
			group_owner_, static_cast<std::size_t>(config_.packet_buffers));
	}

	void set_s32(const char *name, RK_S32 value)
	{
		check_mpp(mpp_enc_cfg_set_s32(cfg_, name, value), name);
	}

	void cleanup() noexcept
	{
		packet_pool_.reset();
		if (frame_buffer_)
			mpp_buffer_put(frame_buffer_);
		frame_buffer_ = nullptr;
		group_owner_.reset();

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
	std::shared_ptr<MppBufferGroupOwner> group_owner_;
	std::shared_ptr<MppPacketBufferPool> packet_pool_;
	MppBuffer frame_buffer_ = nullptr;
	bool request_idr_after_pool_miss_ = false;
};

} // namespace camera_mpp
