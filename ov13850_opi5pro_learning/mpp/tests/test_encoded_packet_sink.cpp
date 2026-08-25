#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "../src/encoded_packet_sink.hpp"

namespace {

using camera_mpp::EncodedPacketView;
using camera_mpp::OstreamPacketSink;

void require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void test_writes_binary_bytes_without_changing_metadata()
{
	const std::uint8_t bytes[] = {0x00, 0x00, 0x01, 0x67, 0x00, 0xff};
	const EncodedPacketView packet = {
		bytes, sizeof(bytes), 33333, true, false, false,
	};
	std::ostringstream output(std::ios::binary);
	OstreamPacketSink sink(output);

	sink.consume(packet);

	const std::string result = output.str();
	require(result.size() == sizeof(bytes), "binary packet size changed");
	require(std::memcmp(result.data(), bytes, sizeof(bytes)) == 0,
		"binary packet bytes changed");
	require(packet.pts_us == 33333, "packet PTS changed");
	require(packet.keyframe, "packet keyframe flag changed");
	require(!packet.codec_config, "packet config flag changed");
}

void test_accepts_empty_packet()
{
	const EncodedPacketView packet = {
		nullptr, 0, -1, false, true, false,
	};
	std::ostringstream output(std::ios::binary);
	OstreamPacketSink sink(output);

	sink.consume(packet);
	require(output.str().empty(), "empty packet wrote data");
}

void test_rejects_null_nonempty_packet()
{
	const EncodedPacketView packet = {
		nullptr, 1, 0, false, false, false,
	};
	std::ostringstream output(std::ios::binary);
	OstreamPacketSink sink(output);

	try {
		sink.consume(packet);
	} catch (const std::invalid_argument &) {
		return;
	}
	throw std::runtime_error("null nonempty packet was accepted");
}

} // namespace

int main()
{
	try {
		test_writes_binary_bytes_without_changing_metadata();
		test_accepts_empty_packet();
		test_rejects_null_nonempty_packet();
		std::cout << "PASS: encoded packet sink\n";
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
