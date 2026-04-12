#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Process states
typedef enum {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_WAITING,      // Waiting for child process
    PROCESS_STATE_ZOMBIE,       // Terminated but not yet reaped
    PROCESS_STATE_TERMINATED
} process_state_t;

// Execution domain
typedef enum {
    PROCESS_KIND_KERNEL_THREAD,
    PROCESS_KIND_USER
} process_kind_t;

// i386 context structure (32-bit)
typedef struct {
    // Callee-saved registers (must be preserved across function calls)
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;

    // Stack pointer
    uint32_t esp;

    // Instruction pointer (return address)
    uint32_t eip;

    // EFLAGS register
    uint32_t eflags;
} context_t;

// Process Control Block (PCB)
typedef struct process {
    uint32_t pid;                    // Process ID
    char name[32];                   // Process name
    process_kind_t kind;             // Kernel thread vs user process

    context_t context;               // CPU context
    process_state_t state;           // Current state

    // Memory management (32-bit pointers)
    uint32_t* page_directory;       // Process's CR3 value (page directory base)
    uint32_t heap_start;            // Start of heap (for sbrk)
    uint32_t heap_current;          // Current heap end
    uint32_t heap_max;              // Maximum heap size
    uint32_t stack_bottom;          // Bottom of user stack
    uint32_t stack_top;             // Top of user stack (grows down)

    // Memory statistics
    size_t pages_allocated;         // Number of pages this process owns
    size_t page_faults;             // Page fault counter

    void* kernel_stack;             // Kernel stack base
    size_t kernel_stack_size;       // Kernel stack size

    uint32_t ticks_total;           // Total CPU ticks used
    uint32_t ticks_remaining;       // Ticks left in current quantum
    uint32_t priority;              // Process priority (0 = highest)
    
    // Process relationships
    uint32_t parent_pid;            // Parent process ID
    int exit_status;                // Exit status (for zombie processes)

    // Signal handling
    uint32_t pending_signals;       // Bitmask of pending signals
    uint32_t signal_mask;           // Bitmask of blocked signals
    void* signal_handlers[32];      // Signal handlers (SIG_DFL, SIG_IGN, or function pointer)

    // File descriptors (per-process)
    void* fd_table;                 // Pointer to process's file descriptor table
    
    struct process* next;           // Next process in queue
    struct process* prev;           // Previous process in queue
    
    void (*entry_point)(void);      // Kernel-thread entry point
    uint32_t user_entry;            // User-mode entry point
    uint64_t wakeup_tick;           // Wake time for sleeping processes
} process_t;

// Maximum number of processes
#define MAX_PROCESSES 64
#define KERNEL_STACK_SIZE 8192  // 8KB kernel stack per process
#define DEFAULT_QUANTUM 10      // Default time quantum (in timer ticks)

// Process management functions
void process_init(void);
process_t* process_create(const char* name, void (*entry_point)(void), uint32_t priority);
process_t* process_create_user(const char* name, uint32_t priority);
void process_destroy(process_t* process);
void process_yield(void);
void process_sleep(uint32_t ticks);
void process_block(void);
void process_unblock(process_t* process);
void process_exit(int status);

// Helper functions for fork/exec
process_t* allocate_process_struct(void);
void free_process_struct(process_t* process);
process_t* find_zombie_child(uint32_t parent_pid);
void ready_queue_push(process_t* proc);

// Context switching
void context_switch(context_t* old_context, context_t* new_context);

// Getters
process_t* process_get_current(void);
uint32_t process_get_pid(void);
const char* process_get_name(void);
process_state_t process_get_state(process_t* process);
process_kind_t process_get_kind(process_t* process);

// Debug
void process_print_all(void);

// User-mode bootstrap
void process_enter_user_mode(void);

#endif // PROCESS_H
