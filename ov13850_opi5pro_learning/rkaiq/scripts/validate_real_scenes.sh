#!/usr/bin/env bash
set -euo pipefail

usage()
{
	cat >&2 <<'EOF'
usage: validate_real_scenes.sh --bundle DIR --configure FILE [options]

Options:
  --output DIR          Result directory (default: timestamped directory)
  --sensor DEVICE       Sensor subdev (default: /dev/v4l-subdev2)
  --video DEVICE        RKISP mainpath (default: /dev/video11)
  --settle-frames N     Frames before each capture (default: 90)
  --skip-frames N       Frames skipped before saved frame (default: 15)
EOF
	exit 2
}

BUNDLE=
CONFIGURE=
OUTPUT=
SENSOR=/dev/v4l-subdev2
VIDEO=/dev/video11
SETTLE_FRAMES=90
SKIP_FRAMES=15

while [[ $# -gt 0 ]]; do
	case $1 in
	--bundle) BUNDLE=$2; shift 2 ;;
	--configure) CONFIGURE=$2; shift 2 ;;
	--output) OUTPUT=$2; shift 2 ;;
	--sensor) SENSOR=$2; shift 2 ;;
	--video) VIDEO=$2; shift 2 ;;
	--settle-frames) SETTLE_FRAMES=$2; shift 2 ;;
	--skip-frames) SKIP_FRAMES=$2; shift 2 ;;
	*) usage ;;
	esac
done

[[ -n $BUNDLE && -n $CONFIGURE ]] || usage
[[ $SETTLE_FRAMES =~ ^[1-9][0-9]*$ ]] || usage
[[ $SKIP_FRAMES =~ ^[0-9]+$ ]] || usage

BUNDLE=$(cd "$BUNDLE" && pwd)
CONFIGURE=$(readlink -f "$CONFIGURE")
RUNNER="$BUNDLE/bin/run_rkaiq_local.sh"
STATS="$BUNDLE/bin/capture_image_stats.sh"

[[ -x $RUNNER ]] || { echo "ERROR: missing runner: $RUNNER" >&2; exit 1; }
[[ -x $STATS ]] || { echo "ERROR: missing stats tool: $STATS" >&2; exit 1; }
[[ -x $CONFIGURE ]] || { echo "ERROR: configure script is not executable" >&2; exit 1; }
for command in v4l2-ctl sha256sum awk paste; do
	command -v "$command" >/dev/null || { echo "ERROR: missing $command" >&2; exit 1; }
done

if [[ -z $OUTPUT ]]; then
	OUTPUT="$PWD/stage6-real-scenes-$(date +%Y%m%d_%H%M%S)"
fi
[[ ! -e $OUTPUT ]] || { echo "ERROR: output already exists: $OUTPUT" >&2; exit 1; }
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd)

RKAIQ_PID=
cleanup()
{
	status=$?
	trap - EXIT INT TERM
	if [[ -n ${RKAIQ_PID:-} ]] && kill -0 "$RKAIQ_PID" 2>/dev/null; then
		kill "$RKAIQ_PID" 2>/dev/null || true
		wait "$RKAIQ_PID" 2>/dev/null || true
	fi
	v4l2-ctl -d "$SENSOR" \
		--set-ctrl=test_pattern=0,exposure=1536,analogue_gain=16,vertical_blanking=96 \
		>/dev/null 2>&1 || true
	exit "$status"
}
trap cleanup EXIT INT TERM

"$RUNNER" > "$OUTPUT/rkaiq.log" 2>&1 &
RKAIQ_PID=$!
sleep 3
kill -0 "$RKAIQ_PID" 2>/dev/null || {
	echo "ERROR: RKAIQ exited during startup" >&2
	tail -80 "$OUTPUT/rkaiq.log" >&2
	exit 1
}

"$CONFIGURE" > "$OUTPUT/configure.log" 2>&1
printf 'scene\tlatency_ms\tsha256\tcontrols\tstats\n' > "$OUTPUT/summary.tsv"

for scene in bright normal dark; do
	printf '\nPlace the camera in the %s scene, then press Enter.\n' "$scene"
	read -r _ || true

	v4l2-ctl -d "$VIDEO" \
		--stream-mmap=4 \
		--stream-count="$SETTLE_FRAMES" \
		--stream-to=/dev/null \
		--stream-poll > "$OUTPUT/$scene-settle.log" 2>&1

	v4l2-ctl -d "$SENSOR" \
		--get-ctrl=exposure,analogue_gain,vertical_blanking,test_pattern \
		> "$OUTPUT/$scene-controls.txt"

	v4l2-ctl -d "$VIDEO" \
		--stream-mmap=4 \
		--stream-skip="$SKIP_FRAMES" \
		--stream-count=1 \
		--stream-to="$OUTPUT/$scene.nv12" \
		--stream-poll > "$OUTPUT/$scene-capture.log" 2>&1

	"$STATS" --width 1920 --height 1080 "$OUTPUT/$scene.nv12" \
		> "$OUTPUT/$scene-stats.txt"
	sha=$(sha256sum "$OUTPUT/$scene.nv12" | awk '{print $1}')

	printf 'Observed same-screen latency in ms for %s (blank if unavailable): ' "$scene"
	read -r latency || latency=
	[[ -n $latency ]] || latency=unknown
	controls=$(paste -sd ';' "$OUTPUT/$scene-controls.txt")
	stats=$(paste -sd ';' "$OUTPUT/$scene-stats.txt")
	printf '%s\t%s\t%s\t%s\t%s\n' \
		"$scene" "$latency" "$sha" "$controls" "$stats" \
		>> "$OUTPUT/summary.tsv"
done

printf 'SCENE_ACCEPTANCE_CAPTURED=%s\n' "$OUTPUT" | tee "$OUTPUT/result.txt"
