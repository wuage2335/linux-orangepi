#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RKAIQ_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
BUNDLE=${RKAIQ_BUNDLE:-$RKAIQ_ROOT/build/bundle}
DEVICE=${RKAIQ_SENSOR_DEVICE:-/dev/v4l-subdev2}

for file in \
	"$BUNDLE/bin/rkaiq_3A_server" \
	"$BUNDLE/bin/rkmodule_info_probe" \
	"$BUNDLE/lib/librkaiq.so" \
	"$BUNDLE/lib/librkmodule_info_preload.so"; do
	[[ -e $file ]] || { echo "ERROR: missing bundle file: $file" >&2; exit 1; }
done

[[ -r /etc/iqfiles/ov13850_CMK-CT0116_default.json ]] || {
	echo "ERROR: matching OV13850 IQ file is missing" >&2
	exit 1
}

export LD_LIBRARY_PATH="$BUNDLE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="$BUNDLE/lib/librkmodule_info_preload.so"
export RKAIQ_MODULE_INFO_SHIM=1

"$BUNDLE/bin/rkmodule_info_probe" "$DEVICE"
exec "$BUNDLE/bin/rkaiq_3A_server" "$@"
