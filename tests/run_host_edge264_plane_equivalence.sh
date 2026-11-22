#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MP4=${1:-}
OUT=${TMPDIR:-/tmp}/crtube-edge264-plane-equivalence
GENERIC_BIN="$OUT/edge264_generic_dump"
R5900_BIN="$OUT/edge264_r5900_lane_dump"
GENERIC_RAW="$OUT/edge264_generic.yuv"
R5900_RAW="$OUT/edge264_r5900_lane.yuv"
REFERENCE_RAW="$OUT/ffmpeg_reference.yuv"
mkdir -p "$OUT"
if [ -z "$MP4" ] || [ ! -f "$MP4" ]; then
    echo "usage: $0 path/to/mc_coverage.mp4" >&2
    exit 2
fi
command -v ffmpeg >/dev/null 2>&1 || { echo "ffmpeg is required" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "python3 is required" >&2; exit 2; }
cd "$ROOT"
COMMON="-std=gnu11 -O2 -flax-vector-conversions -Wno-override-init -Wno-unused-function -Wno-return-type -pthread -Iinclude -Ithird_party/edge264"
CC=${HOST_CC:-cc}
# Generic Edge264 build.
$CC $COMMON src/h264_edge264.c src/h264_mp4.c \
  third_party/edge264/src/edge264.c tests/test_h264_edge264_plane_dump.c \
  -o "$GENERIC_BIN"
# SIMD=6 host build exercises the R5900 lane-selection code with host fallbacks;
# it does not execute EE MMI instructions.
$CC $COMMON -DSIMD=6 -DEDGE264_PS2 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_h264_edge264_plane_dump.c -o "$R5900_BIN"
"$GENERIC_BIN" "$MP4" "$GENERIC_RAW"
"$R5900_BIN" "$MP4" "$R5900_RAW"
ffmpeg -v error -i "$MP4" -map 0:v:0 -vsync 0 -pix_fmt yuv420p \
  -f rawvideo "$REFERENCE_RAW" -y
WIDTH=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$MP4")
HEIGHT=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$MP4")
python3 tests/compare_h264_edge264_planes.py "$WIDTH" "$HEIGHT" \
  "$REFERENCE_RAW" "$GENERIC_RAW" "$R5900_RAW"
echo "Raw planes saved in: $OUT"
