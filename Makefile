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

LDFLAGS   += -L$(PORTLIBS)/lib
LIBS      += -lSDL -lpad -lm

all:      $(TARGET).self
pkg:      $(TARGET).pkg

.PHONY: all pkg clean
