#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/videodev2.h>
#include <linux/rk-camera-module.h>

/*
 * RKAIQ 启动时不只需要知道芯片叫 OV13850，还要知道具体模组和镜头名称，才能
 * 选择与标定硬件匹配的 IQ JSON。本工具直接向 sensor subdev 发送 Rockchip
 * 私有 ioctl，把“内核有没有正确提供元数据”与“RKAIQ/IQ 是否工作”分开验证。
 *
 * 输入：/dev/v4l-subdevN；输出：sensor/module/lens 三个字符串。
 */
int main(int argc, char **argv)
{
	struct rkmodule_inf info;
	const char *device;
	unsigned long request = RKMODULE_GET_MODULE_INFO;
	int fd;

	if (argc == 3 && !strcmp(argv[1], "--sign-extended")) {
		/*
		 * 某些 AArch64 调用链会把 32 位 ioctl 命令做符号扩展。该选项复现这种
		 * 表示，验证驱动或兼容 shim 是否按命令低 32 位正确识别。
		 */
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
	/* ioctl 成功只代表内核响应，还要拒绝空字段，否则 RKAIQ 仍找不到 IQ。 */
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
