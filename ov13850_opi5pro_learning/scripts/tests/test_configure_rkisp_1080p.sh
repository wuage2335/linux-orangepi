#!/usr/bin/env bash

set -euo pipefail

TEST_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SCRIPT=$(cd "$TEST_DIR/.." && pwd)/configure_rkisp_1080p.sh
TEST_ROOT=$(mktemp -d)

cleanup()
{
	rm -rf "$TEST_ROOT"
}
trap cleanup EXIT

fail()
{
	printf 'FAIL: %s\n' "$*" >&2
	exit 1
}

assert_contains()
{
	local file=$1
	local expected=$2

	grep -Fq -- "$expected" "$file" ||
		fail "expected '$expected' in $file"
}

make_node()
{
	local node=$1
	local name=$2

	mkdir -p "$TEST_ROOT/sysfs/$node"
	printf '%s\n' "$name" > "$TEST_ROOT/sysfs/$node/name"
}

write_fake_v4l2_ctl()
{
	mkdir -p "$TEST_ROOT/bin"

	cat > "$TEST_ROOT/bin/v4l2-ctl" <<'FAKE'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >> "$V4L2_CTL_LOG"

case "$*" in
	*--get-fmt-video*)
		if [[ ${V4L2_FAKE_BAD_FORMAT:-0} == 1 ]]; then
			cat <<'OUT'
Width/Height      : 1280/720
Pixel Format      : 'NV12'
Bytes per Line    : 1280
Size Image        : 1382400
OUT
		else
			cat <<'OUT'
Width/Height      : 1920/1080
Pixel Format      : 'NV12'
Bytes per Line    : 1920
Size Image        : 3110400
OUT
		fi
		;;
	*--get-selection=target=crop*)
		cat <<'OUT'
Selection Video Capture: crop, Left 0, Top 190, Width 2112, Height 1188, Flags:
OUT
		;;
esac
FAKE

	chmod +x "$TEST_ROOT/bin/v4l2-ctl"
}

setup_valid_fixture()
{
	rm -rf "$TEST_ROOT/sysfs"
	mkdir -p "$TEST_ROOT/sysfs"
	make_node v4l-subdev2 'ov13850_i2c_min 3-0010'
	make_node v4l-subdev1 'rockchip-csi2-dphy0'
	make_node v4l-subdev0 'rockchip-mipi-csi2'
	make_node v4l-subdev4 'rkcif-mipi-lvds'
	make_node video11 'rkisp_mainpath'
	: > "$TEST_ROOT/v4l2.log"
}

run_script()
{
	VIDEO4LINUX_SYSFS_ROOT="$TEST_ROOT/sysfs" \
	V4L2_CTL_BIN="$TEST_ROOT/bin/v4l2-ctl" \
	V4L2_CTL_LOG="$TEST_ROOT/v4l2.log" \
	bash "$SCRIPT" > "$TEST_ROOT/stdout" 2> "$TEST_ROOT/stderr"
}

[[ -f $SCRIPT ]] || fail "production script does not exist: $SCRIPT"
write_fake_v4l2_ctl

setup_valid_fixture
run_script
assert_contains "$TEST_ROOT/stdout" 'CONFIGURATION_OK'
assert_contains "$TEST_ROOT/v4l2.log" '--set-subdev-fmt pad=0,width=2112,height=1568,code=0x3007'
assert_contains "$TEST_ROOT/v4l2.log" '--set-fmt-video=width=1920,height=1080,pixelformat=NV12'
assert_contains "$TEST_ROOT/v4l2.log" '--set-selection=target=crop,left=0,top=190,width=2112,height=1188'

rm -f "$TEST_ROOT/sysfs/v4l-subdev2/name"
if run_script; then
	fail 'missing sensor unexpectedly succeeded'
fi
assert_contains "$TEST_ROOT/stderr" 'ERROR:'
assert_contains "$TEST_ROOT/stderr" 'Sensor'

setup_valid_fixture
make_node video12 'rkisp_mainpath'
if run_script; then
	fail 'duplicate mainpath unexpectedly succeeded'
fi
assert_contains "$TEST_ROOT/stderr" 'ERROR:'
assert_contains "$TEST_ROOT/stderr" 'mainpath'

setup_valid_fixture
if V4L2_FAKE_BAD_FORMAT=1 run_script; then
	fail 'bad readback unexpectedly succeeded'
fi
assert_contains "$TEST_ROOT/stderr" 'ERROR:'
assert_contains "$TEST_ROOT/stderr" 'format readback'

setup_valid_fixture
run_script
run_script
assert_contains "$TEST_ROOT/stdout" 'CONFIGURATION_OK'

printf 'PASS: configure_rkisp_1080p tests\n'
