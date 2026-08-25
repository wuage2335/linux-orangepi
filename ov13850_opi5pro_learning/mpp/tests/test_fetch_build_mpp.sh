#!/usr/bin/env bash

set -euo pipefail

TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MPP_ROOT=$(cd "$TEST_DIR/.." && pwd)
BUILD_SCRIPT="$MPP_ROOT/scripts/fetch_build_mpp.sh"
PACKAGE_SCRIPT="$MPP_ROOT/scripts/package_mpp_bundle.sh"
ORIGIN="$MPP_ROOT/ORIGIN.md"
SDK="$MPP_ROOT/build/sdk"
BUNDLE="$MPP_ROOT/build/bundle/official-mpp"
EXPECTED_COMMIT=c08762ebfadeb4e986d2fed993bc7a54862d3ebe

fail()
{
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

[[ -x $BUILD_SCRIPT ]] || fail "missing executable build script: $BUILD_SCRIPT"
[[ -x $PACKAGE_SCRIPT ]] || fail "missing executable package script: $PACKAGE_SCRIPT"
[[ -f $ORIGIN ]] || fail "missing origin manifest: $ORIGIN"

grep -Fq "$EXPECTED_COMMIT" "$ORIGIN" || fail 'origin commit mismatch'

"$BUILD_SCRIPT"

[[ -f $SDK/include/rockchip/rk_mpi.h ]] || fail 'missing rk_mpi.h'
[[ -f $SDK/include/rockchip/mpp_frame.h ]] || fail 'missing mpp_frame.h'
[[ -f $SDK/lib/librockchip_mpp.so ]] || fail 'missing librockchip_mpp.so'
[[ -x $SDK/bin/mpi_enc_test ]] || fail 'missing mpi_enc_test'
[[ -x $SDK/bin/mpp_info_test ]] || fail 'missing mpp_info_test'
[[ -f $SDK/SHA256SUMS ]] || fail 'missing SDK SHA256SUMS'

file -L "$SDK/lib/librockchip_mpp.so" | grep -Fq 'ARM aarch64' ||
    fail 'MPP library is not aarch64'
file "$SDK/bin/mpi_enc_test" | grep -Fq 'ARM aarch64' ||
    fail 'mpi_enc_test is not aarch64'
file "$SDK/bin/mpp_info_test" | grep -Fq 'ARM aarch64' ||
    fail 'mpp_info_test is not aarch64'

(cd "$SDK" && sha256sum -c SHA256SUMS)

"$PACKAGE_SCRIPT"

for header in rk_mpi.h mpp_buffer.h mpp_frame.h mpp_packet.h rk_venc_cfg.h; do
    [[ -f $BUNDLE/include/rockchip/$header ]] ||
        fail "bundle missing development header: $header"
done

(cd "$BUNDLE" && sha256sum -c SHA256SUMS)

printf 'PASS: official MPP SDK build\n'
