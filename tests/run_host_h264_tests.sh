#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}/crtube-h264-p-slice-test
RBSP_OUT=${TMPDIR:-/tmp}/crtube-h264-rbsp-test
cd "$ROOT"
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_p_slice.c \
  src/h264_p_slice.c src/h264_p_mb.c src/h264_p_partition.c src/h264_p_residual.c \
  src/h264_p_recon.c src/h264_p_decode.c src/h264_motion.c \
  src/h264_frame.c src/h264_recon.c src/h264_chroma.c src/h264_cavlc.c \
  src/h264_macroblock.c src/h264_intra.c src/h264_mb_parse.c \
  src/h264_mb_residual.c src/h264_slice_data.c src/h264_decoder.c \
  -o "$OUT"
"$OUT"
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_rbsp.c src/h264_decoder.c -o "$RBSP_OUT"
"$RBSP_OUT"
I16_OUT=${TMPDIR:-/tmp}/crtube-h264-i16-test
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_i16.c src/h264_i16.c src/h264_frame.c \
  src/h264_chroma.c src/h264_cavlc.c src/h264_intra.c src/h264_recon.c \
  src/h264_mb_parse.c src/h264_mb_residual.c src/h264_macroblock.c \
  -o "$I16_OUT"
"$I16_OUT"

CAVLC_OUT=${TMPDIR:-/tmp}/crtube-h264-cavlc-bounds-test
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_cavlc_bounds.c src/h264_cavlc.c -o "$CAVLC_OUT"
"$CAVLC_OUT"

PART_OUT=${TMPDIR:-/tmp}/crtube-h264-partition-weight-test
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_partition_weight.c src/h264_p_partition.c \
  src/h264_p_mb.c src/h264_motion.c src/h264_frame.c -o "$PART_OUT"
"$PART_OUT"

CAVLC_REF_OUT=${TMPDIR:-/tmp}/crtube-h264-cavlc-reference-test
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_cavlc_reference.c src/h264_cavlc.c -o "$CAVLC_REF_OUT"
"$CAVLC_REF_OUT"

WEIGHT_OUT=${TMPDIR:-/tmp}/crtube-h264-weight-table-test
cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude \
  tests/test_h264_weight_table.c src/h264_decoder.c -o "$WEIGHT_OUT"
"$WEIGHT_OUT"
