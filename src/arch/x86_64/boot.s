# Multiboot2 header and boot code for SimpleOS (32-bit)
# This file sets up the initial boot environment and calls the kernel

.section .multiboot
.align 8
multiboot_header:
    # Magic number
    .long 0xe85250d6                # Multiboot2 magic
    .long 0                         # Architecture (0 = i386 protected mode)
    .long multiboot_header_end - multiboot_header  # Header length
    .long -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header))  # Checksum

    # End tag
    .word 0                         # Type
    .word 0                         # Flags
    .long 8                         # Size
multiboot_header_end:

.section .data
# Boot GDT - minimal GDT for early boot
.align 8
boot_gdt:
    # Null descriptor (entry 0)
    .quad 0
    # Code segment (entry 1) - 0x08
    # Base=0, Limit=4GB, Present, Ring 0, Code, Readable, 32-bit
    .word 0xFFFF        # Limit low
    .word 0x0000        # Base low
    .byte 0x00          # Base middle
    .byte 0x9A          # Access: Present, Ring 0, Code, Readable
    .byte 0xCF          # Granularity: 4KB pages, 32-bit, Limit high
    .byte 0x00          # Base high
    # Data segment (entry 2) - 0x10
    # Base=0, Limit=4GB, Present, Ring 0, Data, Writable, 32-bit
    .word 0xFFFF        # Limit low
    .word 0x0000        # Base low
    .byte 0x00          # Base middle
    .byte 0x92          # Access: Present, Ring 0, Data, Writable
    .byte 0xCF          # Granularity: 4KB pages, 32-bit, Limit high
    .byte 0x00          # Base high
boot_gdt_end:

boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1   # GDT limit
    .long boot_gdt                       # GDT base

.section .bss
.align 16
stack_bottom:
    .skip 16384                     # 16 KiB stack
stack_top:

.section .text
.global _start
.type _start, @function

# Serial output helper - outputs character in AL to COM1 (0x3F8)
serial_char:
    push %dx
    mov $0x3F8, %dx
    outb %al, %dx
    pop %dx
    ret

_start:
    # Disable interrupts
    cli

    # Debug: Output '1' to serial
    mov $'1', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # Load our own GDT immediately - don't rely on GRUB's GDT
    lgdt boot_gdt_ptr

    # Debug: Output '2' to serial (GDT loaded)
    mov $'2', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # Reload all segment registers with our selectors
    mov $0x10, %ax          # Data segment selector
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Debug: Output '3' to serial (segments loaded)
    mov $'3', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # Far jump to reload CS with code segment selector
    ljmp $0x08, $.reload_cs

.reload_cs:
    # Debug: Output '4' to serial (CS reloaded via far jump)
    mov $'4', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # Now we have valid segments, set up stack
    mov $stack_top, %esp

    # Debug: Output '5' to serial (stack set up)
    mov $'5', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # Clear EFLAGS
    pushl $0
    popf

    # Debug: Output '6' to serial (about to call kernel_main)
    mov $'6', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # Pass multiboot info to kernel_main (32-bit calling convention)
    # Arguments pushed right to left on stack
    pushl %ebx                      # Multiboot info structure pointer
    pushl %eax                      # Multiboot magic number

    # Call kernel
    call kernel_main

    # Debug: Output 'X' to serial (kernel returned - should never happen)
    mov $'X', %al
    mov $0x3F8, %dx
    outb %al, %dx

    # If kernel returns, hang
    cli
1:  hlt
    jmp 1b

.size _start, . - _start
