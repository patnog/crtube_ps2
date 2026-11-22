#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=${TMPDIR:-/tmp}/crtube-r5900-tests-$$
trap 'rm -f "$TMP-mmi" "$TMP-vector" "$TMP-mask"' EXIT HUP INT TERM
CC=${CC:-cc}
CFLAGS="-std=gnu11 -O1 -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-parentheses -flax-vector-conversions"
$CC -std=c99 -O2 -Wall -Wextra -Werror \
  "$ROOT/tests/test_r5900_mmi.c" \
  "$ROOT/third_party/edge264/ps2/edge264_r5900_mmi.c" -o "$TMP-mmi"
"$TMP-mmi"
$CC $CFLAGS -DSIMD=6 -I"$ROOT/third_party/edge264/src" \
  "$ROOT/tests/test_r5900_vector_ops.c" -o "$TMP-vector"
"$TMP-vector"
$CC $CFLAGS -DSIMD=6 -I"$ROOT/third_party/edge264/src" \
  "$ROOT/tests/test_r5900_vector_semantics.c" -o "$TMP-mask"
"$TMP-mask"
echo "R5900 host vector test suite: PASS"
