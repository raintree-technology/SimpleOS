# This assembly file contains low-level functions for i386 system initialization and interrupt handling.
# It includes routines for loading GDT and IDT, enabling paging, and handling interrupt service routines (ISRs).
# 32-bit version for v86 browser compatibility

.global load_gdt
.global load_idt
.global enable_paging
.global isr0
.global isr1
.global isr2
.global isr3
.global isr4
.global isr5
.global isr6
.global isr7
.global isr8
.global isr10
.global isr11
.global isr12
.global isr13
.global isr14
.global isr16
.global isr17
.global isr18
.global isr19
.global isr20
.global irq0
.global irq1
.global isr128

# void load_gdt(gdt_ptr_t* gdt_ptr)
# 32-bit: argument passed on stack at [esp+4]
load_gdt:
    mov 4(%esp), %eax           # Get GDT pointer from stack
    lgdt (%eax)
    mov $0x10, %ax              # Data segment selector
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    # Far jump to reload CS
    ljmp $0x08, $.reload_cs
.reload_cs:
    ret

# void load_idt(idt_ptr_t* idt_ptr)
load_idt:
    mov 4(%esp), %eax           # Get IDT pointer from stack
    lidt (%eax)
    ret

# void enable_paging(uint32_t page_directory)
enable_paging:
    mov 4(%esp), %eax           # Get page directory address
    mov %eax, %cr3
    mov %cr0, %eax
    or $0x80000001, %eax        # Enable paging (PG) and protection (PE)
    mov %eax, %cr0
    ret

# CPU exceptions (0-31)
# Some exceptions push error codes, others don't

# 0: Divide by zero
isr0:
    cli
    pushl $0        # Dummy error code
    pushl $0        # Interrupt number
    jmp isr_common_stub

# 1: Debug
isr1:
    cli
    pushl $0
    pushl $1
    jmp isr_common_stub

# 2: Non-maskable interrupt
isr2:
    cli
    pushl $0
    pushl $2
    jmp isr_common_stub

# 3: Breakpoint
isr3:
    cli
    pushl $0
    pushl $3
    jmp isr_common_stub

# 4: Overflow
isr4:
    cli
    pushl $0
    pushl $4
    jmp isr_common_stub

# 5: Bound range exceeded
isr5:
    cli
    pushl $0
    pushl $5
    jmp isr_common_stub

# 6: Invalid opcode
isr6:
    cli
    pushl $0
    pushl $6
    jmp isr_common_stub

# 7: Device not available
isr7:
    cli
    pushl $0
    pushl $7
    jmp isr_common_stub

# 8: Double fault (pushes error code)
isr8:
    cli
    pushl $8        # Interrupt number (error code already pushed)
    jmp isr_common_stub

# 10: Invalid TSS (pushes error code)
isr10:
    cli
    pushl $10
    jmp isr_common_stub

# 11: Segment not present (pushes error code)
isr11:
    cli
    pushl $11
    jmp isr_common_stub

# 12: Stack segment fault (pushes error code)
isr12:
    cli
    pushl $12
    jmp isr_common_stub

# 13: General protection fault (pushes error code)
isr13:
    cli
    pushl $13
    jmp isr_common_stub

# 14: Page fault (pushes error code)
isr14:
    cli
    pushl $14
    jmp isr_common_stub

# 16: x87 FPU error
isr16:
    cli
    pushl $0
    pushl $16
    jmp isr_common_stub

# 17: Alignment check (pushes error code)
isr17:
    cli
    pushl $17
    jmp isr_common_stub

# 18: Machine check
isr18:
    cli
    pushl $0
    pushl $18
    jmp isr_common_stub

# 19: SIMD floating-point
isr19:
    cli
    pushl $0
    pushl $19
    jmp isr_common_stub

# 20: Virtualization
isr20:
    cli
    pushl $0
    pushl $20
    jmp isr_common_stub

isr_common_stub:
    # Save all general-purpose registers (32-bit has 8)
    pushal                      # Push eax, ecx, edx, ebx, esp, ebp, esi, edi

    # Save segment registers
    push %ds
    push %es
    push %fs
    push %gs

    # Load kernel data segment
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    # Pass pointer to registers struct (esp) as argument
    pushl %esp
    call isr_handler
    addl $4, %esp               # Clean up argument

    # Restore segment registers
    pop %gs
    pop %fs
    pop %es
    pop %ds

    # Restore all general-purpose registers
    popal

    # Clean up error code and interrupt number (2 * 4 bytes)
    addl $8, %esp
    iret

# IRQ handlers
irq0:
    cli
    pushl $0
    pushl $32
    jmp isr_common_stub

irq1:
    cli
    pushl $0
    pushl $33
    jmp isr_common_stub

# System call handler (INT 0x80 = 128)
# Note: cli added for consistency with other ISR stubs.
isr128:
    cli
    pushl $0
    pushl $128
    jmp isr_common_stub
