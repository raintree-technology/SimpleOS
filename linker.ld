/* This is the linker script for SimpleOS, defining the memory layout of the kernel.
   It specifies the entry point and organizes various sections of the kernel in memory. */

/* SimpleOS/linker.ld */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS {
    . = 1M;

    /* All loadable content in one contiguous block starting at 1M */
    .text : {
        KEEP(*(.multiboot))
        *(.text)
        *(.rodata)
    }

    .data ALIGN(4K) : {
        *(.data)
    }

    .bss ALIGN(16) : {
        *(COMMON)
        *(.bss)
    }
}