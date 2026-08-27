#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <linux/rk-camera-module.h>

typedef int (*real_ioctl_fn)(int, unsigned long, ...);

int ioctl(int fd, unsigned long request, ...)
{
	static real_ioctl_fn real_ioctl;
	void *arg;
	va_list ap;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (getenv("RKAIQ_MODULE_INFO_SHIM_TRACE") &&
	    !strcmp(getenv("RKAIQ_MODULE_INFO_SHIM_TRACE"), "1"))
		fprintf(stderr, "RKAIQ_SHIM request=0x%lx expected=0x%lx\n",
			request, (unsigned long)RKMODULE_GET_MODULE_INFO);

	if ((unsigned int)request ==
	    (unsigned int)RKMODULE_GET_MODULE_INFO &&
	    getenv("RKAIQ_MODULE_INFO_SHIM") &&
	    !strcmp(getenv("RKAIQ_MODULE_INFO_SHIM"), "1")) {
		struct rkmodule_inf *info = arg;

		if (!info) {
			errno = EFAULT;
			return -1;
		}
		memset(info, 0, sizeof(*info));
		strncpy(info->base.sensor, "ov13850",
			sizeof(info->base.sensor) - 1);
		strncpy(info->base.module, "CMK-CT0116",
			sizeof(info->base.module) - 1);
		strncpy(info->base.lens, "default",
			sizeof(info->base.lens) - 1);
		return 0;
	}

	if (!real_ioctl) {
		real_ioctl = (real_ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
		if (!real_ioctl) {
			errno = ENOSYS;
			return -1;
		}
	}
	return real_ioctl(fd, request, arg);
}
