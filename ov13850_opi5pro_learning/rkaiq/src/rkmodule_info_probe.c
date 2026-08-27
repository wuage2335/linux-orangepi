#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/videodev2.h>
#include <linux/rk-camera-module.h>

int main(int argc, char **argv)
{
	struct rkmodule_inf info;
	const char *device;
	unsigned long request = RKMODULE_GET_MODULE_INFO;
	int fd;

	if (argc == 3 && !strcmp(argv[1], "--sign-extended")) {
		request = (unsigned long)(long)(int)(unsigned int)request;
		device = argv[2];
	} else if (argc == 2) {
		device = argv[1];
	} else {
		fprintf(stderr,
			"usage: %s [--sign-extended] </dev/v4l-subdevN>\n",
			argv[0]);
		return 2;
	}
	fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "ERROR: open %s: %s\n", device, strerror(errno));
		return 1;
	}

	memset(&info, 0, sizeof(info));
	if (ioctl(fd, request, &info) < 0) {
		fprintf(stderr, "ERROR: RKMODULE_GET_MODULE_INFO: %s\n",
			strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);

	if (!info.base.sensor[0] || !info.base.module[0] || !info.base.lens[0]) {
		fprintf(stderr, "ERROR: module-info contains an empty base field\n");
		return 1;
	}
	printf("sensor=%s\nmodule=%s\nlens=%s\n",
		info.base.sensor, info.base.module, info.base.lens);
	return 0;
}
