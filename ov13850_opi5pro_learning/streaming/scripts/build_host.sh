#!/usr/bin/env bash
# 在 x86_64 WSL 构建独立 MPP SDK 和全部 streaming 主机验证目标。
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE=${MPP_SOURCE:-$ROOT/../mpp/build/source/mpp}
BUILD="$ROOT/build/host-sdk"
COMMIT=c08762ebfadeb4e986d2fed993bc7a54862d3ebe
[[ $(git -C "$SOURCE" rev-parse HEAD) == "$COMMIT" ]] || {
    echo 'ERROR: MPP source commit mismatch; use project pinned MPP 1.1.0' >&2
    exit 1
}
[[ -z $(git -C "$SOURCE" status --porcelain) ]] || {
    echo 'ERROR: MPP source has uncommitted changes' >&2
    exit 1
}
pkg-config --exists gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0
cmake -S "$SOURCE" -B "$BUILD/cmake" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$BUILD/sdk" -DBUILD_SHARED_LIBS=ON -DBUILD_TEST=OFF
cmake --build "$BUILD/cmake" -j"${JOBS:-4}"
cmake --install "$BUILD/cmake"
# BUILD 显式区分主机输出，避免覆盖板端部署包。
make -C "$ROOT" BUILD="$ROOT/build/host-streaming" MPP_BUNDLE="$BUILD/sdk" \
	smoke test-rtp-sink test-congestion test-live-pts test-fixed-slot-pool rtp rtsp
make -C "$ROOT/../benchmarks" BUILD="$ROOT/../benchmarks/build/host" \
    MPP_BUNDLE="$BUILD/sdk" all test
python3 "$ROOT/tests/test_camera_session.py"
python3 "$ROOT/tests/test_runtime_install.py"
python3 "$ROOT/tests/test_session_failures.py"
echo HOST_BUILD_AND_TESTS_OK
