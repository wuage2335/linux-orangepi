#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "im2d.hpp"

namespace {

constexpr int kSrcWidth = 1920;
constexpr int kSrcHeight = 1080;
constexpr int kDstWidth = 1280;
constexpr int kDstHeight = 720;

constexpr int kWarmups = 5;
constexpr int kIterations = 100;

constexpr int kFormat = RK_FORMAT_YCbCr_420_SP;

constexpr std::size_t kSrcSize =
	static_cast<std::size_t>(kSrcWidth) * kSrcHeight * 3 / 2;

constexpr std::size_t kDstSize =
	static_cast<std::size_t>(kDstWidth) * kDstHeight * 3 / 2;

static_assert(kSrcWidth % 2 == 0);
static_assert(kSrcHeight % 2 == 0);
static_assert(kDstWidth % 2 == 0);
static_assert(kDstHeight % 2 == 0);

class ImportedBuffer {
public:
	ImportedBuffer(void *address, std::size_t size)
		: handle_(importbuffer_virtualaddr(
			  address, static_cast<int>(size)))
	{
	}

	~ImportedBuffer()
	{
		if (handle_ != 0)
			releasebuffer_handle(handle_);
	}

	ImportedBuffer(const ImportedBuffer &) = delete;
	ImportedBuffer &operator=(const ImportedBuffer &) = delete;

	bool valid() const
	{
		return handle_ != 0;
	}

	rga_buffer_handle_t get() const
	{
		return handle_;
	}

private:
	rga_buffer_handle_t handle_ = 0;
};

bool read_exact_frame(const char *path,
		      std::vector<unsigned char> &buffer)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);

	if (!input) {
		std::cerr << "ERROR: cannot open input: "
			  << path << '\n';
		return false;
	}

	const std::streamoff size = input.tellg();

	if (size != static_cast<std::streamoff>(buffer.size())) {
		std::cerr << "ERROR: input size=" << size
			  << " expected=" << buffer.size() << '\n';
		return false;
	}

	input.seekg(0, std::ios::beg);
	input.read(reinterpret_cast<char *>(buffer.data()),
		   static_cast<std::streamsize>(buffer.size()));

	if (!input) {
		std::cerr << "ERROR: failed to read complete input frame\n";
		return false;
	}

	return true;
}

bool write_exact_frame(const char *path,
		       const std::vector<unsigned char> &buffer)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);

	if (!output) {
		std::cerr << "ERROR: cannot create output: "
			  << path << '\n';
		return false;
	}

	output.write(reinterpret_cast<const char *>(buffer.data()),
		     static_cast<std::streamsize>(buffer.size()));
	output.close();

	if (!output) {
		std::cerr << "ERROR: failed to write complete output frame\n";
		std::remove(path);
		return false;
	}

	return true;
}

bool resize_once(const rga_buffer_t &src, rga_buffer_t &dst)
{
	const IM_STATUS status = imresize(src, dst);

	if (status != IM_STATUS_SUCCESS) {
		std::cerr << "ERROR: imresize failed: "
			  << imStrError(status) << '\n';
		return false;
	}

	return true;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 3) {
		std::cerr
			<< "ERROR: usage: "
			<< argv[0]
			<< " <input-1920x1080.nv12>"
			<< " <output-1280x720.nv12>\n";
		return 2;
	}

	if (std::strcmp(argv[1], argv[2]) == 0) {
		std::cerr
			<< "ERROR: input and output paths must differ\n";
		return 2;
	}

	std::remove(argv[2]);

	try {
		std::vector<unsigned char> src_data(kSrcSize);
		std::vector<unsigned char> dst_data(kDstSize, 0);

		if (!read_exact_frame(argv[1], src_data))
			return 3;

		ImportedBuffer src_handle(
			src_data.data(), src_data.size());
		ImportedBuffer dst_handle(
			dst_data.data(), dst_data.size());

		if (!src_handle.valid() || !dst_handle.valid()) {
			std::cerr
				<< "ERROR: importbuffer_virtualaddr failed\n";
			return 4;
		}

		rga_buffer_t src = wrapbuffer_handle(
			src_handle.get(), kSrcWidth, kSrcHeight, kFormat);
		rga_buffer_t dst = wrapbuffer_handle(
			dst_handle.get(), kDstWidth, kDstHeight, kFormat);

		im_rect empty = {};
		const IM_STATUS check = imcheck(src, dst, empty, empty);

		if (check != IM_STATUS_NOERROR) {
			std::cerr << "ERROR: imcheck failed: "
				  << imStrError(check) << '\n';
			return 5;
		}

		const char *version = querystring(RGA_VERSION);

		std::cout
			<< "librga="
			<< (version != nullptr ? version : "unknown")
			<< '\n';
		std::cout << "input=1920x1080 NV12 bytes="
			  << kSrcSize << '\n';
		std::cout << "output=1280x720 NV12 bytes="
			  << kDstSize << '\n';
		std::cout << "warmups=" << kWarmups
			  << " iterations=" << kIterations << '\n';

		for (int i = 0; i < kWarmups; ++i) {
			if (!resize_once(src, dst))
				return 6;
		}

		const auto start = std::chrono::steady_clock::now();

		for (int i = 0; i < kIterations; ++i) {
			if (!resize_once(src, dst))
				return 6;
		}

		const auto end = std::chrono::steady_clock::now();

		if (!write_exact_frame(argv[2], dst_data))
			return 7;

		const double total_us =
			std::chrono::duration<double, std::micro>(
				end - start).count();
		const double average_us = total_us / kIterations;
		const double operations_per_second =
			average_us > 0.0 ? 1000000.0 / average_us : 0.0;

		std::cout << std::fixed << std::setprecision(2)
			  << "total_us=" << total_us
			  << " average_us=" << average_us
			  << " operations_per_second="
			  << operations_per_second << '\n';
		std::cout << "RGA_RESIZE_OK\n";
	} catch (const std::exception &error) {
		std::cerr << "ERROR: exception: "
			  << error.what() << '\n';
		return 8;
	}

	return 0;
}
