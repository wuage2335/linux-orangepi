#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
STREAMING_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
LEARNING_ROOT=$(cd "$STREAMING_ROOT/.." && pwd)
MPP_ROOT="$LEARNING_ROOT/mpp"
MPP_BUNDLE="$MPP_ROOT/build/bundle/official-mpp"
RKISP_CONFIG="$LEARNING_ROOT/scripts/configure_rkisp_1080p.sh"
OUTPUT_DIR=${1:-$STREAMING_ROOT/dist}
ARCHIVE="$OUTPUT_DIR/stage5-streaming-source.tar.gz"
CHECKSUM="$ARCHIVE.sha256"
STAGE=$(mktemp -d)

cleanup()
{
	rm -rf "$STAGE"
}
trap cleanup EXIT

fail()
{
	echo "ERROR: $*" >&2
	exit 1
}

[[ -f $MPP_BUNDLE/include/rockchip/rk_mpi.h ]] ||
	fail 'MPP development bundle is missing rk_mpi.h'
[[ -f $MPP_BUNDLE/lib/librockchip_mpp.so.0 ]] ||
	fail 'MPP development bundle is missing librockchip_mpp.so.0'
[[ -x $RKISP_CONFIG ]] || fail 'RKISP 1080p configuration script is missing'

PACKAGE_ROOT="$STAGE/ov13850_opi5pro_learning"
mkdir -p "$PACKAGE_ROOT/streaming" \
	"$PACKAGE_ROOT/mpp/src" \
	"$PACKAGE_ROOT/mpp/build/bundle" \
	"$PACKAGE_ROOT/scripts"

tar -C "$STREAMING_ROOT" \
	--exclude='./build' \
	--exclude='./dist' \
	-cf - . |
	tar -C "$PACKAGE_ROOT/streaming" -xf -

for header in encoded_packet_sink.hpp mpp_encoder_core.hpp v4l2_capture.hpp; do
	cp "$MPP_ROOT/src/$header" "$PACKAGE_ROOT/mpp/src/$header"
done

cp "$RKISP_CONFIG" "$PACKAGE_ROOT/scripts/configure_rkisp_1080p.sh"
chmod 0755 "$PACKAGE_ROOT/scripts/configure_rkisp_1080p.sh"

cp -a "$MPP_BUNDLE" "$PACKAGE_ROOT/mpp/build/bundle/official-mpp"

mkdir -p "$OUTPUT_DIR"
rm -f "$ARCHIVE" "$CHECKSUM"
tar -C "$STAGE" -czf "$ARCHIVE" ov13850_opi5pro_learning
(
	cd "$OUTPUT_DIR"
	sha256sum "$(basename "$ARCHIVE")" >"$(basename "$CHECKSUM")"
)

echo "STREAMING_SOURCE_PACKAGE=OK"
echo "archive=$ARCHIVE"
echo "checksum=$CHECKSUM"
