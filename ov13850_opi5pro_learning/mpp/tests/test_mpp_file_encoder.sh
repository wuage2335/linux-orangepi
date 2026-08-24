#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 3 ]] || {
    printf 'usage: %s <mpp-bundle> <1920x1080-nv12> <output.h264>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
INPUT=$2
OUTPUT=$3
BIN="$BUNDLE/bin/nv12_mpp_encoder"
LIB="$BUNDLE/lib/librockchip_mpp.so"
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

    if LD_LIBRARY_PATH="$BUNDLE/lib" "$BIN" "$@" \
        >"$TEST_ROOT/$name.stdout" 2>"$TEST_ROOT/$name.stderr"
    then
        fail "$name unexpectedly succeeded"
    fi
    grep -Fq 'ERROR:' "$TEST_ROOT/$name.stderr" ||
        fail "$name did not report ERROR:"
}

[[ -x $BIN ]] || fail "missing executable: $BIN"
[[ -f $LIB ]] || fail "missing MPP shared library: $LIB"
[[ -r $INPUT ]] || fail "input is not readable: $INPUT"
[[ $(stat -c '%s' "$INPUT") -eq 3110400 ]] ||
    fail 'input size is not 3110400 bytes'

expect_failure no_args
dd if=/dev/zero of="$TEST_ROOT/short.nv12" bs=1024 count=1 status=none
expect_failure short_input "$TEST_ROOT/short.nv12" "$TEST_ROOT/short.h264"

rm -f "$OUTPUT"
LD_LIBRARY_PATH="$BUNDLE/lib" "$BIN" "$INPUT" "$OUTPUT" |
    tee "$TEST_ROOT/encode.log"

[[ -s $OUTPUT ]] || fail 'encoder produced an empty bitstream'
grep -Fq 'codec=h264' "$TEST_ROOT/encode.log" || fail 'missing H.264 codec'
grep -Fq 'frames_in=300 frames_out=300' "$TEST_ROOT/encode.log" ||
    fail 'unexpected frame counters'
grep -Eq 'packets=[1-9][0-9]*' "$TEST_ROOT/encode.log" || fail 'missing packets'
grep -Eq 'idr_frames=[1-9][0-9]*' "$TEST_ROOT/encode.log" || fail 'missing IDR'
grep -Eq 'encoded_bytes=[1-9][0-9]*' "$TEST_ROOT/encode.log" ||
    fail 'missing encoded byte count'
grep -Eq 'encode_fps=[0-9]+([.][0-9]+)?' "$TEST_ROOT/encode.log" ||
    fail 'missing encoder FPS'
grep -Fq 'MPP_FILE_ENCODE_OK' "$TEST_ROOT/encode.log" ||
    fail 'missing success marker'

sha256sum "$OUTPUT"
printf 'PASS: MPP H.264 file encoder\n'
