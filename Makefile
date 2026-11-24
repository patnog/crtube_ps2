.SUFFIXES:
.DEFAULT_GOAL := all

ifeq ($(H264_EDGE264_EXPERIMENTAL),1)
EE_BIN  := bin/crtube-ps2-edge264-experimental.elf
else
EE_BIN  := bin/crtube-ps2.elf
endif
PS2HTTP_IRX ?= $(PS2SDK)/iop/irx/ps2http.irx
# The previous hand-written H.264 decoder was an unfinished CAVLC subset and
# had no caller in the application. Keep its sources for reference, but do not
# link that incomplete pipeline into the default ELF.
H264_BACKEND ?= none
H264_LEGACY_OBJS := src/h264_playback.o src/h264_frame.o src/h264_chroma.o \
  src/h264_mb_residual.o src/h264_mb_parse.o src/h264_slice_data.o \
  src/h264_p_mb.o src/h264_p_partition.o src/h264_p_residual.o src/h264_p_recon.o \
  src/h264_p_decode.o src/h264_p_slice.o src/h264_pipeline.o \
  src/h264_i16.o src/h264_motion.o src/h264_macroblock.o \
  src/h264_intra.o src/h264_recon.o src/h264_cavlc.o src/h264_decoder.o

EE_OBJS := src/h264_mp4_smb.o src/h264_frame.o src/network/ps2ipc.o src/main.o src/app.o src/ui.o src/localization.o src/screen.o src/h264_yuv.o src/config.o src/debug_log.o src/network/network.o src/network/http.o src/network/tcp_probe.o \
           libs/DEV9_irx.o libs/NETMAN_irx.o libs/SMAP_irx.o libs/SMSUTILS_irx.o \
           libs/PS2IP_NM_irx.o libs/PS2IPS_irx.o libs/PS2HTTP_irx.o libs/SMBMAN_irx.o \
           libs/IOMANX_irx.o libs/FILEXIO_irx.o

# Explicit opt-in only: build a separate ELF containing edge264. The normal
# application ELF and H264_BACKEND default remain unchanged.
ifeq ($(H264_EDGE264_EXPERIMENTAL),1)
EDGE264_R5900_MMI ?= 1
H264_TEST_MAX_SAMPLES ?= 120
H264_PLANE_CRC_EVERY_FRAME ?= 1
H264_FOUR_PASS_TEST ?= 0
EDGE264_EE_CFLAGS := -flax-vector-conversions -DSIMD=6 -DEDGE264_PS2 \
                     -DEDGE264_SINGLE_THREAD -DCRTUBE_EDGE264_PROFILE -march=r5900
ifeq ($(EDGE264_R5900_MMI),1)
EDGE264_EE_CFLAGS += -DCRTUBE_R5900_MMI -DCRTUBE_R5900_MMI_ENABLED=1
else
EDGE264_EE_CFLAGS += -DCRTUBE_R5900_MMI_ENABLED=0
endif
ifeq ($(EDGE264_R5900_DISABLE_MINMAX_U8),1)
EDGE264_EE_CFLAGS += -DEDGE264_R5900_DISABLE_MINMAX_U8=1
endif
ifeq ($(EDGE264_R5900_DISABLE_CHROMA_MC),1)
EDGE264_EE_CFLAGS += -DEDGE264_R5900_DISABLE_CHROMA_MC=1
endif
ifeq ($(EDGE264_R5900_DISABLE_CHROMA_MADDABCD),1)
EDGE264_EE_CFLAGS += -DEDGE264_R5900_DISABLE_CHROMA_MADDABCD=1
endif
ifeq ($(EDGE264_R5900_DISABLE_CHROMA_WEIGHTED),1)
EDGE264_EE_CFLAGS += -DEDGE264_R5900_DISABLE_CHROMA_WEIGHTED=1
endif
ifeq ($(EDGE264_R5900_ENABLE_CHROMA_WEIGHTED),1)
EDGE264_EE_CFLAGS += -DEDGE264_R5900_ENABLE_CHROMA_WEIGHTED=1
endif
ifeq ($(H264_MMI_MATRIX_TEST),1)
EDGE264_EE_CFLAGS += -DCRTUBE_H264_MMI_MATRIX_TEST=1
endif
EE_OBJS := $(filter-out src/app.o,$(EE_OBJS))
ifeq ($(H264_EDGE264_DECODER_TEST_ONLY),1)
# Enable the first real VU decoder offload: 16-wide vertical luma six-tap.
EDGE264_EE_CFLAGS += -DCRTUBE_EDGE264_VU_OFFLOAD=1
EE_LIBS += -lpacket2
EE_INCS += -I$(PS2SDK)/ee/packet2/include -I$(PS2SDK)/ee/dma/include
# Full hardware regression: MMI/function tests followed by a real MP4 decode
# of the embedded 180-frame fixture. No external media path is required.
EE_OBJS += src/app_edge264_smoke.o src/h264_mp4.o src/h264_edge264_smb_playback.o \
           src/edge264_test_mp4.o tests/vu_h264_parallel.o tests/vu_h264_filter.o \
           tests/vu_h264_filter_once.o \
           third_party/edge264/build/edge264_r5900.o \
           third_party/edge264/build/h264_edge264_adapter.o \
           third_party/edge264/build/edge264_ps2_builtins.o \
           third_party/edge264/build/edge264_r5900_mmi.o \
           tests/r5900_deblock_functions.o tests/r5900_idct_functions.o \
           tests/r5900_dc_functions.o tests/r5900_intra_kernels.o \
           tests/r5900_inter_kernels.o tests/r5900_mvpred_kernels.o \
           tests/r5900_residual_kernels.o tests/r5900_deblock_kernels.o
