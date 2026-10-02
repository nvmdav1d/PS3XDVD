#---------------------------------------------------------------------------------
# DVD Region Tools - PS3 homebrew (PSL1GHT / NPDRM)
#
#   make            -> build/DVDREGION.self
#   make pkg        -> DVDREGION.pkg  + DVDREGION.gnpdrm.pkg
#   make clean
#
# Requires: ps3toolchain + PSL1GHT ($PSL1GHT set) + ps3libraries (SDL 1.3)
# See .github/workflows/build.yml for a containerised build.
#---------------------------------------------------------------------------------

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is not set. e.g. export PSL1GHT=$$PS3DEV/psl1ght)
endif

TITLE     := DVD Region Tools
APPID     := DVDRGN01
CONTENTID := UP0001-$(APPID)_00-0000000000000000

#---------------------------------------------------------------------------------
# SOURCES / OFILES / TARGET must be defined BEFORE ppu_rules is included.
#
# base_rules declares the link rule as "%.elf: $(OFILES)". Rule prerequisites
# are expanded when the rule is read, not when the recipe runs, so an OFILES
# assigned afterwards leaves that rule with no prerequisites at all: make then
# links nothing and you get "undefined reference to `main'".
#---------------------------------------------------------------------------------
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

include $(PSL1GHT)/ppu_rules

#---------------------------------------------------------------------------------
# Toolchain
#
# base_rules declares the compiler as "export CC := $(PREFIX)gcc" but the linker
# as "export LD ?= $(PREFIX)gcc". GNU make already has a built-in LD, and ?= never
# overrides an existing value, so LD is left as the host "ld" - which then
# rejects every PowerPC archive with "skipping incompatible". Assign explicitly.
#---------------------------------------------------------------------------------
CC        := ppu-gcc
CXX       := ppu-g++
LD        := ppu-gcc

# SDL installs its headers as $PORTLIBS/include/SDL/SDL.h, so both the parent
# and the SDL subdirectory have to be on the include path for <SDL.h> to resolve.
INCLUDES  := -Iinclude -I$(PORTLIBS)/include -I$(PORTLIBS)/include/SDL

CFLAGS    += -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
             $(INCLUDES) $(LIBPSL1GHT_INC)

CXXFLAGS  += $(CFLAGS)

# base_rules links with "$(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS)", and nothing in
# PSL1GHT assigns LIBPATHS, so the search paths must be given here or every
# PSL1GHT library is reported as missing.
LIBPATHS  := -L$(PSL1GHT)/ppu/lib \
             -L$(PSL1GHT)/ppu/powerpc64-ps3-elf/lib \
             -L$(PORTLIBS)/lib

LDFLAGS   += $(LIBPATHS)

# SDL 1.3 is a static library that pulls in the whole RSX/video/audio stack, so
# its undefined symbols have to be resolved after it.
#
# There is deliberately no -lpad: the ioPad* entry points live in libio, which is
# what PSL1GHT's own samples/input/padtest links (-lio -lnet). The hldtux/ps3dev
# image ships no libpad.a at all, so asking for one breaks the link.
LIBS      := -lSDL -lsysutil -lrsx -lgcm_sys -lio -laudio \
             -lrt -llv2 -lm

all:      $(TARGET).self
pkg:      $(TARGET).pkg

.PHONY: all pkg clean