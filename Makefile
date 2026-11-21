.SUFFIXES:
.DEFAULT_GOAL := all

EE_BIN  := bin/crtube-ps2.elf
PS2HTTP_IRX ?= $(PS2SDK)/iop/irx/ps2http.irx
EE_OBJS := src/main.o src/app.o src/screen.o src/config.o src/debug_log.o src/network/network.o src/network/http.o src/network/tcp_probe.o \
           libs/DEV9_irx.o libs/NETMAN_irx.o libs/SMAP_irx.o libs/SMSUTILS_irx.o \
           libs/PS2IP_NM_irx.o libs/PS2IPS_irx.o libs/PS2HTTP_irx.o libs/SMBMAN_irx.o \
           libs/IOMANX_irx.o libs/FILEXIO_irx.o

EE_INCS   += -Iinclude
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -DNEWLIB_PORT_AWARE

EE_LIBS   += -L$(PS2SDK)/ports/lib -lfileXio -lnetman -lps2ips -lpad -ldebug -lpatches

.PHONY: all clean run standalone

all: $(EE_BIN)

$(EE_BIN): | bin

bin:
	mkdir -p $@

clean:
	rm -f $(EE_BIN) $(EE_OBJS) libs/DEV9_irx.c libs/NETMAN_irx.c libs/SMAP_irx.c \
		libs/SMSUTILS_irx.c libs/PS2IP_NM_irx.c libs/PS2IPS_irx.c \
		libs/PS2HTTP_irx.c libs/SMBMAN_irx.c libs/IOMANX_irx.c libs/FILEXIO_irx.c

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
standalone:
	$(MAKE) clean
	$(MAKE) NETWORK_MODE=standalone all

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
