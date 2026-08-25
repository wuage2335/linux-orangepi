#!/usr/bin/env bash
set -uo pipefail

VIDEO_DEVICE=${VIDEO_DEVICE:-/dev/video11}
MPP_DEVICE=${MPP_DEVICE:-/dev/mpp_service}
status=0

report_missing()
{
	echo "MISSING: $*" >&2
	status=1
}

echo "===== BOARD ====="
uname -m
uname -r

for command in gst-launch-1.0 gst-inspect-1.0 pkg-config g++; do
	if command -v "$command" >/dev/null 2>&1; then
		echo "COMMAND_OK=$command"
	else
		report_missing "command $command"
	fi
done

if command -v gst-launch-1.0 >/dev/null 2>&1; then
	echo "===== GSTREAMER ====="
	gst-launch-1.0 --version || report_missing "working gst-launch-1.0"
fi

echo "===== DEVICES ====="
for device in "$VIDEO_DEVICE" "$MPP_DEVICE"; do
	if [[ -e "$device" ]]; then
		echo "DEVICE_OK=$device"
	else
		report_missing "device $device"
	fi
done

echo "===== PLUGINS ====="
if command -v gst-inspect-1.0 >/dev/null 2>&1; then
	for plugin in appsrc h264parse rtph264pay udpsink \
		      udpsrc rtpjitterbuffer rtph264depay; do
		if gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
			echo "PLUGIN_OK=$plugin"
		else
			report_missing "GStreamer plugin $plugin"
		fi
	done
fi

echo "===== DEVELOPMENT PACKAGES ====="
if command -v pkg-config >/dev/null 2>&1; then
	for module in gstreamer-1.0 gstreamer-app-1.0 \
		      gstreamer-rtsp-server-1.0; do
		if version=$(pkg-config --modversion "$module" 2>/dev/null); then
			echo "PKG_CONFIG_OK=$module:$version"
		else
			report_missing "pkg-config module $module"
		fi
	done
fi

if command -v g++ >/dev/null 2>&1; then
	g++ --version | head -1
fi

if ((status)); then
	echo "GSTREAMER_BOARD_ENV=INCOMPLETE"
	exit 1
fi

echo "GSTREAMER_BOARD_ENV=OK"
