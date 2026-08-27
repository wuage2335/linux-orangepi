#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RKAIQ_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
KERNEL_ROOT=$(cd "$RKAIQ_ROOT/../.." && pwd)
BUILD_ROOT=${RKAIQ_BUILD_ROOT:-$RKAIQ_ROOT/build}
SOURCE="$BUILD_ROOT/source"
OUTPUT="$BUILD_ROOT/output"
UAPI="$BUILD_ROOT/uapi"

REPOSITORY=https://gitlab.com/rk3588_linux/linux/external/camera_engine_rkaiq.git
BRANCH=rk3588
COMMIT=5af997da2442a504b1005cb804a75745171dc522

for tool in git cmake ninja aarch64-linux-gnu-gcc aarch64-linux-gnu-g++; do
	command -v "$tool" >/dev/null || { echo "ERROR: missing $tool" >&2; exit 1; }
done

mkdir -p "$BUILD_ROOT"
if [[ ! -d $SOURCE/.git ]]; then
	[[ ! -e $SOURCE ]] || { echo "ERROR: source exists but is not git" >&2; exit 1; }
	git clone --depth 1 --branch "$BRANCH" "$REPOSITORY" "$SOURCE"
fi
git -C "$SOURCE" fetch --depth 1 origin "$COMMIT"
git -C "$SOURCE" checkout --detach "$COMMIT"
[[ $(git -C "$SOURCE" rev-parse HEAD) == "$COMMIT" ]] || exit 1

mkdir -p "$UAPI/linux"
cp "$KERNEL_ROOT/include/uapi/linux/rk-camera-module.h" "$SOURCE/include/common/rk-camera-module.h"
cp "$KERNEL_ROOT/include/uapi/linux/rk-video-format.h" "$UAPI/linux/rk-video-format.h"

cmake -S "$SOURCE" -B "$OUTPUT" -G Ninja \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=aarch64 \
	-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
	-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
	-DCMAKE_BUILD_TYPE=Release \
	-DARCH=aarch64 \
	-DRKAIQ_TARGET_SOC=rk3588 \
	-DISP_HW_VERSION=-DISP_HW_V30 \
	-DCMAKE_SKIP_RPATH=TRUE \
	-DCMAKE_C_FLAGS="-I$UAPI" \
	-DCMAKE_CXX_FLAGS="-I$UAPI"

ninja -C "$OUTPUT" rkaiq rkaiq_3A_server -j8
"$SCRIPT_DIR/package_rkaiq_bundle.sh" "$SOURCE" "$OUTPUT" "$BUILD_ROOT/bundle"
