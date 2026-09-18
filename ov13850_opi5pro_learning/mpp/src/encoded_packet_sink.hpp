#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <stdexcept>

namespace camera_mpp {

/*
 * A view of one encoded output packet. Without owner the producer retains the
 * storage and a sink must finish consuming data before consume() returns. An
 * owner keeps externally managed storage alive for asynchronous sinks.
 */
struct EncodedPacketView {
	const std::uint8_t *data;
	std::size_t size;
	std::int64_t pts_us;
	bool keyframe;
	bool codec_config;
	bool eos;
	std::shared_ptr<const void> owner = {};
};

class EncodedPacketSink {
public:
	virtual ~EncodedPacketSink() = default;
	virtual void consume(const EncodedPacketView &packet) = 0;
};

/* Preserve the Stage 4 elementary-stream output while sharing packet delivery. */
class OstreamPacketSink final : public EncodedPacketSink {
public:
	explicit OstreamPacketSink(std::ostream &output)
		: output_(output)
	{
	}

	void consume(const EncodedPacketView &packet) override
	{
		if (!packet.size)
			return;
		if (!packet.data)
			throw std::invalid_argument("nonempty packet has no data");

		output_.write(reinterpret_cast<const char *>(packet.data),
			      static_cast<std::streamsize>(packet.size));
		if (!output_)
			throw std::runtime_error("failed to write encoded packet");
	}

private:
	std::ostream &output_;
};

} // namespace camera_mpp
