#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
	echo "usage: $0 <rkaiq-learning-root>" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
FETCH="$ROOT/scripts/fetch_build_rkaiq.sh"
PACKAGE="$ROOT/scripts/package_rkaiq_bundle.sh"
RUN="$ROOT/scripts/run_rkaiq_local.sh"
PATCH="$ROOT/patches/0001-ov13850-learning-compat.patch"

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -x "$FETCH" ]] || fail "missing fetch/build script"
[[ -x "$PACKAGE" ]] || fail "missing package script"
[[ -x "$RUN" ]] || fail "missing local run script"
[[ -f "$PATCH" ]] || fail "missing OV13850 compatibility patch"
bash -n "$FETCH"
bash -n "$PACKAGE"
bash -n "$RUN"

for token in \
	'https://gitlab.com/rk3588_linux/linux/external/camera_engine_rkaiq.git' \
	'5af997da2442a504b1005cb804a75745171dc522' \
	'RKAIQ_TARGET_SOC=rk3588' \
	'ISP_HW_VERSION=-DISP_HW_V30' \
	'aarch64-linux-gnu-gcc' \
	'include/uapi/linux/rk-camera-module.h' \
	'rk-video-format.h' \
	'0001-ov13850-learning-compat.patch' \
	'ninja -C' \
	'rkaiq_3A_server'; do
	grep -F "$token" "$FETCH" >/dev/null ||
		fail "fetch/build script missing token: $token"
done

for token in 'static int width = 2112' 'static int height = 1568' \
	'ov13850_i2c_min 3-0010' 'rkaiq sysctl init failed' \
	'normal_no_read_back' 'fixed-focus sensor: disable AF algorithm'; do
	grep -F "$token" "$PATCH" >/dev/null ||
		fail "compatibility patch missing token: $token"
done

for token in 'struct sensor_exposure_cfg exposure' 'u32 params_id'; do
	grep -F "$token" "$PATCH" >/dev/null ||
		fail "ISP3 ABI patch missing token: $token"
done

for token in 'librkaiq.so' 'prepare_compatible_iq.sh' 'NOTICE' 'ORIGIN.md' 'SHA256SUMS'; do
	grep -F "$token" "$PACKAGE" >/dev/null ||
		fail "package script missing token: $token"
done

for token in 'normal_no_read_back=1' 'RKAIQ_IQ_PATH' 'prepare_compatible_iq.sh'; do
	grep -F "$token" "$RUN" >/dev/null ||
		fail "run script missing token: $token"
done

echo "PASS: RKAIQ fetch/build script contract"
