#---------------------------------------------------------------------------------
# DVD Region Tools - PS3 homebrew (PSL1GHT / NPDRM)
#
#   make            build build/DVDREGION.self
#   make pkg        build DVDREGION.pkg  (signable with your own category / console)
#   make clean
#
# Requires:  ps3toolchain + PSL1GHT (PSL1GHT env var set) + ps3libraries (SDL)
#---------------------------------------------------------------------------------

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is not set. e.g. export PSL1GHT=$$PS3DEV/psl1ght)
endif

TITLE     := DVD Region Tools
APPID     := DVDRGN01
CONTENTID := UP0001-$(APPID)_00-0000000000000000

include $(PSL1GHT)/ppu_rules

# base_rules declares the tools as "export CC := $(PREFIX)gcc" but the linker as
# "export LD ?= $(PREFIX)gcc". GNU make already has a built-in LD, and ?= leaves
# an existing value alone, so LD stays the host "ld" - which then rejects every
# PowerPC archive as "skipping incompatible". Assigning explicitly is the fix.
CC      := ppu-gcc
LD      := ppu-gcc
CXX     := ppu-g++

TARGET    := DVDREGION
SOURCES   := source/main.c \
             source/util.c \
             source/gfx.c \
             source/input.c \
             source/xreg.c \
             source/storage.c \
             source/disc.c \
             source/rip.c
OFILES    := $(SOURCES:.c=.o)

INCLUDES  := -Iinclude -I$(PORTLIBS)/include

CFLAGS    += -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
             $(INCLUDES) $(LIBPSL1GHT_INC)

CXXFLAGS  += $(CFLAGS)

# base_rules links with "$(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS)", and nothing in
# PSL1GHT assigns LIBPATHS, so the search paths have to be given here or every
# PSL1GHT library (-lpad and friends) is reported as missing.
LIBPATHS  := -L$(PSL1GHT)/ppu/lib \
             -L$(PSL1GHT)/ppu/powerpc64-ps3-elf/lib \
             -L$(PORTLIBS)/lib

LDFLAGS   += $(LIBPATHS)

# SDL 1.3 is a static library that pulls in the whole RSX/video/audio stack, so
# its undefined symbols must be resolved after it.
#
# There is deliberately no -lpad: the ioPad* entry points live in libio, which is
# what PSL1GHT's own samples/input/padtest links (-lio -lnet). The hldtux/ps3dev
# image does not ship a libpad.a at all, so asking for one breaks the link.
LIBS      := -lSDL -lsysutil -lrsx -lgcm_sys -lio -laudio \
             -lrt -llv2 -lm

all:      $(TARGET).self
pkg:      $(TARGET).pkg

.PHONY: all pkg clean
