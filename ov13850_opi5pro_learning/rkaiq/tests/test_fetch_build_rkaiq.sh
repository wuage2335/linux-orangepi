#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
	echo "usage: $0 <rkaiq-learning-root>" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
FETCH="$ROOT/scripts/fetch_build_rkaiq.sh"
PACKAGE="$ROOT/scripts/package_rkaiq_bundle.sh"

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -x "$FETCH" ]] || fail "missing fetch/build script"
[[ -x "$PACKAGE" ]] || fail "missing package script"
bash -n "$FETCH"
bash -n "$PACKAGE"

for token in \
	'https://gitlab.com/rk3588_linux/linux/external/camera_engine_rkaiq.git' \
	'5af997da2442a504b1005cb804a75745171dc522' \
	'RKAIQ_TARGET_SOC=rk3588' \
	'ISP_HW_VERSION=-DISP_HW_V30' \
	'aarch64-linux-gnu-gcc' \
	'include/uapi/linux/rk-camera-module.h' \
	'include/uapi/linux/rk-video-format.h' \
	'ninja -C' \
	'rkaiq_3A_server'; do
	grep -F "$token" "$FETCH" >/dev/null ||
		fail "fetch/build script missing token: $token"
done

for token in 'librkaiq.so' 'NOTICE' 'ORIGIN.md' 'SHA256SUMS'; do
	grep -F "$token" "$PACKAGE" >/dev/null ||
		fail "package script missing token: $token"
done

echo "PASS: RKAIQ fetch/build script contract"
