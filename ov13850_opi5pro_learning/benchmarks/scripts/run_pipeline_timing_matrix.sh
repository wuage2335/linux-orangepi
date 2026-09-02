#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 || $# -eq 2 ]] || {
	echo "usage: $0 RESULT_DIR [RTP_HOST]" >&2
	exit 2
}

RESULT_DIR=$1
RTP_HOST=${2:-127.0.0.1}
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BENCHMARK_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
LEARNING_ROOT=$(cd "$BENCHMARK_ROOT/.." && pwd)
BENCHMARK="$BENCHMARK_ROOT/build/pipeline_stage_benchmark"
SAMPLER="$SCRIPT_DIR/sample_rkisp_proc.py"
CONFIGURE="$LEARNING_ROOT/scripts/configure_rkisp_1080p.sh"
RGA="$LEARNING_ROOT/rga/build/rga_nv12_resize/bin/rga_v4l2_live"
RGA_LIB="$LEARNING_ROOT/rga/build/rga_nv12_resize/lib"
SENSOR=${SENSOR_DEVICE:-/dev/v4l-subdev2}
VIDEO=${VIDEO_DEVICE:-/dev/video11}
MPP_BUNDLE=${MPP_BUNDLE:?MPP_BUNDLE must name the official MPP bundle}

[[ ! -e $RESULT_DIR ]] || {
	echo "ERROR: result path already exists: $RESULT_DIR" >&2
	exit 1
}
mkdir -p "$RESULT_DIR"
RESULT_DIR=$(cd "$RESULT_DIR" && pwd)

for file in "$BENCHMARK" "$SAMPLER" "$CONFIGURE" "$RGA"; do
	[[ -e $file ]] || { echo "ERROR: missing $file" >&2; exit 1; }
done

cleanup()
{
	trap - EXIT INT TERM
	v4l2-ctl -d "$SENSOR" --set-ctrl=test_pattern=0 >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

{
	date --iso-8601=seconds
	uname -a
	v4l2-ctl -d "$VIDEO" --all
	cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || true
	grep . /sys/devices/system/cpu/cpufreq/policy*/scaling_governor 2>/dev/null || true
} > "$RESULT_DIR/environment.txt" 2>&1

"$CONFIGURE" > "$RESULT_DIR/configure.log" 2>&1
v4l2-ctl -d "$SENSOR" \
	--set-ctrl=test_pattern=1,exposure=1000,analogue_gain=16,vertical_blanking=96 \
	> "$RESULT_DIR/fixed-controls-set.log" 2>&1
v4l2-ctl -d "$SENSOR" \
	--get-ctrl=test_pattern,exposure,analogue_gain,vertical_blanking \
	> "$RESULT_DIR/fixed-controls-readback.log"

run_pipeline_group()
{
	local label=$1
	local mode=$2
	local sink=$3
	local run

	for run in 1 2 3 4 5; do
		local prefix="$RESULT_DIR/${label}-run${run}"
		timeout 20 python3 "$SAMPLER" \
			--frames 300 --output "$prefix-rkisp.csv" &
		local sampler_pid=$!
		/usr/bin/time -v -o "$prefix-time.txt" \
			env LD_LIBRARY_PATH="$MPP_BUNDLE/lib" \
			"$BENCHMARK" \
			--device "$VIDEO" --mode "$mode" --sink "$sink" \
			--host "$RTP_HOST" --port 5004 \
			--warmup 30 --frames 300 --bitrate 8000000 --gop 30 \
			--mtu 1200 --queue-buffers 2 --csv "$prefix-frames.csv" \
			> "$prefix-summary.log" 2>&1
		wait "$sampler_pid"
	done
}

run_pipeline_group copy-null copy null
run_pipeline_group dmabuf-null dmabuf null
run_pipeline_group dmabuf-rtp dmabuf rtp

for mode in copy direct; do
	for run in 1 2 3 4 5; do
		prefix="$RESULT_DIR/rga-${mode}-run${run}"
		option=()
		[[ $mode == direct ]] && option=(--direct)
		/usr/bin/time -v -o "$prefix-time.txt" \
			env LD_LIBRARY_PATH="$RGA_LIB" \
			"$RGA" "${option[@]}" "$VIDEO" "$prefix-output.nv12" \
			> "$prefix-summary.log" 2>&1
	done
done

v4l2-ctl -d "$SENSOR" \
	--get-ctrl=test_pattern,exposure,analogue_gain,vertical_blanking \
	> "$RESULT_DIR/final-controls.txt"
cat "/sys/bus/i2c/devices/3-0010/power/runtime_status" \
	> "$RESULT_DIR/runtime_status.txt" 2>&1 || true
cat "/sys/bus/i2c/devices/3-0010/power/runtime_usage" \
	> "$RESULT_DIR/runtime_usage.txt" 2>&1 || true

echo "PIPELINE_TIMING_MATRIX_OK result=$RESULT_DIR"
