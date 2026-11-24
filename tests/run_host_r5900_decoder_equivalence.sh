#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

run() {
    echo
    echo "== $1 =="
    sh "tests/$1"
}

# Primitive instructions and vector semantics.
run run_host_r5900_vector_tests.sh

# Real edge264 transform/reconstruction/filter kernels, differential-tested
# against the independent Clang SIMD=4 reference implementation.
run run_host_r5900_dc_function_equivalence.sh
run run_host_r5900_idct_function_equivalence.sh
run run_host_r5900_deblock_function_equivalence.sh
run run_host_r5900_kernel_tests.sh

# End-to-end decoder output equivalence on the same H.264 fixture.
run run_host_r5900_edge264_test.sh

# The standalone in-tree H.264 implementation has its own parser/recon tests.
run run_host_h264_tests.sh

echo
echo "R5900 decoder equivalence suite: PASS"
