#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MPP_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SDK="$MPP_ROOT/build/sdk"
BUNDLE="$MPP_ROOT/build/bundle/official-mpp"

fail()
{
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

[[ -x $SDK/bin/mpi_enc_test ]] || fail 'run fetch_build_mpp.sh first'
[[ -x $SDK/bin/mpp_info_test ]] || fail 'missing mpp_info_test'
[[ -f $SDK/lib/librockchip_mpp.so.0 ]] || fail 'missing shared MPP library'

mkdir -p "$BUNDLE/bin" "$BUNDLE/lib" "$BUNDLE/share"
cp "$SDK/bin/mpi_enc_test" "$BUNDLE/bin/"
cp "$SDK/bin/mpp_info_test" "$BUNDLE/bin/"
cp -a "$SDK/lib/librockchip_mpp.so"* "$BUNDLE/lib/"
cp "$MPP_ROOT/ORIGIN.md" "$BUNDLE/share/ORIGIN.md"

rm -f "$BUNDLE/SHA256SUMS"
(
    cd "$BUNDLE"
    find . -type f ! -name SHA256SUMS -print0 |
        sort -z |
        xargs -0 sha256sum > SHA256SUMS
)

printf 'MPP_BUNDLE_OK\n'
printf 'bundle=%s\n' "$BUNDLE"
