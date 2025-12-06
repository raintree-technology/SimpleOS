#include "../include/tss.h"
#include "../include/terminal.h"
#include <stdint.h>

// Global TSS instance
tss_t tss;

// Default kernel stack (used when no process-specific stack)
static uint8_t default_kernel_stack[8192] __attribute__((aligned(16)));

// Initialize the TSS (32-bit version)
void tss_init(void) {
    // Clear the TSS structure
    uint8_t* tss_ptr = (uint8_t*)&tss;
    for (size_t i = 0; i < sizeof(tss_t); i++) {
        tss_ptr[i] = 0;
    }

    // Set up the kernel stack pointer (ESP0)
    // This will be used when transitioning from ring 3 to ring 0
    tss.esp0 = (uint32_t)(default_kernel_stack + sizeof(default_kernel_stack));
    tss.ss0 = 0x10;  // Kernel data segment selector

    // Set I/O permission bitmap offset to beyond TSS size
    // This effectively disables I/O permissions
    tss.iomap_base = sizeof(tss_t);

    terminal_writestring("TSS initialized at ");
    terminal_print_hex((uint32_t)&tss);
    terminal_writestring("\n");
}

// Set the kernel stack for ring 0
void tss_set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

// Get the current kernel stack
uint32_t tss_get_kernel_stack(void) {
    return tss.esp0;
}
