EE_BIN = r5900_decoder_function_test.elf
TEST_DIR = ..
EDGE_DIR = ../../third_party/edge264
EE_OBJS = test_main.o test_deblock_functions.o test_idct_functions.o \
          test_dc_functions.o test_intra_kernels.o test_inter_kernels.o \
          test_mvpred_kernels.o test_residual_kernels.o test_deblock_kernels.o \
          ../../third_party/edge264/ps2/edge264_r5900_mmi.o

EE_INCS += -I../../third_party/edge264/ps2 -I../../third_party/edge264/src
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -march=r5900 -flax-vector-conversions \
             -DSIMD=6 -DEDGE264_PS2 -DCRTUBE_R5900_MMI -DNEWLIB_PORT_AWARE
EE_LIBS += -ldebug

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

test_main.o: test_main.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

test_deblock_functions.o: $(TEST_DIR)/test_r5900_deblock_functions.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_deblock_functions -c $< -o $@

test_idct_functions.o: $(TEST_DIR)/test_r5900_idct_functions.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_idct_functions -c $< -o $@

test_dc_functions.o: $(TEST_DIR)/test_r5900_dc_functions.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_dc_functions -c $< -o $@

test_intra_kernels.o: $(TEST_DIR)/test_r5900_intra_kernels.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_intra_kernels -c $< -o $@

test_inter_kernels.o: $(TEST_DIR)/test_r5900_inter_kernels.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_inter_kernels -c $< -o $@

test_mvpred_kernels.o: $(TEST_DIR)/test_r5900_mvpred_kernels.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_mvpred_kernels -c $< -o $@

test_residual_kernels.o: $(TEST_DIR)/test_r5900_residual_kernels.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_residual_kernels -c $< -o $@

test_deblock_kernels.o: $(TEST_DIR)/test_r5900_deblock_kernels.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Dmain=r5900_test_deblock_kernels -c $< -o $@

../../third_party/edge264/ps2/edge264_r5900_mmi.o: $(EDGE_DIR)/ps2/edge264_r5900_mmi.c $(EDGE_DIR)/ps2/edge264_r5900_mmi.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

clean:
	rm -f $(EE_OBJS) $(EE_BIN)

.PHONY: all clean
