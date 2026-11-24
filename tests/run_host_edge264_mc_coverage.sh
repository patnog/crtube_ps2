#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/crtube-edge264-mc-coverage
WORK=${TMPDIR:-/tmp}/crtube-edge264-mc-test
TRACE=${EDGE264_TRACE_LOG:-"$WORK/edge264_mc_trace.log"}
FRAMES=${EDGE264_TEST_FRAMES:-180}
mkdir -p "$WORK"
command -v ffmpeg >/dev/null 2>&1 || { echo "ERRO: ffmpeg não encontrado" >&2; exit 2; }
command -v awk >/dev/null 2>&1 || exit 2
cd "$ROOT"
CC=${HOST_CC:-cc}

# Mesmo gerador usado pelo ELF embarcado: movimento diagonal e MVs fracionários.
EDGE264_TEST_FRAMES="$FRAMES" "$ROOT/tests/generate_edge264_mc_mp4.sh" \
  "$WORK/mc_coverage.mp4"

"$CC" -std=gnu11 -O2 -flax-vector-conversions \
  -Wno-override-init -Wno-unused-function -Wno-return-type -pthread \
  -Iinclude -Ithird_party/edge264 -DSIMD=6 -DEDGE264_PS2 \
  -DEDGE264_TRACE_INTER_LUMA -DEDGE264_TRACE_INTER_LUMA_LIMIT=100000 \
  -DEDGE264_TRACE_INTER_CHROMA -DEDGE264_TRACE_INTER_CHROMA_LIMIT=100000 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_h264_edge264_plane_crc.c -o "$OUT"

echo "Arquivo: $WORK/mc_coverage.mp4"
echo "Trace: $TRACE"
"$OUT" "$WORK/mc_coverage.mp4" >"$WORK/decoder_output.log" 2>"$TRACE"
echo "Decoder: $WORK/decoder_output.log"
echo
echo "=== Cobertura MC ==="
awk '
/EDGE264_LUMA_TRACE call=/ {
  calls++
  if (match($0,/frac=[0-9]+,[0-9]+/)) {
    f=substr($0,RSTART+5,RLENGTH-5); split(f,a,",")
    if (a[1] != 0 || a[2] != 0) frac++
    if (a[1] != 0 && a[2] == 0) horiz++
    if (a[1] == 0 && a[2] != 0) vert++
    if (a[1] != 0 && a[2] != 0) diag++
  }
  if (match($0,/wh=[0-9]+,[0-9]+/)) {v=substr($0,RSTART+3,RLENGTH-3); blocks[v]++}
  if (match($0,/wod=[^ ]+/)) {v=substr($0,RSTART+4,RLENGTH-4); weights[v]++}
}
/EDGE264_CHROMA_TRACE call=/ {
  ccalls++
  if (match($0,/frac=[0-9]+,[0-9]+/)) {
    f=substr($0,RSTART+5,RLENGTH-5); split(f,a,",")
    if (a[1] != 0 || a[2] != 0) cfrac++
  }
}
END {
  printf "luma calls=%d fractional=%d horizontal=%d vertical=%d diagonal=%d\n",calls,frac,horiz,vert,diag
  printf "chroma calls=%d fractional=%d\n",ccalls,cfrac
  printf "block dimensions (w,h):\n"
  for (v in blocks) printf "  %s: %d\n",v,blocks[v]
  printf "weighted-prediction signatures (wod): %d\n",length(weights)
  if (!calls || !ccalls || !frac || !cfrac) {
    print "FAIL: cobertura fracionária insuficiente; consulte o trace e ajuste o padrão de movimento."
    exit 1
  }
  print "PASS: luma e chroma fracionários foram exercitados."
}' "$TRACE"
echo "Trace completo salvo em: $TRACE"
