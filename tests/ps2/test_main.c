#include <stdio.h>
#include "edge264_r5900_mmi.h"

int r5900_test_deblock_functions(void);
int r5900_test_idct_functions(void);
int r5900_test_dc_functions(void);
int r5900_test_intra_kernels(void);
int r5900_test_inter_kernels(void);
int r5900_test_mvpred_kernels(void);
int r5900_test_residual_kernels(void);
int r5900_test_deblock_kernels(void);

int main(void)
{
    int failures = 0;
#define RUN_TEST(fn, label) do { \
    int result = fn(); \
    printf("EE test %s: %s (result=%d)\\n", label, result ? "FAIL" : "PASS", result); \
    if (result) ++failures; \
} while (0)

    printf("R5900 decoder function validation: begin\\n");
    if (edge264_r5900_mmi_selftest() != 0) ++failures;
    RUN_TEST(r5900_test_deblock_functions, "deblock functions");
    RUN_TEST(r5900_test_idct_functions, "IDCT functions");
    RUN_TEST(r5900_test_dc_functions, "DC reconstruction");
    RUN_TEST(r5900_test_intra_kernels, "intra kernels");
    RUN_TEST(r5900_test_inter_kernels, "inter kernels");
    RUN_TEST(r5900_test_mvpred_kernels, "motion-vector kernels");
    RUN_TEST(r5900_test_residual_kernels, "residual kernels");
    RUN_TEST(r5900_test_deblock_kernels, "deblock kernels");

    printf("R5900 decoder function validation: %s failures=%d\\n",
           failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
