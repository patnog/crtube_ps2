#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MP4=${1:-${MP4:-}}
OUT=${TMPDIR:-/tmp}/crtube-edge264-luma-trace
if [ -z "$MP4" ] || [ ! -f "$MP4" ]; then
    echo "usage: $0 path/to/240p-baseline.mp4" >&2
    exit 2
fi
cd "$ROOT"
CC=${HOST_CC:-cc}
TRACE_LIMIT=${EDGE264_TRACE_LIMIT:-512}
"$CC" -std=gnu11 -O2 -flax-vector-conversions \
    -Wno-override-init -Wno-unused-function -Wno-return-type -pthread \
    -Iinclude -Ithird_party/edge264 -DSIMD=6 -DEDGE264_PS2 \
    -DEDGE264_TRACE_INTER_LUMA \
    -DEDGE264_TRACE_INTER_LUMA_LIMIT="$TRACE_LIMIT" \
    src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
    tests/test_h264_edge264_plane_crc.c -o "$OUT"
echo "Running SIMD=6 luma trace; first $TRACE_LIMIT inter-luma calls go to stderr" >&2
"$OUT" "$MP4"
