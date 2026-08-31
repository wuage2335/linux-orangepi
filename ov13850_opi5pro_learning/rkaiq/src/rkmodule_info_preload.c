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

/*
 * 这是旧活动内核的受控兼容垫片，不是正式驱动替代品。通过 LD_PRELOAD，它先
 * 收到进程发出的 ioctl；只有同时满足以下条件时才返回项目已验证的模组信息：
 *
 *   1. 命令是 RKMODULE_GET_MODULE_INFO；
 *   2. RKAIQ_MODULE_INFO_SHIM=1 被显式设置。
 *
 * 其他 ioctl 必须转发给 libc 的真实实现。这样可以在不替换活动内核的情况下
 * 验证 RKAIQ，其影响范围只限于启动时加载了本 so 的目标进程。
 */
typedef int (*real_ioctl_fn)(int, unsigned long, ...);

int ioctl(int fd, unsigned long request, ...)
{
	/* 可变参数中第三个参数通常是 ioctl 数据指针，先完整取出再决定处理或转发。 */
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

		/* 只填 RKAIQ 选 IQ 所需的 base 字段，并保证结构其余部分为零。 */
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
		/* RTLD_NEXT 跳过本 shim，找到动态链接顺序中的下一份真实 ioctl。 */
		real_ioctl = (real_ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
		if (!real_ioctl) {
			errno = ENOSYS;
			return -1;
		}
	}
	return real_ioctl(fd, request, arg);
}
