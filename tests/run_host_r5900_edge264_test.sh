#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BASE=${TMPDIR:-/tmp}/crtube-edge264-reference-test
R5900=${TMPDIR:-/tmp}/crtube-edge264-r5900-test
FIXTURE=${TMPDIR:-/tmp}/crtube-edge264-r5900-bumping.264
cd "$ROOT"

base64 -d tests/data/edge264-bumping.264.b64 > "$FIXTURE"
COMMON="-std=gnu11 -O2 -flax-vector-conversions -Wno-override-init -Wno-unused-function -Wno-return-type -pthread -Iinclude -Ithird_party/edge264"
# shellcheck disable=SC2086
${HOST_CC:-cc} $COMMON src/h264_edge264.c src/h264_mp4.c \
  third_party/edge264/src/edge264.c tests/test_r5900_backend_equivalence.c -o "$BASE"
# shellcheck disable=SC2086
${HOST_CC:-cc} $COMMON -DSIMD=6 -DEDGE264_PS2 \
  src/h264_edge264.c src/h264_mp4.c third_party/edge264/src/edge264.c \
  tests/test_r5900_backend_equivalence.c -o "$R5900"

BASE_RESULT=$("$BASE" "$FIXTURE")
R5900_RESULT=$("$R5900" "$FIXTURE")
printf 'reference: %s\nR5900 C/MMI backend: %s\n' "$BASE_RESULT" "$R5900_RESULT"
[ "$BASE_RESULT" = "$R5900_RESULT" ] || {
  echo "R5900 backend output differs from the reference backend" >&2
  exit 1
}
