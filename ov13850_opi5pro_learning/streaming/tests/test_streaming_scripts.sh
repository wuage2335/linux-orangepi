#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BOARD_CHECKER="$ROOT/scripts/check_gstreamer_board.sh"
WINDOWS_CHECKER="$ROOT/scripts/check_gstreamer_windows.ps1"
RTP_SENDER="$ROOT/scripts/send_h264_file_rtp.sh"
RTP_RECEIVER="$ROOT/scripts/receive_h264_rtp.ps1"
RTSP_RECEIVER="$ROOT/scripts/receive_h264_rtsp.ps1"
PACKAGE_SCRIPT="$ROOT/scripts/package_streaming_source.sh"
STREAMING_MAKEFILE="$ROOT/Makefile"
SMOKE_SOURCE="$ROOT/tests/check_streaming_build.cpp"

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -x "$BOARD_CHECKER" ]] || fail "missing board environment checker"
[[ -f "$WINDOWS_CHECKER" ]] || fail "missing Windows environment checker"
[[ -x "$RTP_SENDER" ]] || fail "missing H.264 RTP sender"
[[ -f "$RTP_RECEIVER" ]] || fail "missing Windows RTP receiver"
[[ -f "$RTSP_RECEIVER" ]] || fail "missing Windows RTSP receiver"
[[ -x "$PACKAGE_SCRIPT" ]] || fail "missing streaming source packager"
[[ -f "$STREAMING_MAKEFILE" ]] || fail "missing native streaming Makefile"
[[ -f "$SMOKE_SOURCE" ]] || fail "missing native streaming smoke source"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
CALL_LOG="$TMP/calls.log"
mkdir -p "$TMP/bin"
touch "$TMP/video11" "$TMP/mpp_service"

cat >"$TMP/bin/gst-launch-1.0" <<'EOF'
#!/usr/bin/env bash
echo "gst-launch-1.0 $*" >>"$CALL_LOG"
echo "gst-launch-1.0 version 1.20.3"
EOF

cat >"$TMP/bin/gst-inspect-1.0" <<'EOF'
#!/usr/bin/env bash
echo "gst-inspect-1.0 $*" >>"$CALL_LOG"
exit 0
EOF

cat >"$TMP/bin/pkg-config" <<'EOF'
#!/usr/bin/env bash
echo "pkg-config $*" >>"$CALL_LOG"
echo "1.20.3"
EOF

cat >"$TMP/bin/g++" <<'EOF'
#!/usr/bin/env bash
echo "g++ $*" >>"$CALL_LOG"
echo "g++ mock 1.0"
EOF

chmod +x "$TMP/bin"/*

export CALL_LOG
PATH="$TMP/bin:$PATH" \
VIDEO_DEVICE="$TMP/video11" \
MPP_DEVICE="$TMP/mpp_service" \
    "$BOARD_CHECKER" >"$TMP/board.out"

for plugin in appsrc h264parse rtph264pay udpsink \
	      udpsrc rtpjitterbuffer rtph264depay; do
	grep -Fx "gst-inspect-1.0 $plugin" "$CALL_LOG" >/dev/null ||
		fail "board checker skipped plugin: $plugin"
done

grep -F 'gstreamer-1.0' "$CALL_LOG" >/dev/null ||
	fail "board checker skipped gstreamer-1.0"
grep -F 'gstreamer-app-1.0' "$CALL_LOG" >/dev/null ||
	fail "board checker skipped gstreamer-app-1.0"
grep -F 'gstreamer-rtsp-server-1.0' "$CALL_LOG" >/dev/null ||
	fail "board checker skipped gstreamer-rtsp-server-1.0"

for token in udpsrc rtpjitterbuffer rtph264depay h264parse \
	     d3d11h264dec avdec_h264 d3d11videosink autovideosink; do
	grep -F "'$token'" "$WINDOWS_CHECKER" >/dev/null ||
		fail "Windows checker skipped plugin: $token"
done

printf '\x00\x00\x00\x01\x67\x64\x00\x28' >"$TMP/input.h264"
PATH="$TMP/bin:$PATH" \
    "$RTP_SENDER" "$TMP/input.h264" 192.168.1.6 5004 \
    >"$TMP/sender.out"

for token in \
	'filesrc' \
	'h264parse' \
	'video/x-h264,stream-format=byte-stream,framerate=30/1' \
	'rtph264pay pt=96 mtu=1200 config-interval=1' \
	'udpsink host=192.168.1.6 port=5004 sync=true async=false'; do
	grep -F "$token" "$CALL_LOG" >/dev/null ||
		fail "RTP sender missing pipeline token: $token"
done

for token in \
	'rtspsrc' \
	'location=' \
	'protocols=tcp' \
	'latency=' \
	'rtph264depay' \
	'h264parse' \
	'd3d11h264dec' \
	'avdec_h264' \
	'sync=false'; do
	grep -F "$token" "$RTSP_RECEIVER" >/dev/null ||
		fail "Windows RTSP receiver missing pipeline token: $token"
done

for token in \
	'application/x-rtp' \
	'encoding-name=H264' \
	'payload=96' \
	'clock-rate=90000' \
	'rtpjitterbuffer' \
	'drop-on-latency=true' \
	'rtph264depay' \
	'd3d11h264dec' \
	'avdec_h264'; do
	grep -F "$token" "$RTP_RECEIVER" >/dev/null ||
		fail "Windows receiver missing pipeline token: $token"
done

for token in \
	'gstreamer-1.0' \
	'gstreamer-app-1.0' \
	'gstreamer-rtsp-server-1.0' \
	'librockchip_mpp' \
	'$ORIGIN/../lib' \
	'check_streaming_build'; do
	grep -F "$token" "$STREAMING_MAKEFILE" >/dev/null ||
		fail "streaming Makefile missing token: $token"
done

"$PACKAGE_SCRIPT" "$TMP/dist" >"$TMP/package.out"
ARCHIVE="$TMP/dist/stage5-streaming-source.tar.gz"
CHECKSUM="$ARCHIVE.sha256"
[[ -s "$ARCHIVE" ]] || fail "streaming source archive is empty"
[[ -s "$CHECKSUM" ]] || fail "streaming source checksum is missing"
(cd "$TMP/dist" && sha256sum -c "$(basename "$CHECKSUM")") >/dev/null ||
	fail "streaming source checksum failed"

tar -tzf "$ARCHIVE" >"$TMP/archive.list"
for path in \
	'ov13850_opi5pro_learning/streaming/Makefile' \
	'ov13850_opi5pro_learning/mpp/src/encoded_packet_sink.hpp' \
	'ov13850_opi5pro_learning/mpp/src/mpp_encoder_core.hpp' \
	'ov13850_opi5pro_learning/mpp/src/v4l2_capture.hpp' \
	'ov13850_opi5pro_learning/scripts/configure_rkisp_1080p.sh' \
	'ov13850_opi5pro_learning/mpp/build/bundle/official-mpp/include/rockchip/rk_mpi.h' \
	'ov13850_opi5pro_learning/mpp/build/bundle/official-mpp/lib/librockchip_mpp.so.0'; do
	grep -F "$path" "$TMP/archive.list" >/dev/null ||
		fail "streaming source archive missing: $path"
done

echo "PASS: streaming script contracts"
