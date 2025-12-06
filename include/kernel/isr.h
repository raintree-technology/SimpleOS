// This header file defines structures and functions for handling interrupt service routines (ISRs).
// It provides a mechanism for registering custom interrupt handlers and a structure to represent CPU registers during an interrupt.
// 32-bit version for i386

#ifndef ISR_H
#define ISR_H

#include <stdint.h>

// Register structure pushed by isr_common_stub (32-bit)
// Must match the order in asm_functions.s
typedef struct {
    // Segment registers (pushed by isr_common_stub)
    uint32_t gs, fs, es, ds;

    // General purpose registers (pushed by pushal)
    // pushal order: eax, ecx, edx, ebx, esp, ebp, esi, edi
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;

    // Pushed by ISR stub
    uint32_t int_no, err_code;

    // Pushed by CPU on interrupt
    uint32_t eip, cs, eflags, esp, ss;
} registers_t;

// IRQ definitions
#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39
#define IRQ8 40
#define IRQ9 41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

typedef void (*isr_t)(registers_t*);

void register_interrupt_handler(uint8_t n, isr_t handler);
void isr_handler(registers_t* regs);

#endif