else ifeq ($(H264_EDGE264_MMI_TEST_ONLY),1)
# Decoder-independent EE MMI/function suite. Reuse application startup and
# SMB logging, but do not link or initialize MP4/H.264.
EE_OBJS := $(filter-out src/h264_mp4_smb.o,$(EE_OBJS))
EE_OBJS += src/app_edge264_smoke.o third_party/edge264/build/edge264_r5900_mmi.o \
           tests/r5900_deblock_functions.o tests/r5900_idct_functions.o \
           tests/r5900_dc_functions.o tests/r5900_intra_kernels.o \
           tests/r5900_inter_kernels.o tests/r5900_mvpred_kernels.o \
           tests/r5900_residual_kernels.o tests/r5900_deblock_kernels.o
# The included mvpred implementation contains unrelated decoder entry points;
# discard unreferenced sections so this decoder-independent runner links cleanly.
EE_CFLAGS += -ffunction-sections -fdata-sections
EE_LDFLAGS += -Wl,--gc-sections
else
EE_OBJS += src/app_edge264_smoke.o src/h264_mp4.o src/h264_edge264_smoke.o \
           src/h264_edge264_smb_playback.o src/edge264_test_mp4.o \
           third_party/edge264/build/edge264_r5900.o \
           third_party/edge264/build/h264_edge264_adapter.o \
           third_party/edge264/build/edge264_ps2_builtins.o
ifeq ($(EDGE264_R5900_MMI),1)
EE_OBJS += third_party/edge264/build/edge264_r5900_mmi.o
else
$(info Edge264 R5900 MMI: DISABLED (scalar fallback for differential test))
endif
endif
endif

GSKIT ?= $(PS2DEV)/gsKit
EE_INCS   += -Iinclude -I$(GSKIT)/include -I$(PS2SDK)/ports/include
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -DNEWLIB_PORT_AWARE
src/network/ps2ipc.o: EE_CFLAGS += -DPS2IP_DNS

EE_LIBS   += -L$(GSKIT)/lib -lgskit -ldmakit -ldma -L$(PS2SDK)/ports/lib -lfileXio -lnetman -lps2ips -lpad -ldebug -lpatches -lcdvd -lm

# Keep the backend disabled unless the explicit experimental link target is
# requested. The normal ELF never includes edge264.
ifneq ($(H264_BACKEND),none)
$(error H264_BACKEND=$(H264_BACKEND) is not ready for EE; R5900 backend is pending. Use h264-edge264-host-test for host validation)
endif

# Optional YouTube HTTPS diagnostic using the in-tree BearSSL client.
# No libcurl or wolfSSL ports are required.
YOUTUBE_TEST ?= 0
ifeq ($(YOUTUBE_TEST),1)
BEARSSL_SRCS := $(sort $(shell find third_party/bearssl/src -name '*.c'))
BEARSSL_OBJS := $(patsubst third_party/bearssl/src/%.c,third_party/bearssl/build/%.o,$(BEARSSL_SRCS))
BEARSSL_LIB  := third_party/bearssl/lib/libbearssl.a
EE_OBJS += src/images.o src/network/youtube_probe.o src/network/youtube_search.o src/network/bearssl_transport.o \
           src/network/bearssl_trust_anchors.o src/network/sntp.o
