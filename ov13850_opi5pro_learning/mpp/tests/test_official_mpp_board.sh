#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 2 ]] || {
    printf 'usage: %s <official-mpp-bundle> <output-dir>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
OUTPUT_DIR=$2
LIB_DIR="$BUNDLE/lib"
INFO_TEST="$BUNDLE/bin/mpp_info_test"
ENC_TEST="$BUNDLE/bin/mpi_enc_test"

fail()
{
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

[[ -c /dev/mpp_service ]] || fail 'missing /dev/mpp_service'
[[ -x $INFO_TEST ]] || fail 'missing mpp_info_test'
[[ -x $ENC_TEST ]] || fail 'missing mpi_enc_test'

(cd "$BUNDLE" && sha256sum -c SHA256SUMS)
file "$INFO_TEST" | grep -Fq 'ARM aarch64' || fail 'mpp_info_test is not aarch64'
file "$ENC_TEST" | grep -Fq 'ARM aarch64' || fail 'mpi_enc_test is not aarch64'
file -L "$LIB_DIR/librockchip_mpp.so" | grep -Fq 'ARM aarch64' ||
    fail 'MPP shared library is not aarch64'

mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT_DIR/mpp-info.log" "$OUTPUT_DIR/official-generated.h264"

LD_LIBRARY_PATH="$LIB_DIR" "$INFO_TEST" >"$OUTPUT_DIR/mpp-info.log" 2>&1

LD_LIBRARY_PATH="$LIB_DIR" "$ENC_TEST" \
    -o "$OUTPUT_DIR/official-generated.h264" \
    -w 1920 -h 1080 -f 0 -t 7 -n 30 \
    -rc 1 -bps 8000000 -fps 30 -g 0:60:0 \
    >"$OUTPUT_DIR/mpi-enc.log" 2>&1

[[ -s $OUTPUT_DIR/official-generated.h264 ]] ||
    fail 'official encoder produced an empty bitstream'

grep -Eq 'encode 30 frames|encoded[[:space:]]+30 frame' \
    "$OUTPUT_DIR/mpi-enc.log" ||
    fail 'official encoder did not report 30 frames'

sha256sum "$OUTPUT_DIR/official-generated.h264"
printf 'PASS: official MPP board encoding\n'
