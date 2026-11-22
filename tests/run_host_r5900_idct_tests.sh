#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-cc}
OUT="${TMPDIR:-/tmp}/crtube-r5900-idct-test"
"$CC" -std=gnu11 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-parentheses -flax-vector-conversions "$ROOT/tests/test_r5900_idct_kernels.c" -o "$OUT"
"$OUT"
rm -f "$OUT"