EE_INCS += -Ithird_party/bearssl/inc -Ithird_party/bearssl/src
EE_CFLAGS += -DCRTUBE_ENABLE_YOUTUBE_PROBE -DBR_USE_URANDOM=0 -DBR_USE_UNIX_TIME=0
EE_LIBS += -Lthird_party/bearssl/lib -lbearssl -ljpeg_ps2_addons -ljpeg -lcdvd -lm

$(EE_BIN): $(BEARSSL_LIB)

$(BEARSSL_LIB): $(BEARSSL_OBJS)
	mkdir -p $(dir $@)
	$(EE_AR) rcs $@ $^

third_party/bearssl/build/%.o: third_party/bearssl/src/%.c
	mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
endif

.PHONY: all build clean run standalone youtube-test h264-edge264-host-test h264-r5900-probe

# The default build is always clean and includes the YouTube HTTPS/JSON
# diagnostic. Use "make build" only when an incremental build is intentional.
all: clean
	$(MAKE) YOUTUBE_TEST=1 build

build: $(EE_BIN)

$(EE_BIN): | bin

bin:
	mkdir -p $@

clean:
	rm -f $(EE_BIN) bin/crtube-ps2-edge264-experimental.elf $(EE_OBJS) $(H264_LEGACY_OBJS) src/h264_mp4.o src/h264_edge264.o src/h264_edge264_smoke.o src/h264_edge264_smb_playback.o src/edge264_test_mp4.o src/edge264_test_mp4.c src/app_edge264_smoke.o src/screen.o tests/r5900_deblock_functions.o tests/r5900_idct_functions.o tests/r5900_dc_functions.o tests/r5900_intra_kernels.o tests/r5900_inter_kernels.o tests/r5900_mvpred_kernels.o tests/r5900_residual_kernels.o tests/r5900_deblock_kernels.o third_party/edge264/build/edge264.o third_party/edge264/build/edge264_r5900.o third_party/edge264/build/edge264_r5900_mmi.o third_party/edge264/build/h264_edge264_adapter.o third_party/edge264/build/edge264_ps2_builtins.o third_party/edge264/build/r5900_compiler_probe.o src/images.o src/network/youtube_probe.o src/network/youtube_search.o libs/DEV9_irx.c libs/NETMAN_irx.c libs/SMAP_irx.c \
		libs/SMSUTILS_irx.c libs/PS2IP_NM_irx.c libs/PS2IPS_irx.c \
		libs/PS2HTTP_irx.c libs/SMBMAN_irx.c libs/IOMANX_irx.c libs/FILEXIO_irx.c \
		src/network/bearssl_transport.o src/network/bearssl_trust_anchors.o src/network/sntp.o tests/vu_h264_filter_once.o tests/vu_h264_filter_once.vsm
	rm -rf third_party/bearssl/build third_party/bearssl/lib

libs/DEV9_irx.c: $(PS2SDK)/iop/irx/ps2dev9.irx
	bin2c $< $@ DEV9_irx

libs/NETMAN_irx.c: $(PS2SDK)/iop/irx/netman.irx
	bin2c $< $@ NETMAN_irx

libs/SMAP_irx.c: $(PS2SDK)/iop/irx/smap.irx
	bin2c $< $@ SMAP_irx

libs/SMSUTILS_irx.c: iop/irx/smsutils.irx
	bin2c $< $@ SMSUTILS_irx

libs/PS2IP_NM_irx.c: iop/irx/ps2ip-nm.irx
	bin2c $< $@ PS2IP_NM_irx

libs/PS2IPS_irx.c: iop/irx/ps2ips.irx
	bin2c $< $@ PS2IPS_irx

# Official PS2SDK IOP HTTP filesystem driver (registers the http: device).
libs/PS2HTTP_irx.c: $(PS2HTTP_IRX)
	@test -f $< || { echo "Missing PS2SDK HTTP driver: $<"; exit 1; }
	bin2c $< $@ PS2HTTP_irx

libs/SMBMAN_irx.c: iop/irx/smbman.irx
	bin2c $< $@ SMBMAN_irx

libs/IOMANX_irx.c: $(PS2SDK)/iop/irx/iomanX.irx
	bin2c $< $@ IOMANX_irx

libs/FILEXIO_irx.c: $(PS2SDK)/iop/irx/fileXio.irx
	bin2c $< $@ FILEXIO_irx

# Standalone ELF owns the network stack and can be launched without PS2Link.
standalone: all

# Compatibility alias; the default build already includes this diagnostic.
youtube-test: all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal


# Host-side integration smoke test. This validates the adapter and upstream
# decoder on the host; it is not a substitute for an EE cross-build or hardware test.
h264-edge264-host-test:
	sh tests/run_host_edge264_test.sh

