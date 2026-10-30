CC   = gcc

BINDIR = /usr/bin

TARGET_MACHINE := $(shell $(CC) -dumpmachine)

ifeq ($(findstring x86_64,$(TARGET_MACHINE)),x86_64)

ELF_FORMAT = elf64-x86-64
ELF_ARCH   = i386:x86-64

else ifneq ($(findstring i386,$(TARGET_MACHINE)),)
	
ELF_ARCH   = i386
STATIC = -static

else ifneq ($(findstring i686,$(TARGET_MACHINE)),)

ELF_FORMAT = elf32-i386
ELF_ARCH   = i386

else

$(error Unsupported target: $(TARGET_MACHINE))

endif

SRCS = xadlbank.c

OBJS= $(SRCS:.c=.o)

TARGET = xadlbank

CFLAGS += -Os -Wall -Wshadow -Wextra -pedantic --std=gnu99 -ffunction-sections -fdata-sections

LDFLAGS += $(STATIC) -Wl,--gc-sections,--sort-common,-s -lX11 -lm

.PHONY: all clean install

all:  $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS) $(CFLAGS) 
	
install: $(TARGET2)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

clean:
	rm -f $(TARGET) $(OBJS)

