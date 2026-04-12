#include <stdint.h>
#include <stdarg.h>
#include "drivers/terminal.h"
#include "kernel/isr.h"

// Helper to print hex values (32-bit)
static void print_hex(const char* label, uint32_t value) {
    terminal_writestring(label);
    terminal_writestring(": 0x");

    // Print 8 hex digits for 32-bit value
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        if (digit < 10) {
            terminal_putchar('0' + digit);
        } else {
            terminal_putchar('A' + digit - 10);
        }
    }
    terminal_writestring("  ");
}

// Kernel panic - halt system with error message
void panic(const char* msg) {
    // Disable interrupts
    asm volatile("cli");

    // Print panic header
    terminal_writestring("\n\n");
    terminal_writestring("================================================================================\n");
    terminal_writestring("                              KERNEL PANIC\n");
    terminal_writestring("================================================================================\n\n");
    terminal_writestring("Error: ");
    terminal_writestring(msg);
    terminal_writestring("\n\n");
    terminal_writestring("System halted. Please restart your computer.\n");

    // Halt forever
    while (1) {
        asm volatile("hlt");
    }
}

// Panic with register dump (32-bit)
void panic_with_regs(const char* msg, registers_t* regs) {
    // Disable interrupts
    asm volatile("cli");

    // Print panic header
    terminal_writestring("\n\n");
    terminal_writestring("================================================================================\n");
    terminal_writestring("                              KERNEL PANIC\n");
    terminal_writestring("================================================================================\n\n");
    terminal_writestring("Error: ");
    terminal_writestring(msg);
    terminal_writestring("\n\n");

    // Print interrupt info
    terminal_writestring("Interrupt: ");
    print_hex("INT", regs->int_no);
    print_hex("ERR", regs->err_code);
    terminal_writestring("\n\n");

    // Print general purpose registers (32-bit)
    terminal_writestring("Registers:\n");
    print_hex("EAX", regs->eax);
    print_hex("EBX", regs->ebx);
    print_hex("ECX", regs->ecx);
    print_hex("EDX", regs->edx);
    terminal_writestring("\n");

    print_hex("ESI", regs->esi);
    print_hex("EDI", regs->edi);
    print_hex("EBP", regs->ebp);
    print_hex("ESP", regs->esp);
    terminal_writestring("\n\n");

    // Print segment registers
    terminal_writestring("Segments:\n");
    print_hex("DS ", regs->ds);
    print_hex("ES ", regs->es);
    print_hex("FS ", regs->fs);
    print_hex("GS ", regs->gs);
    terminal_writestring("\n\n");

    // Print instruction pointer and flags
    terminal_writestring("Execution:\n");
    print_hex("EIP", regs->eip);
    print_hex("CS ", regs->cs);
    print_hex("EFLAGS", regs->eflags);
    terminal_writestring("\n\n");

    terminal_writestring("System halted. Please restart your computer.\n");

    // Halt forever
    while (1) {
        asm volatile("hlt");
    }
}

// Exception messages
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// General exception handler
void exception_handler(registers_t* regs) {
    if (regs->int_no < 32) {
        panic_with_regs(exception_messages[regs->int_no], regs);
    } else {
        panic_with_regs("Unknown exception", regs);
    }
}