# Compile-only probe against the actual PS2SDK EE compiler. This does not
# enable or link the experimental decoder backend.
h264-r5900-probe:
	EE_CC="$(EE_CC)" EE_CFLAGS="$(EE_CFLAGS)" EE_INCS="$(EE_INCS)" sh tests/run_r5900_compiler_probe.sh

.PHONY: h264-r5900-probe h264-r5900-mmi-host-test h264-r5900-mmi-ee-test h264-r5900-mmi-runner

# Broad deterministic/randomized validation of the SIMD=6 lane contract.
.PHONY: h264-r5900-vector-host-test
h264-r5900-vector-host-test:
	sh tests/run_host_r5900_vector_tests.sh

# Portable reference checks for the semantics of the first MMI primitives.
h264-r5900-mmi-host-test:
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror tests/test_r5900_mmi.c third_party/edge264/ps2/edge264_r5900_mmi.c -o /tmp/crtube-r5900-mmi-test
	/tmp/crtube-r5900-mmi-test

# Build the application-based MMI + decoder-function test ELF.
h264-r5900-mmi-runner:
	$(MAKE) h264-r5900-mmi-ee-runner

.PHONY: h264-r5900-decoder-runner
h264-r5900-decoder-runner:
	$(MAKE) h264-r5900-decoder-ee-runner

# Diagnostic control: decode the identical embedded stream with the scalar
# backend. Keep the direct MMI instruction tests enabled, but disable MMI in
# the decoder so plane CRCs can isolate backend-specific chroma differences.
.PHONY: h264-r5900-decoder-scalar-runner
h264-r5900-decoder-scalar-runner:
	$(MAKE) h264-r5900-decoder-scalar-ee-runner

# Keep the MMI backend active but force unsigned-byte min/max wrappers to the
# scalar lane implementation. This isolates the min/max path used by clipping
# and deblocking while preserving direct instruction self-tests.
.PHONY: h264-r5900-decoder-no-minmax-runner
h264-r5900-decoder-no-minmax-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_TEST_MAX_SAMPLES=0 EDGE264_R5900_MMI=1 EDGE264_R5900_DISABLE_MINMAX_U8=1 YOUTUBE_TEST=1 build

# Keep MMI enabled for luma and the rest of the decoder, but force the complete
# chroma motion-compensation arithmetic (C16/C8/C4 and maddABCD) through the
# scalar reference formulas. This isolates the path implicated by Cb/Cr CRCs.
.PHONY: h264-r5900-decoder-no-chroma-mc-runner
h264-r5900-decoder-no-chroma-mc-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_TEST_MAX_SAMPLES=0 EDGE264_R5900_MMI=1 EDGE264_R5900_DISABLE_CHROMA_MC=1 YOUTUBE_TEST=1 build

# Split the chroma MC diagnostic into its two arithmetic stages. First keep
# PMADDH-backed weighted accumulation active and scalarize maddABCD only.
.PHONY: h264-r5900-decoder-no-chroma-maddabcd-runner
h264-r5900-decoder-no-chroma-maddabcd-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_TEST_MAX_SAMPLES=0 EDGE264_R5900_MMI=1 EDGE264_R5900_DISABLE_CHROMA_MADDABCD=1 YOUTUBE_TEST=1 build

# Keep maddABCD MMI active, but scalarize the final weighted chroma blend
# (C16/C8/C4). This separates input filtering from destination blending.
.PHONY: h264-r5900-decoder-no-chroma-weighted-runner
h264-r5900-decoder-no-chroma-weighted-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_TEST_MAX_SAMPLES=0 EDGE264_R5900_MMI=1 EDGE264_R5900_DISABLE_CHROMA_WEIGHTED=1 YOUTUBE_TEST=1 build

# Build the MMI + decoder-function suite using the same complete startup/link environment
# as the experimental application ELF (including its normal UI/network
# dependencies). It mounts SMB for dbg_crtube.txt but never opens an MP4 or
# constructs an H.264 decoder. YOUTUBE_TEST=1 is needed for the shared
# application network/UI objects to resolve their normal link dependencies;
# it does not run a YouTube probe during this test-only startup.
.PHONY: h264-r5900-mmi-ee-runner h264-r5900-decoder-ee-runner h264-r5900-decoder-matrix-ee-runner
h264-r5900-mmi-ee-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_MMI_TEST_ONLY=1 EDGE264_R5900_MMI=1 YOUTUBE_TEST=1 build

