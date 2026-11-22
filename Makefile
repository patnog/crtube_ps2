.SUFFIXES:
.DEFAULT_GOAL := all

EE_BIN  := bin/crtube-ps2.elf
PS2HTTP_IRX ?= $(PS2SDK)/iop/irx/ps2http.irx
EE_OBJS := src/network/ps2ipc.o src/main.o src/app.o src/ui.o src/localization.o src/screen.o src/config.o src/debug_log.o src/network/network.o src/network/http.o src/network/tcp_probe.o \
           libs/DEV9_irx.o libs/NETMAN_irx.o libs/SMAP_irx.o libs/SMSUTILS_irx.o \
           libs/PS2IP_NM_irx.o libs/PS2IPS_irx.o libs/PS2HTTP_irx.o libs/SMBMAN_irx.o \
           libs/IOMANX_irx.o libs/FILEXIO_irx.o

GSKIT ?= $(PS2DEV)/gsKit
EE_INCS   += -Iinclude -I$(GSKIT)/include -I$(PS2SDK)/ports/include
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -DNEWLIB_PORT_AWARE
src/network/ps2ipc.o: EE_CFLAGS += -DPS2IP_DNS

EE_LIBS   += -L$(GSKIT)/lib -lgskit -ldmakit -L$(PS2SDK)/ports/lib -lfileXio -lnetman -lps2ips -lpad -ldebug -lpatches -lcdvd -lm

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

.PHONY: all build clean run standalone youtube-test

# The default build is always clean and includes the YouTube HTTPS/JSON
# diagnostic. Use "make build" only when an incremental build is intentional.
all: clean
	$(MAKE) YOUTUBE_TEST=1 build

build: $(EE_BIN)

$(EE_BIN): | bin

bin:
	mkdir -p $@

clean:
	rm -f $(EE_BIN) $(EE_OBJS) src/images.o src/network/youtube_probe.o src/network/youtube_search.o libs/DEV9_irx.c libs/NETMAN_irx.c libs/SMAP_irx.c \
		libs/SMSUTILS_irx.c libs/PS2IP_NM_irx.c libs/PS2IPS_irx.c \
		libs/PS2HTTP_irx.c libs/SMBMAN_irx.c libs/IOMANX_irx.c libs/FILEXIO_irx.c \
		src/network/bearssl_transport.o src/network/bearssl_trust_anchors.o src/network/sntp.o
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
