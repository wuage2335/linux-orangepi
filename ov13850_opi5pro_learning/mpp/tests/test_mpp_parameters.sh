#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 3 ]] || {
    printf 'usage: %s <mpp-bundle> <input.nv12> <output-dir>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
INPUT=$2
OUTPUT_DIR=$3
BIN="$BUNDLE/bin/nv12_mpp_encoder"

fail()
{
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_case()
{
    local name=$1
    shift
    local output="$OUTPUT_DIR/$name"
    local log="$OUTPUT_DIR/$name.log"

    LD_LIBRARY_PATH="$BUNDLE/lib" "$BIN" "$@" "$INPUT" "$output" |
        tee "$log"
    [[ -s $output ]] || fail "$name produced empty output"
    grep -Fq 'MPP_FILE_ENCODE_OK' "$log" || fail "$name missing success marker"
}

mkdir -p "$OUTPUT_DIR"

run_case h264-cbr-4m.h264 \
    --codec h264 --bitrate 4000000 --gop 30 --rc cbr --frames 30 \
    --request-idr 15
run_case h264-vbr-12m.h264 \
    --codec h264 --bitrate 12000000 --gop 60 --rc vbr --frames 30
run_case h265-cbr-8m.h265 \
    --codec h265 --bitrate 8000000 --gop 30 --rc cbr --frames 30

grep -Fq 'codec=h264' "$OUTPUT_DIR/h264-cbr-4m.h264.log"
grep -Eq 'idr_frames=([2-9]|[1-9][0-9]+)' "$OUTPUT_DIR/h264-cbr-4m.h264.log" ||
    fail 'requested H.264 IDR was not observed'
grep -Fq 'rc=vbr' "$OUTPUT_DIR/h264-vbr-12m.h264.log"
grep -Fq 'codec=h265' "$OUTPUT_DIR/h265-cbr-8m.h265.log"

sha256sum "$OUTPUT_DIR"/*.h264 "$OUTPUT_DIR"/*.h265
printf 'PASS: MPP encoder parameter matrix\n'
