#!/usr/bin/env bash
# 源码包在 aarch64 板端原生构建；日志建议由调用者用 tee 保存。
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec 9>"${CAMERA_LOCK_FILE:-/tmp/ov13850-camera-session.lock}"
flock -n 9 || { echo 'ERROR: camera session locked; stop streaming before rebuilding' >&2; exit 1; }
[[ $(uname -m) == aarch64 ]] || { echo 'ERROR: expected aarch64 board' >&2; exit 1; }
pkg-config --modversion gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0
make -C "$ROOT" smoke test-rtp-sink test-congestion test-live-pts rtp rtsp
python3 "$ROOT/tests/test_camera_session.py"
python3 "$ROOT/tests/test_runtime_install.py"
python3 "$ROOT/tests/test_session_failures.py"
echo BOARD_BUILD_AND_TESTS_OK
