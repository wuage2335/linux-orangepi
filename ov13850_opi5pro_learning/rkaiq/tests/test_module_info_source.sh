#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
	echo "usage: $0 <kernel-tree>" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
DRIVER="$ROOT/drivers/media/i2c/ov13850_i2c_min.c"

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -f "$DRIVER" ]] || fail "missing driver: $DRIVER"

for token in \
	'<linux/rk-camera-module.h>' \
	'module_index' \
	'module_facing' \
	'module_name' \
	'len_name' \
	'RKMODULE_CAMERA_MODULE_INDEX' \
	'RKMODULE_CAMERA_MODULE_FACING' \
	'RKMODULE_CAMERA_MODULE_NAME' \
	'RKMODULE_CAMERA_LENS_NAME' \
	'RKMODULE_GET_MODULE_INFO' \
	'ov13850_min_get_module_inf' \
	'ov13850_min_ioctl' \
	'ov13850_min_compat_ioctl32' \
	'v4l2_subdev_core_ops' \
	'.core = &ov13850_core_ops' \
	'"m%02d_%s_ov13850 %s"'; do
	grep -F "$token" "$DRIVER" >/dev/null ||
		fail "missing module-info token: $token"
done

echo "PASS: OV13850 module-info source contract"
