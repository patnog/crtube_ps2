#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

: "${EE_CC:?EE_CC is not set; source the PS2SDK environment first}"
OUT=${R5900_PROBE_OUT:-third_party/edge264/build/r5900_compiler_probe.o}
mkdir -p "$(dirname "$OUT")"

printf 'R5900 compiler: %s\n' "$EE_CC"
# Use the same EE flags as the project, while making the target ISA explicit.
# The object is compile-only: it is never linked with the application.
$EE_CC ${EE_CFLAGS:-} ${EE_INCS:-} -march=r5900 -mabi=32 \
    -O2 -Wall -Wextra -Werror -std=gnu11 \
    -c tests/ps2/r5900_compiler_probe.c -o "$OUT"

printf 'R5900 compiler probe: PASS (%s)\n' "$OUT"
