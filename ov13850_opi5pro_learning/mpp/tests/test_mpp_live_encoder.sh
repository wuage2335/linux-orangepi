#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 3 ]] || {
    printf 'usage: %s <mpp-bundle> <video-device> <output.h264>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
DEVICE=$2
OUTPUT=$3
DMABUF_OUTPUT="${OUTPUT}.dmabuf.h264"
BIN="$BUNDLE/bin/v4l2_mpp_encoder"
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

[[ -x $BIN ]] || fail "missing executable: $BIN"
[[ -c $DEVICE ]] || fail "not a video character device: $DEVICE"

rm -f "$OUTPUT" "$DMABUF_OUTPUT"
LD_LIBRARY_PATH="$BUNDLE/lib" "$BIN" "$DEVICE" "$OUTPUT" |
    tee "$TEST_ROOT/live.log"

[[ -s $OUTPUT ]] || fail 'live encoder produced empty bitstream'
grep -Fq 'codec=h264' "$TEST_ROOT/live.log"
grep -Fq 'mode=copy' "$TEST_ROOT/live.log"
grep -Fq 'pre_skipped=3 frames_in=300 frames_out=300' "$TEST_ROOT/live.log" ||
    fail 'unexpected live frame counters'
grep -Fq 'timeouts=0 dropped=0' "$TEST_ROOT/live.log" ||
    fail 'live capture reported timeout or drop'
grep -Eq 'packets=300' "$TEST_ROOT/live.log" || fail 'unexpected packet count'
grep -Eq 'idr_frames=[1-9][0-9]*' "$TEST_ROOT/live.log" || fail 'missing IDR'
grep -Eq 'copy_average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing copy average'
grep -Eq 'mpp_average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing MPP average'
grep -Eq 'loop_fps=[0-9]+([.][0-9]+)?' "$TEST_ROOT/live.log" ||
    fail 'missing loop FPS'
grep -Fq 'MPP_V4L2_ENCODE_OK' "$TEST_ROOT/live.log" ||
    fail 'missing live success marker'

LD_LIBRARY_PATH="$BUNDLE/lib" "$BIN" --dmabuf "$DEVICE" "$DMABUF_OUTPUT" |
    tee "$TEST_ROOT/dmabuf.log"

[[ -s $DMABUF_OUTPUT ]] || fail 'DMA-BUF encoder produced empty bitstream'
grep -Fq 'mode=dmabuf' "$TEST_ROOT/dmabuf.log"
grep -Fq 'pre_skipped=3 frames_in=300 frames_out=300' "$TEST_ROOT/dmabuf.log" ||
    fail 'unexpected DMA-BUF frame counters'
grep -Fq 'timeouts=0 dropped=0' "$TEST_ROOT/dmabuf.log" ||
    fail 'DMA-BUF capture reported timeout or drop'
grep -Fq 'copy_average_us=0.00' "$TEST_ROOT/dmabuf.log" ||
    fail 'DMA-BUF path reported copy time'
grep -Fq 'MPP_V4L2_ENCODE_OK' "$TEST_ROOT/dmabuf.log" ||
    fail 'missing DMA-BUF success marker'

sha256sum "$OUTPUT" "$DMABUF_OUTPUT"
printf 'PASS: V4L2 MPP H.264 copy/dmabuf encoder\n'
