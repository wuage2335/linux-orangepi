#!/usr/bin/env bash
set -euo pipefail

usage()
{
	echo "usage: $0 INPUT.h264 WINDOWS_IP [PORT]" >&2
	exit 2
}

[[ $# -eq 2 || $# -eq 3 ]] || usage

INPUT=$1
HOST=$2
PORT=${3:-5004}

[[ -s "$INPUT" ]] || {
	echo "ERROR: input H.264 file is missing or empty: $INPUT" >&2
	exit 1
}

if [[ ! "$HOST" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
	echo "ERROR: WINDOWS_IP must be an IPv4 address" >&2
	exit 2
fi

IFS=. read -r o1 o2 o3 o4 <<<"$HOST"
for octet in "$o1" "$o2" "$o3" "$o4"; do
	((octet >= 0 && octet <= 255)) || {
		echo "ERROR: invalid IPv4 address: $HOST" >&2
		exit 2
	}
done

if [[ ! "$PORT" =~ ^[0-9]+$ ]] || ((PORT < 1 || PORT > 65535)); then
	echo "ERROR: PORT must be in the range 1..65535" >&2
	exit 2
fi

command -v gst-launch-1.0 >/dev/null 2>&1 || {
	echo "ERROR: gst-launch-1.0 is not installed" >&2
	exit 1
}

echo "input=$INPUT"
echo "destination=$HOST:$PORT"
echo "payload_type=96 clock_rate=90000 mtu=1200"

exec gst-launch-1.0 -v \
	filesrc location="$INPUT" \
	! h264parse \
	! 'video/x-h264,stream-format=byte-stream,framerate=30/1' \
	! rtph264pay pt=96 mtu=1200 config-interval=1 \
	! udpsink host="$HOST" port="$PORT" sync=true async=false
