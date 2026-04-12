#ifndef PANIC_H
#define PANIC_H

#include "kernel/isr.h"

// Panic and halt the system
void panic(const char* msg);

// Panic with register dump
void panic_with_regs(const char* msg, registers_t* regs);

// General exception handler
void exception_handler(registers_t* regs);

// Assert macro for kernel debugging
#define ASSERT(condition) do { \
    if (!(condition)) { \
        panic("Assertion failed: " #condition); \
    } \
} while (0)

#endif // PANIC_H
