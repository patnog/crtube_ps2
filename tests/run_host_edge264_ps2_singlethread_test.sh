#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/crtube-edge264-ps2.XXXXXX")
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cd "$ROOT"

${HOST_CC:-cc} -std=gnu11 -O2 -flax-vector-conversions \
  -Wno-override-init -Wno-unused-function -Wno-return-type \
  -DSIMD=6 -DEDGE264_PS2 -DEDGE264_SINGLE_THREAD \
  -Iinclude -Ithird_party/edge264 -Ithird_party/edge264/ps2/include \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  third_party/edge264/ps2/edge264_ps2_builtins.c \
  tests/test_h264_edge264_adapter.c -o "$TMP/adapter-test"

base64 -d tests/data/edge264-bumping.264.b64 > "$TMP/bumping.264"
"$TMP/adapter-test" "$TMP/bumping.264"

${HOST_CC:-cc} -std=c99 -O2 -fno-builtin -Wall -Wextra -Werror \
  tests/test_edge264_ps2_builtins.c \
  third_party/edge264/ps2/edge264_ps2_builtins.c -o "$TMP/builtins-test"
"$TMP/builtins-test"
echo "edge264 PS2 single-thread compatibility: PASS"
