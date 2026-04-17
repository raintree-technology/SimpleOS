#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include "kernel/isr.h"

// Exception handlers
void page_fault_handler(registers_t* regs);
void general_protection_fault_handler(registers_t* regs);
void double_fault_handler(registers_t* regs);
void invalid_opcode_handler(registers_t* regs);
void stack_fault_handler(registers_t* regs);

void init_exceptions(void);

#endif // EXCEPTIONS_H
