#!/usr/bin/env bash
# 一键板端功能回归；运行前确保 camera_session 长稳已退出。
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LEARNING=$(cd "$ROOT/.." && pwd)
[[ $# == 1 ]] || { echo "usage: $0 <new-evidence-directory>" >&2; exit 2; }
OUTPUT=$1
DEVICE=${CAMERA_DEVICE:-/dev/video11}
SENSOR=${CAMERA_SENSOR:-/dev/v4l-subdev2}
MPP="$LEARNING/mpp/build/bundle/official-mpp"
RGA="$LEARNING/rga/build/rga_nv12_resize"
exec 9>/tmp/ov13850-camera-session.lock
flock -n 9 || { echo 'ERROR: camera session locked' >&2; exit 1; }
command -v fuser >/dev/null
if fuser "$DEVICE" >/dev/null 2>&1; then
    echo 'ERROR: video device already in use' >&2
    exit 1
fi
[[ ! -e $OUTPUT ]] || { echo 'ERROR: output already exists' >&2; exit 1; }
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd)
trap 'rc=$?; printf "exit_code=%s\n" "$rc" > "$OUTPUT/status.txt"' EXIT
run()
{
    local name=$1
    shift
    printf 'RUN %s\n' "$name"
    timeout --kill-after=10s 180s "$@" >"$OUTPUT/$name.log" 2>&1
    printf 'PASS %s\n' "$name"
}
run configure "$LEARNING/scripts/configure_rkisp_1080p.sh"
run capture v4l2-ctl -d "$DEVICE" --stream-mmap=4 --stream-skip=10 --stream-count=1 \
    --stream-poll --stream-to="$OUTPUT/frame.nv12"
[[ $(stat -c %s "$OUTPUT/frame.nv12") == 3110400 ]]
run official-mpp bash "$LEARNING/mpp/tests/test_official_mpp_board.sh" "$MPP" "$OUTPUT/official"
run mpp-file bash "$LEARNING/mpp/tests/test_mpp_file_encoder.sh" "$MPP" "$OUTPUT/frame.nv12" "$OUTPUT/file.h264"
run mpp-parameters bash "$LEARNING/mpp/tests/test_mpp_parameters.sh" "$MPP" "$OUTPUT/frame.nv12" "$OUTPUT/parameters"
run mpp-live bash "$LEARNING/mpp/tests/test_mpp_live_encoder.sh" "$MPP" "$DEVICE" "$OUTPUT/live.h264"
run decode-h264 env LD_LIBRARY_PATH="$MPP/lib" "$MPP/bin/mpi_dec_test" \
    -i "$OUTPUT/file.h264" -t 7 -f 0 -o /dev/null
run decode-h265 env LD_LIBRARY_PATH="$MPP/lib" "$MPP/bin/mpi_dec_test" \
    -i "$OUTPUT/parameters/h265-cbr-8m.h265" -t 16777220 -f 0 -o /dev/null
run rga-file bash "$LEARNING/rga/tests/test_rga_nv12_resize.sh" "$RGA" "$OUTPUT/frame.nv12"
run rga-live bash "$LEARNING/rga/tests/test_rga_v4l2_live.sh" "$RGA" "$DEVICE" "$OUTPUT/resized.nv12"
run rtp "$ROOT/build/bin/v4l2_mpp_rtp_sender" --device "$DEVICE" --host 127.0.0.1 --frames 300
grep -q 'timeouts=0 dropped=0' "$OUTPUT/rtp.log"
grep -q 'queue_overruns=0' "$OUTPUT/rtp.log"
grep -q STREAM_RTP_OK "$OUTPUT/rtp.log"
run rtsp-recovery bash "$ROOT/tests/test_rtsp_recovery.sh" "$ROOT" "$DEVICE" 8554 /live
cat /sys/bus/i2c/devices/3-0010/power/runtime_status > "$OUTPUT/pm-status.txt"
cat /sys/bus/i2c/devices/3-0010/power/runtime_usage > "$OUTPUT/pm-usage.txt"
grep -qx suspended "$OUTPUT/pm-status.txt"
grep -qx 0 "$OUTPUT/pm-usage.txt"
echo BOARD_REGRESSION_OK
