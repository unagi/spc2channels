# spc2channels - Makefile
#
# Dependencies: libgme (game-music-emu)
#   Install: https://github.com/libgme/game-music-emu
#
# Build modes:
#   make              -> dynamic link (requires libgme installed)
#   make static       -> static link (portable binary, needs libgme.a)
#   make GME_PREFIX=/path/to/gme  -> custom libgme location

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra
TARGET    = spc2channels
SRC       = spc2channels.c

# libgme location (override with GME_PREFIX=/your/path)
GME_PREFIX ?= /usr/local

GME_CFLAGS  = -I$(GME_PREFIX)/include
GME_LDFLAGS = -L$(GME_PREFIX)/lib
GME_LIBS    = -lgme

# Static link needs libstdc++ and libm
STATIC_LIBS = $(GME_PREFIX)/lib/libgme.a -lstdc++ -lm

.PHONY: all static clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(GME_CFLAGS) -o $@ $< $(GME_LDFLAGS) $(GME_LIBS)

static: $(SRC)
	$(CC) $(CFLAGS) $(GME_CFLAGS) -o $(TARGET) $< $(STATIC_LIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

PREFIX ?= /usr/local