# Run the same MMI/function suite, then decode the embedded 180-frame MP4
# through the R5900 backend. All CRCs and results go to the existing SMB log.
h264-r5900-decoder-ee-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_TEST_MAX_SAMPLES=0 EDGE264_R5900_MMI=1 EDGE264_R5900_DISABLE_CHROMA_MADDABCD=1 YOUTUBE_TEST=1 build

# Single-ELF decoder matrix: scalar baseline, each currently integrated MMI
# family in isolation, then all available families together. CRCs for all 180
# output frames are captured from the scalar pass and compared on every pass.
# No GS frame upload or 24-fps pacing is performed during the matrix.
h264-r5900-decoder-matrix-ee-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_MMI_MATRIX_TEST=1 H264_TEST_MAX_SAMPLES=0 H264_PLANE_CRC_EVERY_FRAME=0 EDGE264_R5900_MMI=1 EDGE264_R5900_ENABLE_CHROMA_WEIGHTED=1 YOUTUBE_TEST=1 build

# Same regression fixture and SMB log, with scalar decoder kernels.
h264-r5900-decoder-scalar-ee-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_TEST_MAX_SAMPLES=0 EDGE264_R5900_MMI=0 YOUTUBE_TEST=1 build

# Single-ELF differential runner: scalar EE, MMI EE, then MMI+VU0/VU1.
# Each backend runs instrumented and quiet so output equivalence and timing
# can be compared without mixing EE-only and VU-assisted measurements.
.PHONY: h264-r5900-decoder-ee-vu-runner h264-r5900-decoder-four-pass-ee-runner
h264-r5900-decoder-ee-vu-runner:
	$(MAKE) h264-edge264-ee-clean
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 H264_EDGE264_DECODER_TEST_ONLY=1 H264_EDGE264_MMI_TEST_ONLY=1 H264_FOUR_PASS_TEST=1 H264_MMI_MATRIX_TEST=1 H264_TEST_MAX_SAMPLES=0 H264_PLANE_CRC_EVERY_FRAME=1 EDGE264_R5900_MMI=1 EDGE264_R5900_DISABLE_CHROMA_MADDABCD=1 YOUTUBE_TEST=1 build

h264-r5900-decoder-four-pass-ee-runner: h264-r5900-decoder-ee-vu-runner

# Compile the real MMI implementation with the EE compiler. This is a compile
# gate only; it does not link the decoder into the application.
h264-r5900-mmi-ee-test:
	@test -n "$(EE_CC)" || { echo "EE_CC is not set; source the PS2SDK environment first"; exit 2; }
	mkdir -p third_party/edge264/build
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -march=r5900 -DCRTUBE_R5900_MMI -Ithird_party/edge264/ps2 -c third_party/edge264/ps2/edge264_r5900_mmi.c -o third_party/edge264/build/edge264_r5900_mmi.o
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpaddub\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpaddsh\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpmaddh\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bppacb\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpsubub\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpminh\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpmaxh\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpcgth\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpceqh\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpand\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpor\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\bpxor\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpaddw\\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpsubw\\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpsraw\\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpaddh\\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpsubh\\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpsrah\\b'
	$(EE_OBJDUMP) -d third_party/edge264/build/edge264_r5900_mmi.o | grep -Eiq '\\bpnor\\b'
	@echo "R5900 MMI primitive codegen: PASS"

# Derive the matching binutils executable when PS2SDK does not set it.
EE_OBJDUMP ?= $(patsubst %gcc,%objdump,$(EE_CC))


# Compiler-only gate for the first edge264 GCC/R5900 vector backend. This does
# not add the decoder to EE_OBJS or claim real-time playback performance.
.PHONY: h264-r5900-edge264-ee-compile h264-r5900-edge264-host-test

h264-r5900-edge264-host-test:
	sh tests/run_host_r5900_edge264_test.sh

h264-r5900-edge264-ee-compile:
	@test -n "$(EE_CC)" || { echo "EE_CC is not set; source the PS2SDK environment first"; exit 2; }
	mkdir -p third_party/edge264/build
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264 -Ithird_party/edge264/ps2/include -DSIMD=6 -DEDGE264_PS2 -DCRTUBE_R5900_MMI -DEDGE264_SINGLE_THREAD -march=r5900 -c third_party/edge264/src/edge264.c -o third_party/edge264/build/edge264_r5900.o
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ithird_party/edge264/ps2 -DCRTUBE_R5900_MMI -DEDGE264_SINGLE_THREAD -march=r5900 -c third_party/edge264/ps2/edge264_r5900_mmi.c -o third_party/edge264/build/edge264_r5900_mmi.o
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ithird_party/edge264 -c src/h264_edge264.c -o third_party/edge264/build/h264_edge264_adapter.o
	@echo "R5900 edge264 translation units: compile PASS (not linked into application)"

