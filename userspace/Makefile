CROSS_PREFIX ?= i686-elf-
CC = $(CROSS_PREFIX)gcc
LD = $(CROSS_PREFIX)ld
CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-stack-protector -m32
LDFLAGS = -m elf_i386 -T link.ld -nostdlib

PROGRAMS = hello shell grep wc

all: $(addsuffix .elf,$(PROGRAMS))

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o
	$(LD) $(LDFLAGS) $< -o $@

# Convert to C array for embedding in kernel
%_binary.h: %.elf
	xxd -i $< | sed -e 's/unsigned char/static const unsigned char/' -e 's/unsigned int/static const unsigned int/' > $@

headers: $(addsuffix _binary.h,$(PROGRAMS))

clean:
	rm -f *.o *.elf

distclean: clean
	rm -f *_binary.h

.PHONY: all clean distclean headers
