#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/crtube-edge264-adapter-test
cd "$ROOT"

${HOST_CC:-cc} -std=gnu11 -O2 -flax-vector-conversions \
  -Wno-override-init -Wno-unused-function -Wno-return-type \
  -pthread -Iinclude -Ithird_party/edge264 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_h264_edge264_adapter.c -o "$OUT"

FIXTURE=${TMPDIR:-/tmp}/crtube-edge264-bumping.264
base64 -d tests/data/edge264-bumping.264.b64 > "$FIXTURE"
"$OUT" "$FIXTURE"
