#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${TMPDIR:-/tmp}/crtube-r5900-vector-semantics-$$
trap 'rm -f "$BIN"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -flax-vector-conversions \
  -DSIMD=6 -I"$ROOT/third_party/edge264/src" \
  "$ROOT/tests/test_r5900_vector_semantics.c" -o "$BIN"
"$BIN"
