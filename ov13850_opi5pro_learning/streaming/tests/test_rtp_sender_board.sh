#!/usr/bin/env bash
set -euo pipefail

[[ $# -ge 3 && $# -le 4 ]] || {
	echo "usage: $0 <streaming-root> <video-device> <destination-ip> [port]" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
DEVICE=$2
HOST=$3
PORT=${4:-5004}
BIN="$ROOT/build/bin/v4l2_mpp_rtp_sender"
LOG=$(mktemp)

cleanup()
{
	rm -f "$LOG"
}
trap cleanup EXIT

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -x "$BIN" ]] || fail "missing executable: $BIN"
[[ -c "$DEVICE" ]] || fail "not a video character device: $DEVICE"

if "$BIN" --host invalid --frames 1 >"$LOG" 2>&1; then
	fail "invalid destination unexpectedly succeeded"
fi
grep -F 'ERROR:' "$LOG" >/dev/null ||
	fail "invalid destination did not report ERROR"

"$BIN" \
	--device "$DEVICE" \
	--host "$HOST" \
	--port "$PORT" \
	--frames 300 \
	--bitrate 8000000 \
	--gop 30 \
	--mtu 1200 \
	--queue-buffers 2 \
	--mode dmabuf |
	tee "$LOG"

grep -F 'mode=dmabuf' "$LOG" >/dev/null || fail "missing DMA-BUF mode"
grep -F 'frames_in=300 frames_sent=300' "$LOG" >/dev/null ||
	fail "unexpected live frame counters"
grep -F 'timeouts=0 dropped=0' "$LOG" >/dev/null ||
	fail "capture reported timeout or drop"
grep -F 'rtp_clock_rate=90000' "$LOG" >/dev/null ||
	fail "missing RTP clock rate"
grep -Eq 'queue_overruns=[0-9]+' "$LOG" || fail "missing queue overrun count"
grep -F 'congestion_events=0 congestion_idr_requests=0' "$LOG" >/dev/null ||
	fail "unexpected congestion recovery counters"
grep -F 'STREAM_RTP_OK' "$LOG" >/dev/null || fail "missing success marker"

PM_ROOT=/sys/bus/i2c/devices/3-0010/power
[[ $(cat "$PM_ROOT/runtime_status") == suspended ]] ||
	fail "sensor did not runtime suspend"
[[ $(cat "$PM_ROOT/runtime_usage") == 0 ]] ||
	fail "sensor runtime usage is not zero"

echo "PASS: live MPP RTP sender"
