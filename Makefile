CC      ?= gcc
BINDIR  ?= /usr/bin

TARGET_MACHINE := $(shell $(CC) -dumpmachine)

# Old 32-bit x86 targets are built statically by default.
# Override on the command line, e.g.:  make STATIC=   or   make STATIC=-static
ifneq ($(findstring i386,$(TARGET_MACHINE)),)
STATIC ?= -static
endif
STATIC ?=

SRCS   = xadlbank.c
OBJS   = $(SRCS:.c=.o)
TARGET = xadlbank

# xadlbank.c includes the file chooser implementation directly
DEPS   = xfileselect/xfileselect.c xfileselect/xfileselect.h xbmfont/xbmfont.h

CFLAGS  += -Os -Wall -Wshadow -Wextra -pedantic --std=gnu99 -ffunction-sections -fdata-sections -I./xbmfont
LDFLAGS += $(STATIC) -Wl,--gc-sections,--sort-common,-s

# make PRETTYFONTS=1 : the 9x15 font (scaled x2, x3, x4 on large screens)
# make SMALL_FONTS=1 : only 6x13 (scaled on large screens), about 17 KB smaller
ifeq ($(PRETTYFONTS),1)
CFLAGS  += -DPRETTYFONTS
endif
ifeq ($(SMALL_FONTS),1)
CFLAGS  += -DXADLBANK_SMALL_FONTS
endif
LDLIBS  += -lX11

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

xadlbank.o: xadlbank.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

clean:
	rm -f $(TARGET) $(OBJS)
