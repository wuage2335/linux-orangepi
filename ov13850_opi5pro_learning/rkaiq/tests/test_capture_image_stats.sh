#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
	echo "usage: $0 <rkaiq-learning-root>" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
SCRIPT="$ROOT/scripts/capture_image_stats.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -x "$SCRIPT" ]] || fail "missing image stats script"
bash -n "$SCRIPT"

# 4x2 NV12: 8 Y bytes followed by two interleaved U/V pairs.
printf '\x0a\x14\x1e\x28\x32\x3c\x46\x50\x64\x96\x78\xa0' > "$TMP/frame.nv12"
"$SCRIPT" --width 4 --height 2 "$TMP/frame.nv12" > "$TMP/stats.txt"

grep -F 'Y bytes=8 min=10 max=80 mean=45.000' "$TMP/stats.txt" >/dev/null ||
	fail "Y statistics mismatch"
grep -F 'U bytes=2 min=100 max=120 mean=110.000' "$TMP/stats.txt" >/dev/null ||
	fail "U statistics mismatch"
grep -F 'V bytes=2 min=150 max=160 mean=155.000' "$TMP/stats.txt" >/dev/null ||
	fail "V statistics mismatch"

echo "PASS: NV12 image statistics"