# Full experimental ELF link. This is deliberately separate from the default
# ELF; successful linking still does not imply decoder runtime stability.
.PHONY: h264-edge264-ee-link h264-edge264-ee-clean h264-edge264-ps2-host-test h264-edge264-mp4-host-test

# Always discard experimental objects before linking. This prevents stale
# objects built with a different PS2SDK compiler/ABI from reaching ld.
h264-edge264-ee-link: h264-edge264-ee-clean
	@echo "Edge264 R5900 MMI: $(if $(filter 1,$(EDGE264_R5900_MMI)),ENABLED,DISABLED)"
	$(MAKE) H264_EDGE264_EXPERIMENTAL=1 EDGE264_R5900_MMI=$(if $(filter 0,$(EDGE264_R5900_MMI)),0,1) YOUTUBE_TEST=1 build

# Keep the cleanup recipe on one shell line: doubled backslashes here used
# to become a literal argument and the following paths were executed as cmds.
h264-edge264-ee-clean:
	rm -f bin/crtube-ps2-edge264-experimental.elf third_party/edge264/build/edge264_r5900.o third_party/edge264/build/edge264_r5900_mmi.o third_party/edge264/build/h264_edge264_adapter.o third_party/edge264/build/edge264_ps2_builtins.o src/h264_mp4.o src/h264_frame.o src/h264_edge264_smoke.o src/h264_edge264_smb_playback.o src/edge264_test_mp4.o src/edge264_test_mp4.c src/app_edge264_smoke.o src/screen.o tests/r5900_deblock_functions.o tests/r5900_idct_functions.o tests/r5900_dc_functions.o tests/r5900_intra_kernels.o tests/r5900_inter_kernels.o tests/r5900_mvpred_kernels.o tests/r5900_residual_kernels.o tests/r5900_deblock_kernels.o src/images.o src/network/youtube_probe.o src/network/youtube_search.o src/network/bearssl_transport.o src/network/bearssl_trust_anchors.o src/network/sntp.o tests/vu_h264_filter_once.o tests/vu_h264_filter_once.vsm
	rm -rf third_party/bearssl/build third_party/bearssl/lib

# VU0/VU1 six-tap kernels: repeat-64 for the synthetic throughput test and
# repeat-once for actual decoder blocks.
EE_DVP ?= dvp-as
tests/vu_h264_parallel.o: tests/vu_h264_parallel.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -I$(PS2SDK)/ee/packet2/include -I$(PS2SDK)/ee/dma/include -c $< -o $@

tests/vu_h264_filter.o: tests/vu_h264_filter.vsm
	$(EE_DVP) $< -o $@

tests/vu_h264_filter_once.vsm: tests/vu_h264_filter.vsm
	sed -e 's/VU_H264_Filter_CodeStart/VU_H264_Filter_Once_CodeStart/g' \
	    -e 's/VU_H264_Filter_CodeEnd/VU_H264_Filter_Once_CodeEnd/g' \
	    -e 's/VI14,VI00,64/VI14,VI00,1/' $< > $@

tests/vu_h264_filter_once.o: tests/vu_h264_filter_once.vsm
	$(EE_DVP) $< -o $@

# The MMI runner reuses the application's SMB logger and startup. Compile each
# deterministic decoder-function test as a callable entry point (no test ELF).
ifeq ($(H264_EDGE264_MMI_TEST_ONLY),1)
R5900_FUNCTION_TESTS := deblock_functions idct_functions dc_functions intra_kernels inter_kernels mvpred_kernels residual_kernels deblock_kernels
R5900_FUNCTION_TEST_OBJS := $(addprefix tests/r5900_,$(addsuffix .o,$(R5900_FUNCTION_TESTS)))

define R5900_TEST_RULE
tests/r5900_$(1).o: tests/test_r5900_$(1).c
	mkdir -p tests
	$(EE_CC) $(EE_CFLAGS) $(EDGE264_EE_CFLAGS) $(EE_INCS) -Ithird_party/edge264 -DCRTUBE_R5900_SMB_LOG -Dmain=crtube_r5900_test_$(1) -c $$< -o $$@
endef
$(foreach test,$(R5900_FUNCTION_TESTS),$(eval $(call R5900_TEST_RULE,$(test))))
endif

