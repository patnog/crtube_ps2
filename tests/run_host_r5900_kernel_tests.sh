#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=${TMPDIR:-/tmp}/crtube-r5900-kernels-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
CC=${CC:-cc}
for name in intra inter mvpred deblock residual; do
  "$CC" -std=gnu11 -O2 -w -flax-vector-conversions -ffunction-sections -fdata-sections \
    -DSIMD=6 -I"$ROOT/third_party/edge264/src" \
    "$ROOT/tests/test_r5900_${name}_kernels.c" -Wl,--gc-sections -o "$TMP/$name"
  "$TMP/$name"
done
