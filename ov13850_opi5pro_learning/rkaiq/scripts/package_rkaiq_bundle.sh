#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 3 ]] || {
	echo "usage: $0 <source> <cmake-output> <bundle>" >&2
	exit 2
}

SOURCE=$(cd "$1" && pwd)
OUTPUT=$(cd "$2" && pwd)
BUNDLE=$3
COMMIT=$(git -C "$SOURCE" rev-parse HEAD)
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RKAIQ_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
TOOLS="$RKAIQ_ROOT/build/aarch64"

rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/bin" "$BUNDLE/lib" "$BUNDLE/share"
install -m 0755 "$OUTPUT/exe/Release/rkaiq_3A_server" "$BUNDLE/bin/"
install -m 0644 "$OUTPUT/all_lib/Release/librkaiq.so" "$BUNDLE/lib/"
install -m 0755 "$TOOLS/rkmodule_info_probe" "$BUNDLE/bin/"
install -m 0644 "$TOOLS/librkmodule_info_preload.so" "$BUNDLE/lib/"
install -m 0755 "$RKAIQ_ROOT/scripts/prepare_compatible_iq.sh" "$BUNDLE/bin/"
install -m 0755 "$RKAIQ_ROOT/scripts/run_rkaiq_local.sh" "$BUNDLE/bin/"
install -m 0755 "$RKAIQ_ROOT/scripts/capture_image_stats.sh" "$BUNDLE/bin/"
install -m 0755 "$RKAIQ_ROOT/scripts/validate_real_scenes.sh" "$BUNDLE/bin/"
install -m 0644 "$SOURCE/NOTICE" "$BUNDLE/share/NOTICE"

cat > "$BUNDLE/share/ORIGIN.md" <<EOF
# RKAIQ Origin

- Repository: https://gitlab.com/rk3588_linux/linux/external/camera_engine_rkaiq.git
- Branch: rk3588
- Commit: $COMMIT
- Target: RK3588 / ISP_HW_V30 / Linux aarch64 glibc
- UAPI: synchronized from this linux-orangepi kernel tree at build time
EOF

file "$BUNDLE/bin/rkaiq_3A_server" | grep -F 'ARM aarch64' >/dev/null
file "$BUNDLE/lib/librkaiq.so" | grep -F 'ARM aarch64' >/dev/null
readelf -d "$BUNDLE/bin/rkaiq_3A_server" | grep -E 'RPATH|RUNPATH' && {
	echo "ERROR: server contains build-tree runtime path" >&2
	exit 1
} || true

(
	cd "$BUNDLE"
	find bin lib share -type f ! -name SHA256SUMS -print0 | sort -z |
		xargs -0 sha256sum > SHA256SUMS
)
echo "RKAIQ_BUNDLE_OK=$BUNDLE"
