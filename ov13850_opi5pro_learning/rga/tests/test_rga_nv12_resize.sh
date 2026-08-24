#!/usr/bin/env bash

set -euo pipefail

[[ $# -eq 2 ]] || {
    printf 'usage: %s <bundle-dir> <1920x1080-nv12>\n' "$0" >&2
    exit 2
}

BUNDLE=$(cd "$1" && pwd)
INPUT=$2
BIN="$BUNDLE/bin/rga_nv12_resize"
LIB="$BUNDLE/lib/librga.so"
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

expect_failure()
{
    local name=$1
    shift

    if "$BIN" "$@" \
        >"$TEST_ROOT/$name.stdout" \
        2>"$TEST_ROOT/$name.stderr"
    then
        fail "$name unexpectedly succeeded"
    fi

    grep -Fq 'ERROR:' "$TEST_ROOT/$name.stderr" ||
        fail "$name did not report ERROR:"
}

[[ -x $BIN ]] ||
    fail "missing executable: $BIN"

[[ -f $LIB ]] ||
    fail "missing bundled library: $LIB"

[[ -r $INPUT ]] ||
    fail "input is not readable: $INPUT"
[[ $(stat -c '%s' "$INPUT") -eq 3110400 ]] ||
    fail 'input size is not 3110400 bytes'

file "$BIN" |
    grep -Fq 'ARM aarch64' ||
    fail 'binary is not aarch64'

file "$LIB" |
    grep -Fq 'ARM aarch64' ||
    fail 'librga is not aarch64'

RESOLVED_LIB=$(ldd "$BIN" |
    awk '$1 == "librga.so" && $2 == "=>" { print $3; exit }')

[[ -n $RESOLVED_LIB ]] ||
    fail 'ldd did not report a resolved librga.so'

EXPECTED_LIB=$(readlink -f "$LIB")
RESOLVED_LIB=$(readlink -f "$RESOLVED_LIB")

[[ $RESOLVED_LIB == "$EXPECTED_LIB" ]] ||
    fail "binary resolved librga.so to $RESOLVED_LIB, expected $EXPECTED_LIB"

expect_failure no_args

expect_failure missing_input \
    "$TEST_ROOT/missing.nv12" \
    "$TEST_ROOT/missing-output.nv12"

dd if=/dev/zero \
    of="$TEST_ROOT/short.nv12" \
    bs=1024 \
    count=1 \
    status=none

expect_failure short_input \
    "$TEST_ROOT/short.nv12" \
    "$TEST_ROOT/short-output.nv12"

[[ ! -e $TEST_ROOT/short-output.nv12 ]] ||
    fail 'short-input failure left an output file'

cp "$INPUT" "$TEST_ROOT/same-path.nv12"
expect_failure same_path \
    "$TEST_ROOT/same-path.nv12" \
    "$TEST_ROOT/same-path.nv12"

[[ $(stat -c '%s' "$TEST_ROOT/same-path.nv12") -eq 3110400 ]] ||
    fail 'same-path failure damaged the input file'

OUTPUT_A="$TEST_ROOT/output-a.nv12"
OUTPUT_B="$TEST_ROOT/output-b.nv12"

"$BIN" "$INPUT" "$OUTPUT_A" |
    tee "$TEST_ROOT/run-a.log"

"$BIN" "$INPUT" "$OUTPUT_B" |
    tee "$TEST_ROOT/run-b.log"

for output in "$OUTPUT_A" "$OUTPUT_B"; do
    [[ $(stat -c '%s' "$output") -eq 1382400 ]] ||
        fail "output size is not 1382400 bytes: $output"
done

cmp -s "$OUTPUT_A" "$OUTPUT_B" ||
    fail 'repeated output differs'

od -An -tu1 -v "$OUTPUT_A" |
awk '
{
    for (i = 1; i <= NF; i++) {
        if (($i + 0) != 0)
            nonzero = 1
    }
}
END {
    exit nonzero ? 0 : 1
}
' || fail 'output is all zero'

grep -Fq 'librga=' "$TEST_ROOT/run-a.log" ||
    fail 'missing librga version'

grep -Fq 'warmups=5' "$TEST_ROOT/run-a.log" ||
    fail 'missing warmup count'

grep -Fq 'iterations=100' "$TEST_ROOT/run-a.log" ||
    fail 'missing iteration count'

grep -Eq 'total_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/run-a.log" ||
    fail 'missing total timing'

grep -Eq 'average_us=[0-9]+([.][0-9]+)?' "$TEST_ROOT/run-a.log" ||
    fail 'missing average timing'

grep -Eq 'operations_per_second=[0-9]+([.][0-9]+)?' \
    "$TEST_ROOT/run-a.log" ||
    fail 'missing throughput estimate'

grep -Fq 'RGA_RESIZE_OK' "$TEST_ROOT/run-a.log" ||
    fail 'missing success marker'

sha256sum "$OUTPUT_A"

printf 'PASS: RGA NV12 resize tests\n'
