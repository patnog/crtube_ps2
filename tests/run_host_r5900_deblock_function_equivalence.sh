#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=${TMPDIR:-/tmp}/crtube-r5900-deblock-functions-$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"
CC=${CC:-cc}
CLANG_CC=${CLANG_CC:-clang}
COMMON="-std=gnu99 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-variable -flax-vector-conversions"
$CLANG_CC $COMMON -DSIMD=4 "$ROOT/tests/test_r5900_deblock_functions.c" -o "$TMP/clang"
$CC $COMMON -DSIMD=6 "$ROOT/tests/test_r5900_deblock_functions.c" -o "$TMP/r5900"
"$TMP/clang" > "$TMP/clang.out"
"$TMP/r5900" > "$TMP/r5900.out"
if ! cmp -s "$TMP/clang.out" "$TMP/r5900.out"; then
    echo "R5900 deblock function equivalence: FAIL"
    diff -u "$TMP/clang.out" "$TMP/r5900.out" || true
    exit 1
fi
cat "$TMP/r5900.out"
echo "R5900 deblock function equivalence: PASS (20000 soft luma + 20000 soft chroma + 20000 hard luma + 20000 hard chroma cases)"
