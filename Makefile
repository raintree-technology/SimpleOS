# Main build for the 32-bit freestanding SimpleOS target.

CROSS_PREFIX ?= i686-elf-
CC = $(CROSS_PREFIX)gcc
AS = $(CROSS_PREFIX)as
ASFLAGS ?= --32
# Host tool name retained from the GRUB toolchain package, not the kernel target.
GRUB_MKRESCUE ?= x86_64-elf-grub-mkrescue
QEMU ?= qemu-system-i386
ARCH_LABEL ?= i386
ARCH_DIR ?= i386
ARCH_SRC_DIR = src/arch/$(ARCH_DIR)
ARCH_INCLUDE_DIR = include/arch/$(ARCH_DIR)
ARCH_BUILD_DIR = arch/$(ARCH_DIR)

# Compiler flags
CFLAGS = -ffreestanding -c -m32 -O2 -Wall -Wextra -I./include -I./$(ARCH_INCLUDE_DIR) -I./include/kernel -I./include/mm -I./include/drivers -I./include/fs -I./include/ipc -I./include/lib -I./include/boot

# Linker flags
LDFLAGS = -nostdlib -T linker.ld -Wl,-m,elf_i386 -Wl,--no-warn-rwx-segments -Wl,--no-warn-execstack -Wl,--verbose

# Source files organized by subsystem
KERNEL_SRC = src/kernel/kernel.c src/kernel/scheduler.c src/kernel/process.c \
             src/kernel/syscall.c src/kernel/panic.c

MM_SRC = src/mm/kmalloc.c src/mm/pmm.c src/mm/vmm.c

DRIVER_SRC = src/drivers/terminal.c src/drivers/keyboard.c src/drivers/ports.c \
             src/drivers/timer.c src/drivers/vt.c

FS_SRC = src/fs/fs.c

IPC_SRC = src/ipc/pipe.c src/ipc/signal.c

LIB_SRC = src/lib/elf.c src/lib/string.c

BOOT_SRC = src/boot/exceptions.c

ARCH_SRC = $(ARCH_SRC_DIR)/tss.c $(ARCH_SRC_DIR)/usermode.c


# Assembly sources
ASM_SRC = $(ARCH_SRC_DIR)/asm_functions.s $(ARCH_SRC_DIR)/boot.s \
          $(ARCH_SRC_DIR)/context_switch.s

# All C sources
SRC = $(KERNEL_SRC) $(MM_SRC) $(DRIVER_SRC) $(FS_SRC) $(IPC_SRC) $(LIB_SRC) \
      $(BOOT_SRC) $(ARCH_SRC)

# Object files - put them in build directory
OBJ_DIR = build
OBJ = $(SRC:src/%.c=$(OBJ_DIR)/%.o) $(ASM_SRC:src/%.s=$(OBJ_DIR)/%.o)

# Output file
OUTPUT = kernel.bin
ISO = simpleos.iso

# Default target
all: check-toolchain $(ISO)

help:
	@echo "SimpleOS build targets"
	@echo "  make            Build $(ISO)"
	@echo "  make run        Build and boot in QEMU"
	@echo "  make clean      Remove build artifacts"
	@echo ""
	@echo "Toolchain variables"
	@echo "  CROSS_PREFIX=$(CROSS_PREFIX)"
	@echo "  GRUB_MKRESCUE=$(GRUB_MKRESCUE)"
	@echo "  QEMU=$(QEMU)"
	@echo "  ARCH_LABEL=$(ARCH_LABEL)"
	@echo "  ARCH_SRC_DIR=$(ARCH_SRC_DIR)"
	@echo "  ARCH_INCLUDE_DIR=$(ARCH_INCLUDE_DIR)"
	@echo "  ASFLAGS=$(ASFLAGS)"

check-toolchain:
	@command -v "$(CC)" >/dev/null 2>&1 || { \
		echo "Missing compiler: $(CC)"; \
		echo "Install an i686 ELF cross-toolchain or use ./build.sh if Docker is available."; \
		exit 1; \
	}
	@command -v "$(AS)" >/dev/null 2>&1 || { \
		echo "Missing assembler: $(AS)"; \
		echo "Install an i686 ELF cross-toolchain or use ./build.sh if Docker is available."; \
		exit 1; \
	}
	@command -v "$(GRUB_MKRESCUE)" >/dev/null 2>&1 || { \
		echo "Missing ISO tool: $(GRUB_MKRESCUE)"; \
		echo "Install the GRUB mkrescue toolchain required to build $(ISO)."; \
		exit 1; \
	}

# Create build directories
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/kernel
	mkdir -p $(OBJ_DIR)/mm
	mkdir -p $(OBJ_DIR)/drivers
	mkdir -p $(OBJ_DIR)/fs
	mkdir -p $(OBJ_DIR)/ipc
	mkdir -p $(OBJ_DIR)/lib
	mkdir -p $(OBJ_DIR)/boot
	mkdir -p $(OBJ_DIR)/$(ARCH_BUILD_DIR)

# Link object files to create the kernel binary
$(OUTPUT): $(OBJ_DIR) $(OBJ)
	$(CC) -m32 $(LDFLAGS) -o $@ $(OBJ)

# Compile C files
$(OBJ_DIR)/%.o: src/%.c
	$(CC) $(CFLAGS) $< -o $@

# Assemble assembly files
$(OBJ_DIR)/%.o: src/%.s
	$(AS) $(ASFLAGS) $< -o $@

# Create bootable ISO (using i386-pc modules for legacy BIOS boot)
$(ISO): $(OUTPUT)
	mkdir -p iso/boot/grub
	cp $(OUTPUT) iso/boot/
	cp boot/grub/grub.cfg iso/boot/grub/
	GRUB_PKGDATADIR=grub-bios/lib/grub $(GRUB_MKRESCUE) -d grub-bios/lib/grub/i386-pc -o $(ISO) iso/
	rm -rf iso/

# Run in QEMU (32-bit)
run: check-toolchain $(ISO)
	@command -v "$(QEMU)" >/dev/null 2>&1 || { \
		echo "Missing emulator: $(QEMU)"; \
		echo "Install QEMU to boot $(ISO) locally."; \
		exit 1; \
	}
	$(QEMU) -cdrom $(ISO) -m 512M

# Clean up built files
clean:
	rm -rf $(OBJ_DIR)
	rm -f $(OUTPUT) $(ISO)
	rm -rf iso/

# Phony targets
.PHONY: all clean run help check-toolchain