src/app_edge264_smoke.o: src/app.c include/h264_edge264.h third_party/edge264/ps2/edge264_r5900_mmi.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ithird_party/edge264/ps2 -DCRTUBE_H264_EDGE264_RUNTIME_SMOKE $(if $(filter 1,$(H264_EDGE264_MMI_TEST_ONLY) $(H264_EDGE264_DECODER_TEST_ONLY)),-DCRTUBE_R5900_MMI_TEST_ONLY) $(if $(filter 1,$(H264_EDGE264_DECODER_TEST_ONLY)),-DCRTUBE_R5900_DECODER_TEST_ONLY) $(if $(filter 1,$(H264_MMI_MATRIX_TEST)),-DCRTUBE_H264_MMI_MATRIX_TEST=1) $(if $(filter 1,$(H264_FOUR_PASS_TEST)),-DCRTUBE_H264_FOUR_PASS_TEST=1) -c $< -o $@

ifeq ($(H264_EDGE264_EXPERIMENTAL),1)
src/screen.o: src/screen.c include/screen.h include/h264_edge264.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -DCRTUBE_H264_EDGE264_RUNTIME_SMOKE -c $< -o $@

src/h264_edge264_smb_playback.o: src/h264_edge264_smb_playback.c include/h264_edge264_smb_playback.h include/edge264_test_mp4.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -Ithird_party/edge264 -DCRTUBE_H264_EDGE264_RUNTIME_SMOKE -DCRTUBE_R5900_MMI_ENABLED=$(EDGE264_R5900_MMI) $(if $(filter 1,$(EDGE264_R5900_DISABLE_MINMAX_U8)),-DEDGE264_R5900_DISABLE_MINMAX_U8=1) $(if $(filter 1,$(EDGE264_R5900_DISABLE_CHROMA_MC)),-DEDGE264_R5900_DISABLE_CHROMA_MC=1) $(if $(filter 1,$(EDGE264_R5900_DISABLE_CHROMA_MADDABCD)),-DEDGE264_R5900_DISABLE_CHROMA_MADDABCD=1) $(if $(filter 1,$(EDGE264_R5900_DISABLE_CHROMA_WEIGHTED)),-DEDGE264_R5900_DISABLE_CHROMA_WEIGHTED=1) -DCRTUBE_H264_TEST_MAX_SAMPLES=$(H264_TEST_MAX_SAMPLES) -DCRTUBE_H264_PLANE_CRC_EVERY_FRAME=$(H264_PLANE_CRC_EVERY_FRAME) $(if $(filter 1,$(H264_MMI_MATRIX_TEST)),-DCRTUBE_H264_MMI_MATRIX_TEST=1) $(if $(filter 1,$(H264_EDGE264_DECODER_TEST_ONLY)),-DCRTUBE_R5900_DECODER_TEST_ONLY) -c $< -o $@

src/edge264_test_mp4.o: src/edge264_test_mp4.c include/edge264_test_mp4.h
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

src/edge264_test_mp4.c: tests/data/mc_coverage.mp4
	bin2c $< $@ edge264_test_mp4

tests/data/mc_coverage.mp4: tests/generate_edge264_mc_mp4.sh
	$< $@
endif

third_party/edge264/build/edge264_r5900.o: third_party/edge264/src/edge264.c third_party/edge264/edge264.h third_party/edge264/ps2/edge264_r5900_mmi.h
	mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EDGE264_EE_CFLAGS) -DEDGE264_SINGLE_THREAD -march=r5900 $(EE_INCS) -Ithird_party/edge264 -Ithird_party/edge264/ps2/include -c $< -o $@

third_party/edge264/build/edge264_r5900_mmi.o: third_party/edge264/ps2/edge264_r5900_mmi.c third_party/edge264/ps2/edge264_r5900_mmi.h
	mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EDGE264_EE_CFLAGS) -march=r5900 $(if $(filter 1,$(H264_EDGE264_MMI_TEST_ONLY) $(H264_EDGE264_DECODER_TEST_ONLY)),-DCRTUBE_R5900_MMI_TEST_ONLY) $(EE_INCS) -Ithird_party/edge264/ps2 -c $< -o $@

third_party/edge264/build/edge264_ps2_builtins.o: third_party/edge264/ps2/edge264_ps2_builtins.c
	mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) -fno-builtin -c $< -o $@

third_party/edge264/build/h264_edge264_adapter.o: src/h264_edge264.c include/h264_edge264.h
	mkdir -p $(dir $@)
	$(EE_CC) $(EE_CFLAGS) $(EDGE264_EE_CFLAGS) $(EE_INCS) -Ithird_party/edge264 -c $< -o $@

# Host regression for the PS2 single-thread atomic shim and libgcc helpers.
h264-edge264-ps2-host-test:
	sh tests/run_host_edge264_ps2_singlethread_test.sh

