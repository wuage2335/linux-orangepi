#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 3 ]] || {
    printf 'usage: %s <bundle-dir> <video-device> <output-dir>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
DEVICE=$2
OUTPUT_DIR=$3
BIN="$BUNDLE/bin/rga_v4l2_live"
TIME_BIN=/usr/bin/time

[[ -x $BIN ]] || {
    printf 'ERROR: missing executable: %s\n' "$BIN" >&2
    exit 1
}
[[ -x $TIME_BIN ]] || {
    printf 'ERROR: missing GNU time: %s\n' "$TIME_BIN" >&2
    exit 1
}
command -v v4l2-ctl >/dev/null || {
    printf 'ERROR: v4l2-ctl is unavailable\n' >&2
    exit 1
}

mkdir -p "$OUTPUT_DIR"
rm -f \
    "$OUTPUT_DIR/bypass.time" "$OUTPUT_DIR/bypass.log" \
    "$OUTPUT_DIR/copy.time" "$OUTPUT_DIR/copy.log" \
    "$OUTPUT_DIR/direct.time" "$OUTPUT_DIR/direct.log" \
    "$OUTPUT_DIR/copy-last.nv12" "$OUTPUT_DIR/direct-last.nv12"

"$TIME_BIN" -v -o "$OUTPUT_DIR/bypass.time" \
    v4l2-ctl -d "$DEVICE" \
        --stream-mmap=4 \
        --stream-count=300 \
        --stream-to=/dev/null \
        --stream-poll \
    >"$OUTPUT_DIR/bypass.log" 2>&1

"$TIME_BIN" -v -o "$OUTPUT_DIR/copy.time" \
    "$BIN" "$DEVICE" "$OUTPUT_DIR/copy-last.nv12" \
    >"$OUTPUT_DIR/copy.log" 2>&1

"$TIME_BIN" -v -o "$OUTPUT_DIR/direct.time" \
    "$BIN" --direct "$DEVICE" "$OUTPUT_DIR/direct-last.nv12" \
    >"$OUTPUT_DIR/direct.log" 2>&1

grep -Fq 'mode=copy' "$OUTPUT_DIR/copy.log"
grep -Fq 'mode=direct' "$OUTPUT_DIR/direct.log"
grep -Fq 'RGA_V4L2_LIVE_OK' "$OUTPUT_DIR/copy.log"
grep -Fq 'RGA_V4L2_LIVE_OK' "$OUTPUT_DIR/direct.log"

printf '===== BYPASS =====\n'
grep -E 'Elapsed|Percent of CPU|User time|System time|Maximum resident' \
    "$OUTPUT_DIR/bypass.time"
printf '===== COPY =====\n'
cat "$OUTPUT_DIR/copy.log"
grep -E 'Elapsed|Percent of CPU|User time|System time|Maximum resident' \
    "$OUTPUT_DIR/copy.time"
printf '===== DIRECT =====\n'
cat "$OUTPUT_DIR/direct.log"
grep -E 'Elapsed|Percent of CPU|User time|System time|Maximum resident' \
    "$OUTPUT_DIR/direct.time"

printf 'BENCHMARK_RGA_V4L2_MODES_OK\n'
