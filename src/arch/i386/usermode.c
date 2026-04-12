#include <stdint.h>
#include "kernel/process.h"
#include "arch/i386/tss.h"
#include "drivers/terminal.h"
#include "kernel/panic.h"

// Segment selectors (32-bit)
#define KERNEL_CODE_SEL 0x08  // GDT entry 1
#define KERNEL_DATA_SEL 0x10  // GDT entry 2
#define USER_CODE_SEL   0x1B  // GDT entry 3 | RPL 3
#define USER_DATA_SEL   0x23  // GDT entry 4 | RPL 3

// Switch current process to user mode (32-bit)
__attribute__((noreturn))
void switch_to_user_mode(void* entry_point, void* user_stack) {
    uint32_t eflags;

    // Get current process
    process_t* current = process_get_current();
    if (!current) {
        panic("switch_to_user_mode: No current process!");
    }

    // Set the kernel stack in TSS for when we return from user mode
    tss_set_kernel_stack((uint32_t)current->kernel_stack + current->kernel_stack_size);

    terminal_writestring("Switching to user mode: entry=");
    terminal_print_hex((uint32_t)entry_point);
    terminal_writestring(", stack=");
    terminal_print_hex((uint32_t)user_stack);
    terminal_writestring("\n");

    asm volatile("pushfl; popl %0" : "=r"(eflags));
    eflags |= 0x200;

    // Use inline assembly to switch to user mode (32-bit iret)
    asm volatile(
        "cli\n"
        "mov %0, %%esp\n"        // Set user stack
        "pushl %1\n"             // Push user data segment
        "pushl %0\n"             // Push user stack pointer
        "pushl %2\n"             // Push user EFLAGS
        "pushl %3\n"             // Push user code segment
        "pushl %4\n"             // Push entry point
        "mov %1, %%ax\n"         // Load user data segment
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "iret\n"                 // Return to user mode (32-bit)
        :
        : "r"(user_stack), "i"(USER_DATA_SEL), "r"(eflags), "i"(USER_CODE_SEL), "r"(entry_point)
        : "memory"
    );
    __builtin_unreachable();
}

// Test function that will run in user mode (32-bit syscall convention)
void user_mode_test(void) {
    // This will run in ring 3!
    // Try to make a system call to write
    // 32-bit Linux syscall: eax=syscall#, ebx=arg1, ecx=arg2, edx=arg3
    static const char msg[] = "Hello from user mode!\n";
    asm volatile(
        "mov $2, %%eax\n"        // sys_write
        "mov $1, %%ebx\n"        // stdout
        "mov %0, %%ecx\n"        // message
        "mov %1, %%edx\n"        // length
        "int $0x80\n"
        :
        : "r"(msg), "i"(sizeof(msg) - 1)
        : "eax", "ebx", "ecx", "edx"
    );

    // Test getpid syscall
    uint32_t pid;
    asm volatile(
        "mov $4, %%eax\n"        // sys_getpid
        "int $0x80\n"
        "mov %%eax, %0"
        : "=r"(pid) : : "eax"
    );

    // Write PID message
    static const char pid_msg[] = "User mode process PID obtained!\n";
    asm volatile(
        "mov $2, %%eax\n"        // sys_write
        "mov $1, %%ebx\n"        // stdout
        "mov %0, %%ecx\n"        // message
        "mov %1, %%edx\n"        // length
        "int $0x80\n"
        :
        : "r"(pid_msg), "i"(sizeof(pid_msg) - 1)
        : "eax", "ebx", "ecx", "edx"
    );

    // Loop forever using sleep syscall
    while (1) {
        static const char loop_msg[] = "[User Mode] Still running...\n";
        asm volatile(
            "mov $2, %%eax\n"        // sys_write
            "mov $1, %%ebx\n"        // stdout
            "mov %0, %%ecx\n"        // message
            "mov %1, %%edx\n"        // length
            "int $0x80\n"
            :
            : "r"(loop_msg), "i"(sizeof(loop_msg) - 1)
            : "eax", "ebx", "ecx", "edx"
        );

        // Sleep for 3 seconds
        asm volatile(
            "mov $5, %%eax\n"        // sys_sleep
            "mov $3000, %%ebx\n"     // 3000ms
            "int $0x80\n"
            ::: "eax", "ebx"
        );
    }
}

// Create a user mode process
process_t* create_user_process(const char* name, void (*entry_point)(void)) {
    process_t* proc = process_create_user(name, 1);
    if (!proc) {
        return NULL;
    }

    proc->user_entry = (uint32_t)entry_point;

    terminal_writestring("Created user mode process: ");
    terminal_writestring(name);
    terminal_writestring("\n");

    return proc;
}

// Test user mode functionality
void test_user_mode(void) {
    terminal_writestring("\n=== Testing User Mode ===\n");
    if (!create_user_process("user-test", user_mode_test)) {
        panic("Failed to create user test process!");
    }
}
