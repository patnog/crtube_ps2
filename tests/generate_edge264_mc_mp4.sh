#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$ROOT/tests/data/mc_coverage.mp4"}
FRAMES=${EDGE264_TEST_FRAMES:-180}
mkdir -p "$(dirname -- "$OUT")"
command -v ffmpeg >/dev/null 2>&1 || {
    echo "ERRO: ffmpeg não encontrado" >&2
    exit 2
}
ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i "testsrc2=size=320x240:rate=30:duration=6" \
  -vf "scroll=horizontal=0.07:vertical=0.035" -frames:v "$FRAMES" \
  -an -c:v libx264 -preset slow -pix_fmt yuv420p -profile:v high \
  -g 30 -keyint_min 30 -sc_threshold 0 -bf 2 -refs 3 \
  -x264-params "me=umh:subme=9:partitions=all:weightp=2:weightb=1" \
  "$OUT"
echo "MP4 gerado: $OUT"
