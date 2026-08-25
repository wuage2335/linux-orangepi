#include <iostream>

#include <gst/gst.h>

#include "rk_mpi.h"

int main(int argc, char **argv)
{
	gst_init(&argc, &argv);

	MppCtx context = nullptr;
	MppApi *api = nullptr;
	const MPP_RET ret = mpp_create(&context, &api);
	if (ret != MPP_OK || !context || !api) {
		std::cerr << "FAIL: mpp_create returned " << ret << '\n';
		return 1;
	}

	mpp_destroy(context);
	std::cout << "gstreamer=" << gst_version_string() << '\n';
	std::cout << "STREAMING_NATIVE_BUILD_OK\n";
	return 0;
}
