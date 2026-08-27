#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
	echo "usage: $0 <rkaiq-learning-root>" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
MAKEFILE="$ROOT/Makefile"
HOST_PROBE="$ROOT/build/host/rkmodule_info_probe"
HOST_SHIM="$ROOT/build/host/librkmodule_info_preload.so"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -f "$MAKEFILE" ]] || fail "missing Makefile"
make -C "$ROOT" host-tools >/dev/null
[[ -x "$HOST_PROBE" ]] || fail "missing host probe"
[[ -f "$HOST_SHIM" ]] || fail "missing host shim"

if "$HOST_PROBE" /dev/null >"$TMP/plain.out" 2>"$TMP/plain.err"; then
	fail "plain /dev/null module-info ioctl unexpectedly succeeded"
fi

RKAIQ_MODULE_INFO_SHIM=1 \
LD_PRELOAD="$HOST_SHIM" \
	"$HOST_PROBE" /dev/null >"$TMP/shim.out"

RKAIQ_MODULE_INFO_SHIM=1 \
LD_PRELOAD="$HOST_SHIM" \
	"$HOST_PROBE" --sign-extended /dev/null >"$TMP/shim-signed.out"

grep -Fx 'sensor=ov13850' "$TMP/shim.out" >/dev/null || fail "sensor mismatch"
grep -Fx 'module=CMK-CT0116' "$TMP/shim.out" >/dev/null || fail "module mismatch"
grep -Fx 'lens=default' "$TMP/shim.out" >/dev/null || fail "lens mismatch"
cmp "$TMP/shim.out" "$TMP/shim-signed.out" ||
	fail "sign-extended ioctl request was not normalized"

echo "PASS: module-info preload compatibility gate"
