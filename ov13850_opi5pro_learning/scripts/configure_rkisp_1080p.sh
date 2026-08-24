#!/usr/bin/env bash

set -euo pipefail

VIDEO4LINUX_SYSFS_ROOT=${VIDEO4LINUX_SYSFS_ROOT:-/sys/class/video4linux}
V4L2_CTL_BIN=${V4L2_CTL_BIN:-v4l2-ctl}

RAW_WIDTH=2112
RAW_HEIGHT=1568
RAW_CODE=0x3007
CROP_LEFT=0
CROP_TOP=190
CROP_WIDTH=2112
CROP_HEIGHT=1188
OUTPUT_WIDTH=1920
OUTPUT_HEIGHT=1080
OUTPUT_FORMAT=NV12
OUTPUT_STRIDE=1920
OUTPUT_SIZE=3110400

error()
{
	printf 'ERROR: %s\n' "$*" >&2
	exit 1
}

require_v4l2_ctl()
{
	if [[ $V4L2_CTL_BIN == */* ]]; then
		[[ -x $V4L2_CTL_BIN ]] ||
			error "v4l2-ctl is not executable: $V4L2_CTL_BIN"
	else
		command -v "$V4L2_CTL_BIN" >/dev/null 2>&1 ||
			error "command not found: $V4L2_CTL_BIN"
	fi
}

find_unique_node()
{
	local role=$1
	local expected=$2
	local match_mode=$3
	local name_file
	local node_name
	local -a matches=()

	shopt -s nullglob
	for name_file in "$VIDEO4LINUX_SYSFS_ROOT"/*/name; do
		IFS= read -r node_name < "$name_file"

		case $match_mode in
		exact)
			[[ $node_name == "$expected" ]] || continue
			;;
		contains)
			[[ $node_name == *"$expected"* ]] || continue
			;;
		*)
			error "internal match mode error for $role: $match_mode"
			;;
		esac

		matches+=("/dev/$(basename "$(dirname "$name_file")")")
	done
	shopt -u nullglob

	if (( ${#matches[@]} == 0 )); then
		error "$role node not found (name: $expected)"
	fi

	if (( ${#matches[@]} > 1 )); then
		error "$role node is ambiguous (${#matches[@]} matches for: $expected)"
	fi

	printf '%s\n' "${matches[0]}"
}

set_subdev_raw_format()
{
	local role=$1
	local node=$2

	printf 'Configure %-12s %s -> %sx%s SBGGR10\n' \
		"$role" "$node" "$RAW_WIDTH" "$RAW_HEIGHT"

	"$V4L2_CTL_BIN" -d "$node" \
		--set-subdev-fmt \
		"pad=0,width=$RAW_WIDTH,height=$RAW_HEIGHT,code=$RAW_CODE" ||
		error "failed to configure $role node: $node"
}

validate_readback()
{
	local mainpath_node=$1
	local format_output
	local crop_output

	format_output=$("$V4L2_CTL_BIN" -d "$mainpath_node" --get-fmt-video) ||
		error "failed to read mainpath format: $mainpath_node"
	crop_output=$("$V4L2_CTL_BIN" -d "$mainpath_node" \
		--get-selection=target=crop) ||
		error "failed to read mainpath crop: $mainpath_node"

	grep -Eq "Width/Height[[:space:]]*:[[:space:]]*$OUTPUT_WIDTH/$OUTPUT_HEIGHT" \
		<<< "$format_output" || error "mainpath format readback width/height mismatch"
	grep -Eq "Pixel Format[[:space:]]*:[[:space:]]*'$OUTPUT_FORMAT'" \
		<<< "$format_output" || error "mainpath format readback pixel format mismatch"
	grep -Eq "Bytes per Line[[:space:]]*:[[:space:]]*$OUTPUT_STRIDE" \
		<<< "$format_output" || error "mainpath format readback stride mismatch"
	grep -Eq "Size Image[[:space:]]*:[[:space:]]*$OUTPUT_SIZE" \
		<<< "$format_output" || error "mainpath format readback size mismatch"
	grep -Eq "Left[[:space:]]+$CROP_LEFT, Top[[:space:]]+$CROP_TOP, Width[[:space:]]+$CROP_WIDTH, Height[[:space:]]+$CROP_HEIGHT" \
		<<< "$crop_output" || error "mainpath crop readback mismatch"

	printf 'Format OK: %sx%s %s stride=%s size=%s\n' \
		"$OUTPUT_WIDTH" "$OUTPUT_HEIGHT" "$OUTPUT_FORMAT" \
		"$OUTPUT_STRIDE" "$OUTPUT_SIZE"
	printf 'Crop OK: left=%s top=%s width=%s height=%s\n' \
		"$CROP_LEFT" "$CROP_TOP" "$CROP_WIDTH" "$CROP_HEIGHT"
}

main()
{
	local sensor_node
	local dphy_node
	local csi2_node
	local cif_node
	local mainpath_node

	require_v4l2_ctl
	[[ -d $VIDEO4LINUX_SYSFS_ROOT ]] ||
		error "video4linux sysfs root not found: $VIDEO4LINUX_SYSFS_ROOT"

	sensor_node=$(find_unique_node Sensor ov13850_i2c_min contains)
	dphy_node=$(find_unique_node D-PHY rockchip-csi2-dphy0 exact)
	csi2_node=$(find_unique_node CSI-2 rockchip-mipi-csi2 exact)
	cif_node=$(find_unique_node CIF rkcif-mipi-lvds exact)
	mainpath_node=$(find_unique_node mainpath rkisp_mainpath exact)

	printf 'Sensor:   %s\n' "$sensor_node"
	printf 'D-PHY:    %s\n' "$dphy_node"
	printf 'CSI-2:    %s\n' "$csi2_node"
	printf 'CIF:      %s\n' "$cif_node"
	printf 'mainpath: %s\n' "$mainpath_node"

	set_subdev_raw_format Sensor "$sensor_node"
	set_subdev_raw_format D-PHY "$dphy_node"
	set_subdev_raw_format CSI-2 "$csi2_node"
	set_subdev_raw_format CIF "$cif_node"

	printf 'Configure mainpath %s -> %sx%s %s\n' \
		"$mainpath_node" "$OUTPUT_WIDTH" "$OUTPUT_HEIGHT" "$OUTPUT_FORMAT"
	"$V4L2_CTL_BIN" -d "$mainpath_node" \
		"--set-fmt-video=width=$OUTPUT_WIDTH,height=$OUTPUT_HEIGHT,pixelformat=$OUTPUT_FORMAT" ||
		error "failed to configure mainpath format: $mainpath_node"

	printf 'Configure crop left=%s top=%s width=%s height=%s\n' \
		"$CROP_LEFT" "$CROP_TOP" "$CROP_WIDTH" "$CROP_HEIGHT"
	"$V4L2_CTL_BIN" -d "$mainpath_node" \
		"--set-selection=target=crop,left=$CROP_LEFT,top=$CROP_TOP,width=$CROP_WIDTH,height=$CROP_HEIGHT" ||
		error "failed to configure mainpath crop: $mainpath_node"

	validate_readback "$mainpath_node"
	printf 'CONFIGURATION_OK\n'
}

main "$@"
