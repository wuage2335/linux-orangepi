#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 ]] || {
	echo "usage: $0 <input.json> <output.json>" >&2
	exit 2
}

INPUT=$1
OUTPUT=$2
command -v python3 >/dev/null || { echo "ERROR: python3 is required" >&2; exit 1; }
[[ -r $INPUT ]] || { echo "ERROR: missing IQ input: $INPUT" >&2; exit 1; }
mkdir -p "$(dirname "$OUTPUT")"

python3 - "$INPUT" "$OUTPUT.tmp" <<'PY'
import json
import sys

source, destination = sys.argv[1:]
with open(source, "r", encoding="utf-8") as stream:
    document = json.load(stream)

enum_renames = {
    "RKAIQ_SENSOR_HDR_MODE_STAGGER": "RK_AIQ_SENSOR_HDR_LINE_MODE_STAGGER",
    "AECV2_STRATEGY_MODE_LOWLIGHT": "AECV2_STRATEGY_MODE_LOWLIGHT_PRIOR",
    "AECV2_HDR_LONGFRMMODE_DISABLE": "AECV2_HDR_LONGFRMMODE_NORMAL",
    "CALIB_AWB_HDR_FR_CH_AUTO": "CALIB_AWB_HDR_FRAME_CHOOSE_MODE_AUTO",
    "CALIB_AWB_BLK_STAT_MODE_AL_V201": "CALIB_AWB_BLK_STAT_MODE_ALL_V201",
    "CalibDbV2_AFMODE_CONT_PICTURE": "CalibDbV2_AF_MODE_CONTINUOUS_PICTURE",
    "CalibDbV2_AFSS_ADAPTIVE_RANGE": "CalibDbV2_CAM_AFM_FSS_ADAPTIVE_RANGE",
    "CalibDbV2_AF_ADAPTIVE_SEARCH": "CalibDbV2_CAM_AFM_ADAPTIVE_SEARCH",
}

def rewrite_enums(value):
    if isinstance(value, dict):
        return {key: rewrite_enums(item) for key, item in value.items()}
    if isinstance(value, list):
        return [rewrite_enums(item) for item in value]
    if isinstance(value, str):
        return enum_renames.get(value, value)
    return value

document = rewrite_enums(document)

renames = {
    "amerge_calib_v11": "amerge_calib_V2",
    "adrc_calib_v11": "adrc_calib_V2",
    "agamma_calib_v11": "agamma_calib_V30",
    "adehaze_calib_v11": "adehaze_calib_v30",
    "cac_v03": "cac_calib",
}
required = set(renames.values())
for main_scene in document["main_scene"]:
    for sub_scene in main_scene["sub_scene"]:
        isp = sub_scene["scene_isp30"]
        for old, new in renames.items():
            if old in isp:
                isp[new] = isp.pop(old)
        isp.pop("cac_v10", None)
        # OV13850 CAM2 has a fixed-focus lens and no focus actuator.  The
        # newer IQ file requests continuous AF, which makes this older RKAIQ
        # reject prepare after it cannot find a lens subdevice.
        af = isp.get("af_v30", {}).get("TuningPara")
        if af is not None:
            af["af_mode"] = "CalibDbV2_AF_MODE_FIXED"
            af.setdefault("contrast_af", {})["enable"] = 0
        missing = required.difference(isp)
        if missing:
            raise SystemExit("missing converted IQ keys: " + ", ".join(sorted(missing)))

with open(destination, "w", encoding="utf-8") as stream:
    json.dump(document, stream, ensure_ascii=False, separators=(",", ":"))
    stream.write("\n")
PY

mv "$OUTPUT.tmp" "$OUTPUT"
sha256sum "$INPUT" "$OUTPUT"
