#ifndef TSS_H
#define TSS_H

#include <stdint.h>

// Task State Segment structure for i386 (32-bit)
typedef struct __attribute__((packed)) {
    uint16_t link;          // Previous TSS selector
    uint16_t reserved0;
    uint32_t esp0;          // Stack pointer for ring 0 (kernel)
    uint16_t ss0;           // Stack segment for ring 0
    uint16_t reserved1;
    uint32_t esp1;          // Stack pointer for ring 1 (unused)
    uint16_t ss1;           // Stack segment for ring 1
    uint16_t reserved2;
    uint32_t esp2;          // Stack pointer for ring 2 (unused)
    uint16_t ss2;           // Stack segment for ring 2
    uint16_t reserved3;
    uint32_t cr3;           // Page directory base
    uint32_t eip;           // Instruction pointer
    uint32_t eflags;        // EFLAGS
    uint32_t eax;           // General registers
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint16_t es;            // Segment selectors
    uint16_t reserved4;
    uint16_t cs;
    uint16_t reserved5;
    uint16_t ss;
    uint16_t reserved6;
    uint16_t ds;
    uint16_t reserved7;
    uint16_t fs;
    uint16_t reserved8;
    uint16_t gs;
    uint16_t reserved9;
    uint16_t ldt;           // LDT selector
    uint16_t reserved10;
    uint16_t trap;          // Debug trap flag
    uint16_t iomap_base;    // I/O permission bitmap offset
} tss_t;

// TSS functions
void tss_init(void);
void tss_set_kernel_stack(uint32_t stack);
uint32_t tss_get_kernel_stack(void);

// Global TSS instance
extern tss_t tss;

#endif // TSS_H
