#!/usr/bin/env bash
# 封装当前源码、固定 aarch64 SDK 和 RGA 运行包；不打包 host 构建产物。
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LEARNING=$(cd "$ROOT/.." && pwd)
[[ $# == 1 && ! -e $1 ]] || { echo "usage: $0 <new-output-directory>" >&2; exit 2; }
OUTPUT=$1
MPP="$LEARNING/mpp/build/bundle/official-mpp"
RGA="$LEARNING/rga/build/rga_nv12_resize"
[[ -f $MPP/lib/librockchip_mpp.so.0 && -f $RGA/lib/librga.so ]]
(cd "$MPP" && sha256sum -c SHA256SUMS)
file -L "$MPP/lib/librockchip_mpp.so.0" | grep -q 'ARM aarch64'
file "$RGA/bin/rga_nv12_resize" | grep -q 'ARM aarch64'
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd)
STAGE=$(mktemp -d /tmp/camera-closeout.XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
DEST="$STAGE/ov13850_opi5pro_learning"
mkdir -p "$DEST"
tar -C "$LEARNING" --exclude=build --exclude=dist --exclude=__pycache__ \
    -cf - streaming scripts mpp/src mpp/tests rga/src rga/tests benchmarks rkaiq |
    tar -C "$DEST" -xf -
mkdir -p "$DEST/mpp/build/bundle" "$DEST/rga/build"
cp -a "$MPP" "$DEST/mpp/build/bundle/"
cp -a "$RGA" "$DEST/rga/build/"
tar -C "$STAGE" -czf "$OUTPUT/camera-closeout.tar.gz" ov13850_opi5pro_learning
(cd "$OUTPUT" && sha256sum camera-closeout.tar.gz > camera-closeout.tar.gz.sha256)
echo "CLOSEOUT_PACKAGE_OK $OUTPUT/camera-closeout.tar.gz"
