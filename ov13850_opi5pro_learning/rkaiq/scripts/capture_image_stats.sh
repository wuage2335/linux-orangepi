#!/usr/bin/env bash
set -euo pipefail

usage()
{
	echo "usage: $0 --width N --height N <frame.nv12>" >&2
	exit 2
}

WIDTH=
HEIGHT=
while [[ $# -gt 1 ]]; do
	case $1 in
	--width) WIDTH=$2 ;;
	--height) HEIGHT=$2 ;;
	*) break ;;
	esac
	shift 2
done

[[ $# -eq 1 && $WIDTH =~ ^[1-9][0-9]*$ && $HEIGHT =~ ^[1-9][0-9]*$ ]] || usage
[[ $((WIDTH % 2)) -eq 0 && $((HEIGHT % 2)) -eq 0 ]] || {
	echo "ERROR: NV12 width and height must be even" >&2
	exit 1
}

FILE=$1
Y_SIZE=$((WIDTH * HEIGHT))
UV_SIZE=$((Y_SIZE / 2))
EXPECTED=$((Y_SIZE + UV_SIZE))
[[ -f $FILE ]] || { echo "ERROR: missing frame: $FILE" >&2; exit 1; }
[[ $(stat -c '%s' "$FILE") -eq $EXPECTED ]] || {
	echo "ERROR: expected $EXPECTED bytes for ${WIDTH}x${HEIGHT} NV12" >&2
	exit 1
}

head -c "$Y_SIZE" "$FILE" | od -An -tu1 -v |
awk '
{
	for (i = 1; i <= NF; ++i) {
		v = $i + 0
		if (!n || v < min) min = v
		if (!n || v > max) max = v
		sum += v
		++n
	}
}
END { printf "Y bytes=%d min=%d max=%d mean=%.3f\n", n, min, max, sum / n }
'

tail -c "$UV_SIZE" "$FILE" | od -An -tu1 -v |
awk '
{
	for (i = 1; i <= NF; ++i) {
		v = $i + 0
		if (n % 2 == 0) {
			if (!un || v < umin) umin = v
			if (!un || v > umax) umax = v
			usum += v
			++un
		} else {
			if (!vn || v < vmin) vmin = v
			if (!vn || v > vmax) vmax = v
			vsum += v
			++vn
		}
		++n
	}
}
END {
	printf "U bytes=%d min=%d max=%d mean=%.3f\n", un, umin, umax, usum / un
	printf "V bytes=%d min=%d max=%d mean=%.3f\n", vn, vmin, vmax, vsum / vn
}
'
