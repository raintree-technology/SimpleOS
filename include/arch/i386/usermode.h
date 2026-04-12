#ifndef USERMODE_H
#define USERMODE_H

#include "kernel/process.h"

// User mode functions
__attribute__((noreturn))
void switch_to_user_mode(void* entry_point, void* user_stack);
process_t* create_user_process(const char* name, void (*entry_point)(void));
void test_user_mode(void);

#endif // USERMODE_H
