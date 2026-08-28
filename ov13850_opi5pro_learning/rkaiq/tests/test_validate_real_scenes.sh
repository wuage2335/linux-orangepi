#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 1 ]] || { echo "usage: $0 <rkaiq-learning-root>" >&2; exit 2; }
ROOT=$(cd "$1" && pwd)
SCRIPT="$ROOT/scripts/validate_real_scenes.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/bin" "$TMP/bundle/bin"

cat > "$TMP/bin/v4l2-ctl" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
for arg in "$@"; do
	case $arg in
	--stream-to=*)
		file=${arg#--stream-to=}
		[[ $file == /dev/null ]] || truncate -s 3110400 "$file"
		;;
	--get-ctrl=*)
		printf 'exposure: 800\nanalogue_gain: 32\nvertical_blanking: 96\n'
		;;
	esac
done
printf '<<<<<<<< 30.00 fps\n'
MOCK
chmod +x "$TMP/bin/v4l2-ctl"

cat > "$TMP/bundle/bin/run_rkaiq_local.sh" <<'MOCK'
#!/usr/bin/env bash
trap 'exit 0' TERM INT
while :; do sleep 1; done
MOCK
chmod +x "$TMP/bundle/bin/run_rkaiq_local.sh"

cat > "$TMP/bundle/bin/capture_image_stats.sh" <<'MOCK'
#!/usr/bin/env bash
printf 'Y bytes=2073600 min=1 max=200 mean=80.000\n'
printf 'U bytes=518400 min=100 max=150 mean=128.000\n'
printf 'V bytes=518400 min=100 max=150 mean=127.500\n'
MOCK
chmod +x "$TMP/bundle/bin/capture_image_stats.sh"

cat > "$TMP/configure.sh" <<'MOCK'
#!/usr/bin/env bash
echo CONFIGURATION_OK
MOCK
chmod +x "$TMP/configure.sh"

[[ -x $SCRIPT ]] || { echo "FAIL: missing scene acceptance script" >&2; exit 1; }

printf '\n100\n\n110\n\n120\n' |
	PATH="$TMP/bin:$PATH" "$SCRIPT" \
		--bundle "$TMP/bundle" \
		--configure "$TMP/configure.sh" \
		--output "$TMP/out" \
		--settle-frames 2 \
		--skip-frames 1

for scene in bright normal dark; do
	[[ -s $TMP/out/$scene.nv12 ]] || { echo "FAIL: missing $scene frame" >&2; exit 1; }
	grep -F "$scene" "$TMP/out/summary.tsv" >/dev/null
	done

grep -F $'bright\t100' "$TMP/out/summary.tsv" >/dev/null
grep -F $'normal\t110' "$TMP/out/summary.tsv" >/dev/null
grep -F $'dark\t120' "$TMP/out/summary.tsv" >/dev/null
grep -F 'SCENE_ACCEPTANCE_CAPTURED=' "$TMP/out/result.txt" >/dev/null

echo "PASS: real-scene acceptance workflow"
