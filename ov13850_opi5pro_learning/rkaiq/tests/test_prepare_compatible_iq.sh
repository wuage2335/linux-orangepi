#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || {
	echo "usage: $0 <rkaiq-learning-root>" >&2
	exit 2
}

ROOT=$(cd "$1" && pwd)
SCRIPT="$ROOT/scripts/prepare_compatible_iq.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

[[ -x $SCRIPT ]] || { echo "FAIL: missing IQ conversion script" >&2; exit 1; }

cat > "$TMP/input.json" <<'JSON'
{"main_scene":[{"sub_scene":[{"scene_isp30":{
  "amerge_calib_v11":{"id":1},
  "adrc_calib_v11":{"id":2},
  "agamma_calib_v11":{"id":3},
  "adehaze_calib_v11":{"id":4},
  "cac_v03":{"id":5},
  "cac_v10":{"id":6},
  "af_v30":{"TuningPara":{"af_mode":"CalibDbV2_AFMODE_CONT_PICTURE","contrast_af":{"enable":1}}},
  "wb_v21":{"keep":true,"frameChooseMode":"CALIB_AWB_HDR_FR_CH_AUTO",
    "blkMeasureMode":"CALIB_AWB_BLK_STAT_MODE_AL_V201"},
  "ae":{"strategy":"AECV2_STRATEGY_MODE_LOWLIGHT",
    "longFrame":"AECV2_HDR_LONGFRMMODE_DISABLE"},
  "sensor":{"lineMode":"RKAIQ_SENSOR_HDR_MODE_STAGGER"}
}}]}]}
JSON

"$SCRIPT" "$TMP/input.json" "$TMP/output.json" >/dev/null
python3 - "$TMP/output.json" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as stream:
    isp = json.load(stream)["main_scene"][0]["sub_scene"][0]["scene_isp30"]

required = {
    "amerge_calib_V2", "adrc_calib_V2", "agamma_calib_V30",
    "adehaze_calib_v30", "cac_calib",
}
assert required.issubset(isp)
assert isp["wb_v21"]["keep"] is True
assert isp["af_v30"]["TuningPara"]["af_mode"] == "CalibDbV2_AF_MODE_FIXED"
assert isp["af_v30"]["TuningPara"]["contrast_af"]["enable"] == 0
assert isp["wb_v21"]["frameChooseMode"] == "CALIB_AWB_HDR_FRAME_CHOOSE_MODE_AUTO"
assert isp["wb_v21"]["blkMeasureMode"] == "CALIB_AWB_BLK_STAT_MODE_ALL_V201"
assert isp["ae"]["strategy"] == "AECV2_STRATEGY_MODE_LOWLIGHT_PRIOR"
assert isp["ae"]["longFrame"] == "AECV2_HDR_LONGFRMMODE_NORMAL"
assert isp["sensor"]["lineMode"] == "RK_AIQ_SENSOR_HDR_LINE_MODE_STAGGER"
assert "adrc_calib_v11" not in isp
assert "cac_v10" not in isp
PY

echo "PASS: RKAIQ IQ schema compatibility conversion"
