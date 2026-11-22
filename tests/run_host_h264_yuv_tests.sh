#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/crtube-h264-yuv-test
${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
    "$ROOT/src/h264_yuv.c" "$ROOT/tests/test_h264_yuv.c" -o "$OUT"
"$OUT"
