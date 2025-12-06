#include <stdint.h>
#include "../include/isr.h"
#include "../include/terminal.h"
#include "../include/panic.h"

// Page fault error code bits
#define PF_PRESENT  (1 << 0)  // Page not present
#define PF_WRITE    (1 << 1)  // Write access
#define PF_USER     (1 << 2)  // User mode
#define PF_RESERVED (1 << 3)  // Reserved bit set
#define PF_FETCH    (1 << 4)  // Instruction fetch

// Helper to print a number in hex (32-bit)
static void print_hex_value(uint32_t value) {
    terminal_writestring("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t digit = (value >> i) & 0xF;
        if (digit < 10) {
            terminal_putchar('0' + digit);
        } else {
            terminal_putchar('A' + digit - 10);
        }
    }
}

// Page fault handler (exception 14)
void page_fault_handler(registers_t* regs) {
    // Get the faulting address from CR2
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    // Analyze the error code
    uint32_t error = regs->err_code;

    terminal_writestring("\n\n================================================================================\n");
    terminal_writestring("                                PAGE FAULT\n");
    terminal_writestring("================================================================================\n\n");

    terminal_writestring("Faulting address: ");
    print_hex_value(faulting_address);
    terminal_writestring("\n\n");

    terminal_writestring("Error code: ");
    print_hex_value(error);
    terminal_writestring(" - ");

    // Decode error code
    if (error & PF_PRESENT) {
        terminal_writestring("protection violation");
    } else {
        terminal_writestring("page not present");
    }

    terminal_writestring(", during ");
    terminal_writestring((error & PF_WRITE) ? "write" : "read");

    terminal_writestring(" in ");
    terminal_writestring((error & PF_USER) ? "user" : "kernel");
    terminal_writestring(" mode");

    if (error & PF_RESERVED) {
        terminal_writestring(", reserved bit set");
    }

    if (error & PF_FETCH) {
        terminal_writestring(", instruction fetch");
    }

    terminal_writestring("\n\n");

    // Print instruction pointer
    terminal_writestring("Instruction pointer: ");
    print_hex_value(regs->eip);
    terminal_writestring("\n");

    // In the future, we would:
    // 1. Check if this is a valid page that needs to be loaded (demand paging)
    // 2. Check if this is a stack growth situation
    // 3. Check if this is a copy-on-write page
    // 4. Kill the process if it's an invalid access

    // For now, panic with full register dump
    panic_with_regs("Unhandled page fault", regs);
}

// General protection fault handler (exception 13)
void general_protection_fault_handler(registers_t* regs) {
    uint32_t error = regs->err_code;

    terminal_writestring("\n\n================================================================================\n");
    terminal_writestring("                          GENERAL PROTECTION FAULT\n");
    terminal_writestring("================================================================================\n\n");

    if (error != 0) {
        terminal_writestring("Error code: ");
        print_hex_value(error);
        terminal_writestring("\n");

        // Decode selector index
        uint16_t index = (error >> 3) & 0x1FFF;
        uint8_t tbl = (error >> 1) & 0x3;
        bool external = error & 0x1;

        terminal_writestring("Segment selector: ");
        print_hex_value(index);

        terminal_writestring(", Table: ");
        if (tbl == 0) terminal_writestring("GDT");
        else if (tbl == 1) terminal_writestring("IDT");
        else terminal_writestring("LDT");

        if (external) {
            terminal_writestring(", External event");
        }
        terminal_writestring("\n");
    }

    terminal_writestring("Instruction pointer: ");
    print_hex_value(regs->eip);
    terminal_writestring("\n");

    panic_with_regs("General protection fault", regs);
}

// Double fault handler (exception 8)
void double_fault_handler(registers_t* regs) {
    // Double fault is VERY bad - the CPU couldn't handle an exception
    // while handling another exception. Often indicates stack issues.

    terminal_writestring("\n\n================================================================================\n");
    terminal_writestring("                              DOUBLE FAULT\n");
    terminal_writestring("================================================================================\n\n");
    terminal_writestring("CRITICAL: Exception occurred while handling another exception!\n");
    terminal_writestring("This often indicates stack overflow or corrupted interrupt handlers.\n\n");

    panic_with_regs("Double fault - system integrity compromised", regs);
}

// Invalid opcode handler (exception 6)
void invalid_opcode_handler(registers_t* regs) {
    terminal_writestring("\n\n================================================================================\n");
    terminal_writestring("                            INVALID OPCODE\n");
    terminal_writestring("================================================================================\n\n");
    terminal_writestring("CPU encountered an invalid or undefined instruction.\n");
    terminal_writestring("This may indicate corrupted code or incompatible CPU instructions.\n\n");

    terminal_writestring("Instruction pointer: ");
    print_hex_value(regs->eip);
    terminal_writestring("\n");

    // Could dump the bytes at EIP to see what instruction caused this

    panic_with_regs("Invalid opcode", regs);
}

// Stack fault handler (exception 12)
void stack_fault_handler(registers_t* regs) {
    terminal_writestring("\n\n================================================================================\n");
    terminal_writestring("                             STACK FAULT\n");
    terminal_writestring("================================================================================\n\n");

    uint32_t error = regs->err_code;
    if (error != 0) {
        terminal_writestring("Segment selector: ");
        print_hex_value(error);
        terminal_writestring("\n");
    }

    terminal_writestring("Stack pointer: ");
    print_hex_value(regs->esp);
    terminal_writestring("\n");

    panic_with_regs("Stack segment fault", regs);
}

// Initialize exception handlers
void init_exceptions(void) {
    // Register specific handlers for important exceptions
    register_interrupt_handler(6, invalid_opcode_handler);
    register_interrupt_handler(8, double_fault_handler);
    register_interrupt_handler(12, stack_fault_handler);
    register_interrupt_handler(13, general_protection_fault_handler);
    register_interrupt_handler(14, page_fault_handler);

    terminal_writestring("Exception handlers initialized\n");
}
