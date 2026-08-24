#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MPP_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
BUILD_ROOT="$MPP_ROOT/build"
SOURCE="$BUILD_ROOT/source/mpp"
CMAKE_BUILD="$BUILD_ROOT/cmake-aarch64"
SDK="$BUILD_ROOT/sdk"
REPOSITORY=https://github.com/rockchip-linux/mpp.git
TAG=1.1.0
COMMIT=c08762ebfadeb4e986d2fed993bc7a54862d3ebe

fail()
{
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

for tool in git cmake make aarch64-linux-gnu-gcc aarch64-linux-gnu-g++; do
    command -v "$tool" >/dev/null || fail "missing build tool: $tool"
done

mkdir -p "$BUILD_ROOT/source" "$CMAKE_BUILD" "$SDK"

if [[ ! -d $SOURCE/.git ]]; then
    [[ ! -e $SOURCE ]] || fail "source path exists but is not a Git repository: $SOURCE"
    git clone --no-checkout "$REPOSITORY" "$SOURCE"
fi

[[ $(git -C "$SOURCE" remote get-url origin) == "$REPOSITORY" ]] ||
    fail 'upstream remote mismatch'

git -C "$SOURCE" fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG"
git -C "$SOURCE" checkout --detach "$COMMIT"

[[ $(git -C "$SOURCE" rev-parse HEAD) == "$COMMIT" ]] ||
    fail 'upstream commit mismatch after checkout'
[[ -z $(git -C "$SOURCE" status --porcelain) ]] ||
    fail 'upstream source has local changes'

cmake -S "$SOURCE" -B "$CMAKE_BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$SDK" \
    -DCMAKE_TOOLCHAIN_FILE="$SOURCE/build/linux/aarch64/arm.linux.cross.cmake" \
    -DTOOLCHAIN=aarch64-linux-gnu- \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TEST=ON \
    -G 'Unix Makefiles'

cmake --build "$CMAKE_BUILD" -j"$(nproc)"
cmake --install "$CMAKE_BUILD"

[[ -f $SDK/include/rockchip/rk_mpi.h ]] ||
    fail 'install did not produce include/rockchip/rk_mpi.h'
[[ -f $SDK/lib/librockchip_mpp.so ]] || fail 'install did not produce shared MPP library'
[[ -x $SDK/bin/mpi_enc_test ]] || fail 'install did not produce mpi_enc_test'
[[ -x $SDK/bin/mpp_info_test ]] || fail 'install did not produce mpp_info_test'

rm -f "$SDK/SHA256SUMS"
(
    cd "$SDK"
    find . -type f ! -name SHA256SUMS -print0 |
        sort -z |
        xargs -0 sha256sum > SHA256SUMS
)

printf 'MPP_SDK_OK\n'
printf 'commit=%s\n' "$COMMIT"
printf 'sdk=%s\n' "$SDK"
