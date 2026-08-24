#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 3 ]] || {
    printf 'usage: %s <bundle-dir> <video-device> <output-file>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
DEVICE=$2
OUTPUT=$3
BIN="$BUNDLE/bin/rga_v4l2_live"
LIB="$BUNDLE/lib/librga.so"
TEST_ROOT=$(mktemp -d)

cleanup()
{
    rm -rf "$TEST_ROOT"
}
trap cleanup EXIT

fail()
{
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

expect_failure()
{
    local name=$1
    shift

    if "$BIN" "$@" \
        >"$TEST_ROOT/$name.stdout" \
        2>"$TEST_ROOT/$name.stderr"
    then
        fail "$name unexpectedly succeeded"
    fi

    grep -Fq 'ERROR:' "$TEST_ROOT/$name.stderr" ||
        fail "$name did not report ERROR:"
}

[[ -x $BIN ]] || fail "missing executable: $BIN"
[[ -f $LIB ]] || fail "missing bundled library: $LIB"
[[ -c $DEVICE ]] || fail "video device is not a character device: $DEVICE"

file "$BIN" | grep -Fq 'ARM aarch64' ||
    fail 'binary is not aarch64'
file "$LIB" | grep -Fq 'ARM aarch64' ||
    fail 'librga is not aarch64'

RESOLVED_LIB=$(ldd "$BIN" |
    awk '$1 == "librga.so" && $2 == "=>" { print $3; exit }')
[[ -n $RESOLVED_LIB ]] || fail 'ldd did not report a resolved librga.so'

EXPECTED_LIB=$(readlink -f "$LIB")
RESOLVED_LIB=$(readlink -f "$RESOLVED_LIB")
[[ $RESOLVED_LIB == "$EXPECTED_LIB" ]] ||
    fail "binary resolved librga.so to $RESOLVED_LIB, expected $EXPECTED_LIB"

expect_failure no_args
expect_failure invalid_device /dev/null "$TEST_ROOT/invalid-output.nv12"
[[ ! -e $TEST_ROOT/invalid-output.nv12 ]] ||
    fail 'invalid-device failure left an output file'

rm -f "$OUTPUT"

"$BIN" "$DEVICE" "$OUTPUT" |
    tee "$TEST_ROOT/live.log"

[[ -f $OUTPUT ]] || fail 'live run did not create output'
[[ $(stat -c '%s' "$OUTPUT") -eq 1382400 ]] ||
    fail 'live output size is not 1382400 bytes'

od -An -tu1 -v "$OUTPUT" |
awk '
{
    for (i = 1; i <= NF; i++) {
        if (($i + 0) != 0)
            nonzero = 1
    }
}
END {
    exit nonzero ? 0 : 1
}
' || fail 'live output is all zero'

grep -Fq 'librga=' "$TEST_ROOT/live.log" ||
    fail 'missing librga version'
grep -Fq 'pre_skipped=3 processed=300 timeouts=0 dropped=0' \
    "$TEST_ROOT/live.log" ||
    fail 'unexpected frame counters'
grep -Eq 'copy_total_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing copy total time'
grep -Eq 'copy_average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing copy average time'
grep -Eq 'rga_total_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing RGA total time'
grep -Eq 'rga_average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing RGA average time'
grep -Eq 'loop_total_s=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing loop total time'
grep -Eq 'capture_process_fps=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing capture/process FPS'
grep -Fq 'RGA_V4L2_LIVE_OK' "$TEST_ROOT/live.log" ||
    fail 'missing live success marker'

sha256sum "$OUTPUT"
printf 'PASS: realtime V4L2 RGA copy-path tests\n'
