#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BOARD_CHECKER="$ROOT/scripts/check_gstreamer_board.sh"
WINDOWS_CHECKER="$ROOT/scripts/check_gstreamer_windows.ps1"

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

[[ -x "$BOARD_CHECKER" ]] || fail "missing board environment checker"
[[ -f "$WINDOWS_CHECKER" ]] || fail "missing Windows environment checker"

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

echo "PASS: streaming script contracts"
