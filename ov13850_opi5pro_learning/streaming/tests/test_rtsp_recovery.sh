#!/usr/bin/env bash
set -euo pipefail

[[ $# -ge 2 && $# -le 4 ]] || {
	echo "usage: $0 <streaming-root> <video-device> [service] [mount]" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
DEVICE=$2
SERVICE=${3:-8554}
MOUNT=${4:-/live}
BIN="$ROOT/build/bin/v4l2_mpp_rtsp_server"
SERVER_LOG=$(mktemp)
CLIENT_ONE_LOG=$(mktemp)
CLIENT_TWO_LOG=$(mktemp)
SERVER_PID=

cleanup()
{
	if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
		kill -TERM "$SERVER_PID" 2>/dev/null || true
		wait "$SERVER_PID" 2>/dev/null || true
	fi
	rm -f "$SERVER_LOG" "$CLIENT_ONE_LOG" "$CLIENT_TWO_LOG"
}
trap cleanup EXIT

fail()
{
	echo "FAIL: $*" >&2
	echo "===== RTSP SERVER LOG =====" >&2
	cat "$SERVER_LOG" >&2 || true
	exit 1
}

wait_for_log()
{
	local marker=$1
	local attempts=${2:-100}

	for ((attempt = 0; attempt < attempts; ++attempt)); do
		grep -F "$marker" "$SERVER_LOG" >/dev/null 2>&1 && return 0
		if ! kill -0 "$SERVER_PID" 2>/dev/null; then
			fail "server exited while waiting for: $marker"
		fi
		sleep 0.1
	done
	fail "timed out waiting for: $marker"
}

wait_for_count()
{
	local marker=$1
	local expected=$2
	local attempts=${3:-100}

	for ((attempt = 0; attempt < attempts; ++attempt)); do
		local count
		count=$(grep -c "^${marker}" "$SERVER_LOG" || true)
		((count >= expected)) && return 0
		if ! kill -0 "$SERVER_PID" 2>/dev/null; then
			fail "server exited while waiting for $expected x $marker"
		fi
		sleep 0.1
	done
	fail "timed out waiting for $expected x $marker"
}

run_client()
{
	local log=$1
	local decoder
	local ret

	if gst-inspect-1.0 avdec_h264 >/dev/null 2>&1; then
		decoder=avdec_h264
	elif gst-inspect-1.0 mppvideodec >/dev/null 2>&1; then
		decoder=mppvideodec
	elif gst-inspect-1.0 openh264dec >/dev/null 2>&1; then
		decoder=openh264dec
	else
		fail "missing H.264 decoder: avdec_h264, mppvideodec, or openh264dec"
	fi

	set +e
	GST_DEBUG_NO_COLOR=1 timeout 6s gst-launch-1.0 -v \
		rtspsrc location="rtsp://127.0.0.1:${SERVICE}${MOUNT}" \
		latency=30 protocols=tcp \
		! rtph264depay \
		! h264parse \
		! "$decoder" \
		! identity name=decoded_frame silent=false \
		! fakesink sync=false \
		>"$log" 2>&1
	ret=$?
	set -e

	[[ $ret -eq 0 || $ret -eq 124 ]] || {
		cat "$log" >&2
		fail "RTSP client failed with status $ret"
	}

	local frames
	frames=$(grep -c 'decoded_frame.*chain' "$log" || true)
	((frames >= 30)) || {
		cat "$log" >&2
		fail "client decoded only $frames frames"
	}
	printf 'decoded_frames=%d\n' "$frames"
}

[[ -x "$BIN" ]] || fail "missing executable: $BIN"
[[ -c "$DEVICE" ]] || fail "not a video character device: $DEVICE"
command -v gst-launch-1.0 >/dev/null || fail "missing gst-launch-1.0"
command -v gst-inspect-1.0 >/dev/null || fail "missing gst-inspect-1.0"
command -v timeout >/dev/null || fail "missing timeout"

"$BIN" \
	--device "$DEVICE" \
	--service "$SERVICE" \
	--mount "$MOUNT" \
	--bitrate 8000000 \
	--gop 30 \
	--mtu 1200 \
	--queue-buffers 2 \
	--mode dmabuf \
	>"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

wait_for_log 'RTSP_SERVER_READY'
run_client "$CLIENT_ONE_LOG"
kill -0 "$SERVER_PID" 2>/dev/null || fail "server died after first client"
wait_for_count 'RTSP_CLIENT_DISCONNECTED' 1 100

run_client "$CLIENT_TWO_LOG"
kill -0 "$SERVER_PID" 2>/dev/null || fail "server died after reconnect"
wait_for_count 'RTSP_CLIENT_DISCONNECTED' 2 100

connected=$(grep -c '^RTSP_CLIENT_CONNECTED' "$SERVER_LOG" || true)
disconnected=$(grep -c '^RTSP_CLIENT_DISCONNECTED' "$SERVER_LOG" || true)
idr_requests=$(grep -c '^IDR_REQUESTED' "$SERVER_LOG" || true)

((connected >= 2)) || fail "expected two client connections, got $connected"
((disconnected >= 2)) || fail "expected two client disconnects, got $disconnected"
((idr_requests >= 2)) || fail "expected two reconnect IDR requests, got $idr_requests"

kill -TERM "$SERVER_PID"
wait "$SERVER_PID"
SERVER_PID=

grep -F 'RTSP_SERVER_STOPPED' "$SERVER_LOG" >/dev/null ||
	fail "missing clean shutdown marker"

PM_ROOT=/sys/bus/i2c/devices/3-0010/power
[[ $(cat "$PM_ROOT/runtime_status") == suspended ]] ||
	fail "sensor did not runtime suspend"
[[ $(cat "$PM_ROOT/runtime_usage") == 0 ]] ||
	fail "sensor runtime usage is not zero"

echo "connections=$connected disconnects=$disconnected idr_requests=$idr_requests"
echo "PASS: shared RTSP reconnect recovery"