# Decode an externally supplied 320x240 AVC MP4 through the real MP4 adapter.
# Usage: make h264-edge264-mp4-host-test MP4=/path/to/240p-baseline.mp4
h264-edge264-mp4-host-test:
	@test -n "$(MP4)" || { echo "Set MP4=/path/to/video.mp4"; exit 2; }
	sh tests/run_host_edge264_mp4_test.sh "$(MP4)"


# Differential tests execute the real soft deblocking edge kernels.
.PHONY: h264-r5900-deblock-function-host-test h264-r5900-deblock-function-ee-compile
h264-r5900-deblock-function-host-test:
	CC_R5900="$(CC)" CC_REF="$(CLANG)" sh tests/run_host_r5900_deblock_function_equivalence.sh

h264-r5900-deblock-function-ee-compile:
	@test -n "$(EE_CC)" || { echo "EE_CC is not set; source the PS2SDK environment first"; exit 2; }
	mkdir -p third_party/edge264/build
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_deblock_functions.c -o third_party/edge264/build/test_r5900_deblock_functions_ee.o
	@echo "R5900 actual deblock kernels: EE compile PASS (not linked or run on console)"

# Differential tests call the real residual reconstruction functions directly.
.PHONY: h264-r5900-idct-function-host-test h264-r5900-idct-function-ee-compile
h264-r5900-idct-function-host-test:
	CC_R5900="$(CC)" CC_REF="$(CLANG)" sh tests/run_host_r5900_idct_function_equivalence.sh

CLANG ?= clang
h264-r5900-idct-function-ee-compile:
	@test -n "$(EE_CC)" || { echo "EE_CC is not set; source the PS2SDK environment first"; exit 2; }
	mkdir -p third_party/edge264/build
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_idct_functions.c -o third_party/edge264/build/test_r5900_idct_functions_ee.o
	@echo "R5900 actual IDCT functions: EE compile PASS (not linked or run on console)"

# Direct equivalence tests for the real DC reconstruction functions.
.PHONY: h264-r5900-dc-function-host-test h264-r5900-dc-function-ee-compile
h264-r5900-dc-function-host-test:
	sh tests/run_host_r5900_dc_function_equivalence.sh

h264-r5900-dc-function-ee-compile:
	@test -n "$(EE_CC)" || { echo "EE_CC is not set; source the PS2SDK environment first"; exit 2; }
	mkdir -p third_party/edge264/build
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DEDGE264_PS2 -DCRTUBE_R5900_MMI -march=r5900 -c tests/test_r5900_dc_functions.c -o third_party/edge264/build/test_r5900_dc_functions_ee.o
	@echo "R5900 actual DC reconstruction functions: EE compile PASS (not linked or run on console)"

.PHONY: h264-yuv-host-test
h264-yuv-host-test:
	sh tests/run_host_h264_yuv_tests.sh


# Unified gate for the functions that feed the R5900 H.264 decoder.
# Host differential tests prove SIMD=6 results match the independent SIMD=4
# reference; EE compile checks ensure the same test translation units are
# accepted by the actual R5900 toolchain. Runtime EE validation still requires
# running the dedicated test ELF on a console.
.PHONY: h264-r5900-decoder-equivalence h264-r5900-decoder-ee-compile
h264-r5900-decoder-equivalence:
	sh tests/run_host_r5900_decoder_equivalence.sh

h264-r5900-decoder-ee-compile: h264-r5900-deblock-function-ee-compile h264-r5900-idct-function-ee-compile h264-r5900-dc-function-ee-compile
	@test -n "$(EE_CC)" || { echo "EE_CC is not set; source the PS2SDK environment first"; exit 2; }
	mkdir -p third_party/edge264/build
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_intra_kernels.c -o third_party/edge264/build/test_r5900_intra_kernels_ee.o
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_inter_kernels.c -o third_party/edge264/build/test_r5900_inter_kernels_ee.o
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_mvpred_kernels.c -o third_party/edge264/build/test_r5900_mvpred_kernels_ee.o
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_deblock_kernels.c -o third_party/edge264/build/test_r5900_deblock_kernels_ee.o
	$(EE_CC) $(EE_CFLAGS) -flax-vector-conversions $(EE_INCS) -Ithird_party/edge264/src -DSIMD=6 -DCRTUBE_R5900_MMI -DEDGE264_PS2 -march=r5900 -c tests/test_r5900_residual_kernels.c -o third_party/edge264/build/test_r5900_residual_kernels_ee.o
	@echo "R5900 decoder function test translation units: EE compile PASS (runtime execution remains a separate hardware gate)"
