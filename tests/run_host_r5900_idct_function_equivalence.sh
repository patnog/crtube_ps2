#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=${TMPDIR:-/tmp}/crtube-r5900-idct-functions-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
CC_R5900=${CC_R5900:-cc}
CC_REF=${CC_REF:-clang}
COMMON="-std=gnu11 -O2 -w -flax-vector-conversions -ffunction-sections -fdata-sections -I$ROOT/third_party/edge264/src"
$CC_R5900 $COMMON -DSIMD=6 "$ROOT/tests/test_r5900_idct_functions.c" -Wl,--gc-sections -o "$TMP/r5900"
$CC_REF $COMMON -DSIMD=4 "$ROOT/tests/test_r5900_idct_functions.c" -Wl,--gc-sections -o "$TMP/reference"
"$TMP/r5900" > "$TMP/r5900.out"
"$TMP/reference" > "$TMP/reference.out"
if ! cmp -s "$TMP/r5900.out" "$TMP/reference.out"; then
  echo "R5900 IDCT function mismatch" >&2
  diff -u "$TMP/reference.out" "$TMP/r5900.out" || true
  exit 1
fi
cat "$TMP/r5900.out"
echo "R5900 IDCT function equivalence: PASS (4000 4x4 + 4000 8x8 blocks)"
