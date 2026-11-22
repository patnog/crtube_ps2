#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/crtube-edge264-mp4-test
MP4=${1:-${MP4:-}}

if [ -z "$MP4" ] || [ ! -f "$MP4" ]; then
    echo "usage: $0 path/to/video.mp4" >&2
    exit 2
fi

cd "$ROOT"
${HOST_CC:-cc} -std=gnu11 -O2 -flax-vector-conversions \
  -Wno-override-init -Wno-unused-function -Wno-return-type \
  -pthread -Iinclude -Ithird_party/edge264 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_h264_edge264_mp4.c -o "$OUT"
"$OUT" "$MP4"
