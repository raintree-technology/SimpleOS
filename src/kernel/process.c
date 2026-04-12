#include "kernel/process.h"
#include "mm/kmalloc.h"
#include "drivers/terminal.h"
#include "kernel/panic.h"
#include "kernel/scheduler.h"
#include "arch/i386/tss.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "ipc/signal.h"
#include "lib/string.h"
#include "arch/i386/usermode.h"

// From syscall.c
extern void init_process_fd_table(process_t* proc);
extern void cleanup_process_fds(process_t* proc);

// Assembly functions
extern void context_switch(context_t* old_context, context_t* new_context);
extern void process_entry_trampoline(void);
extern void process_user_entry_trampoline(void);
extern uint32_t* page_directory;

// Process table
process_t* process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;

// Scheduler queues
static process_t* ready_queue_head = NULL;
static process_t* ready_queue_tail = NULL;
process_t* current_process = NULL;

// Idle process
static process_t idle_process;
static uint8_t idle_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

static void wake_waiting_parent(process_t* child) {
    process_t* parent;

    if (!child || child->parent_pid == 0) {
        return;
    }

    parent = process_find_by_pid(child->parent_pid);
    if (parent && parent->state == PROCESS_STATE_WAITING) {
        parent->state = PROCESS_STATE_READY;
        if (!(parent->prev || parent->next || ready_queue_head == parent)) {
            ready_queue_push(parent);
        }
    }
}

static process_t* alloc_process_common(const char* name, uint32_t priority) {
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (process_table[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        terminal_writestring("Error: Process table full\n");
        return NULL;
    }

    process_t* proc = (process_t*)kzalloc(sizeof(process_t));
    if (!proc) {
        panic("process_create: Out of memory for PCB");
        return NULL;
    }

    proc->kernel_stack = kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        kfree(proc);
        panic("process_create: Out of memory for kernel stack");
        return NULL;
    }

    proc->pid = next_pid++;
    strncpy(proc->name, name, 31);
    proc->name[31] = '\0';
    proc->state = PROCESS_STATE_READY;
    proc->kernel_stack_size = KERNEL_STACK_SIZE;
    proc->priority = priority;
    proc->ticks_total = 0;
    proc->ticks_remaining = DEFAULT_QUANTUM;
    proc->wakeup_tick = 0;
    proc->entry_point = NULL;
    proc->user_entry = 0;

    process_table[slot] = proc;

    init_process_fd_table(proc);
    signal_init_process(proc);

    return proc;
}

// Helper: Add process to ready queue
void ready_queue_push(process_t* proc) {
    if (!proc) {
        return;
    }

    if (proc->prev || proc->next || ready_queue_head == proc || ready_queue_tail == proc) {
        return;
    }

    proc->next = NULL;
    proc->prev = ready_queue_tail;
    
    if (ready_queue_tail) {
        ready_queue_tail->next = proc;
    } else {
        ready_queue_head = proc;
    }
    ready_queue_tail = proc;
}

// Helper: Remove process from ready queue
static void ready_queue_remove(process_t* proc) {
    if (proc->prev) {
        proc->prev->next = proc->next;
    } else {
        ready_queue_head = proc->next;
    }
    
    if (proc->next) {
        proc->next->prev = proc->prev;
    } else {
        ready_queue_tail = proc->prev;
    }
    
    proc->next = NULL;
    proc->prev = NULL;
}

// Helper: Get next ready process
process_t* ready_queue_pop(void) {
    process_t* proc = ready_queue_head;
    if (proc) {
        ready_queue_remove(proc);
    }
    return proc;
}

// Idle process - runs when nothing else is ready
static void idle_task(void) {
    while (1) {
        asm volatile("hlt");
    }
}

// Initialize process management
void process_init(void) {
    // Clear process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i] = NULL;
    }
    
    // Initialize idle process
    idle_process.pid = 0;
    strcpy(idle_process.name, "idle");
    idle_process.kind = PROCESS_KIND_KERNEL_THREAD;
    idle_process.state = PROCESS_STATE_READY;
    idle_process.page_directory = page_directory;
    idle_process.kernel_stack = idle_stack;
    idle_process.kernel_stack_size = KERNEL_STACK_SIZE;
    idle_process.priority = 255;  // Lowest priority
    idle_process.ticks_total = 0;
    idle_process.ticks_remaining = 1;
    idle_process.entry_point = idle_task;
    idle_process.user_entry = 0;
    idle_process.wakeup_tick = 0;
    
    // Set up idle process context (32-bit)
    idle_process.context.esp = (uint32_t)(idle_stack + KERNEL_STACK_SIZE);
    idle_process.context.eip = (uint32_t)idle_task;
    idle_process.context.eflags = 0x202;  // Interrupts enabled
    
    // Idle process is always in table[0]
    process_table[0] = &idle_process;
    
    // Initialize idle process fd table
    init_process_fd_table(&idle_process);
    
    terminal_writestring("Process table ready\n");
}

// Create a new process
process_t* process_create(const char* name, void (*entry_point)(void), uint32_t priority) {
    process_t* proc = alloc_process_common(name, priority);
    if (!proc) {
        return NULL;
    }

    proc->kind = PROCESS_KIND_KERNEL_THREAD;
    proc->page_directory = page_directory;
    proc->entry_point = entry_point;

    uint32_t* stack_top = (uint32_t*)((uint8_t*)proc->kernel_stack + KERNEL_STACK_SIZE);
    proc->context.edi = (uint32_t)entry_point;
    proc->context.esp = (uint32_t)stack_top;
    proc->context.eip = (uint32_t)process_entry_trampoline;
    proc->context.eflags = 0x202;
    proc->context.esi = 0;
    proc->context.ebx = 0;
    proc->context.ebp = 0;

    ready_queue_push(proc);

    terminal_writestring("Spawned task: ");
    terminal_writestring(name);
    terminal_writestring(" [PID ");
    terminal_print_uint(proc->pid);
    terminal_writestring("]\n");
    
    return proc;
}

process_t* process_create_user(const char* name, uint32_t priority) {
    process_t* proc = alloc_process_common(name, priority);
    if (!proc) {
        return NULL;
    }

    proc->kind = PROCESS_KIND_USER;
    proc->page_directory = vmm_create_address_space();
    if (!proc->page_directory) {
        process_destroy(proc);
        panic("process_create_user: Failed to create address space");
        return NULL;
    }

    if (vmm_setup_user_stack(proc) < 0) {
        process_destroy(proc);
        panic("process_create_user: Failed to set up user stack");
        return NULL;
    }

    if (vmm_setup_user_heap(proc) < 0) {
        process_destroy(proc);
        panic("process_create_user: Failed to set up user heap");
        return NULL;
    }

    uint32_t* stack_top = (uint32_t*)((uint8_t*)proc->kernel_stack + KERNEL_STACK_SIZE);
    proc->context.esp = (uint32_t)stack_top;
    proc->context.eip = (uint32_t)process_user_entry_trampoline;
    proc->context.eflags = 0x202;
    proc->context.edi = 0;
    proc->context.esi = 0;
    proc->context.ebx = 0;
    proc->context.ebp = 0;

    ready_queue_push(proc);

    terminal_writestring("Created user process: ");
    terminal_writestring(name);
    terminal_writestring(" (PID ");
    terminal_print_uint(proc->pid);
    terminal_writestring(")\n");

    return proc;
}

// Destroy a process
void process_destroy(process_t* process) {
    if (!process || process == &idle_process) {
        return;
    }
    
    // Remove from queues
    if (process->state == PROCESS_STATE_READY &&
        (process->prev || process->next || ready_queue_head == process)) {
        ready_queue_remove(process);
    }
    
    // Remove from process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i] == process) {
            process_table[i] = NULL;
            break;
        }
    }
    
    // Free resources
    if (process->kernel_stack) {
        kfree(process->kernel_stack);
    }
    
    // Close open file descriptors and free the table
    cleanup_process_fds(process);
    if (process->fd_table) {
        kfree(process->fd_table);
    }

    // Free page directory only for user processes — kernel threads share
    // the kernel page directory and must not destroy it.
    if (process->kind == PROCESS_KIND_USER && process->page_directory) {
        vmm_destroy_address_space(process->page_directory);
    }
    
    kfree(process);
}

// Get current process
process_t* process_get_current(void) {
    return current_process ? current_process : &idle_process;
}

// Get current PID
uint32_t process_get_pid(void) {
    process_t* proc = process_get_current();
    return proc->pid;
}

// Get current process name
const char* process_get_name(void) {
    process_t* proc = process_get_current();
    return proc->name;
}

// Get process state
process_state_t process_get_state(process_t* process) {
    return process ? process->state : PROCESS_STATE_TERMINATED;
}

process_kind_t process_get_kind(process_t* process) {
    return process ? process->kind : PROCESS_KIND_KERNEL_THREAD;
}

// Yield CPU to next process
void process_yield(void) {
    // Update TSS with current process's kernel stack
    process_t* current = process_get_current();
    if (current && current != &idle_process) {
        tss_set_kernel_stack((uint32_t)current->kernel_stack + current->kernel_stack_size);
    }

    // This will be called from timer interrupt
    schedule();
}

// Block current process
void process_block(void) {
    if (current_process && current_process != &idle_process) {
        current_process->state = PROCESS_STATE_BLOCKED;
        schedule();
    }
}

// Unblock a process
void process_unblock(process_t* process) {
    if (process && process->state == PROCESS_STATE_BLOCKED) {
        process->state = PROCESS_STATE_READY;
        ready_queue_push(process);
    }
}

// Exit current process
void process_exit(int status) {
    if (current_process && current_process != &idle_process) {
        terminal_writestring("Process exiting: ");
        terminal_writestring(current_process->name);
        terminal_writestring(" with status ");
        terminal_print_int(status);
        terminal_writestring("\n");

        current_process->exit_status = status;
        if (current_process->parent_pid != 0) {
            current_process->state = PROCESS_STATE_ZOMBIE;
            wake_waiting_parent(current_process);
        } else {
            current_process->state = PROCESS_STATE_TERMINATED;
        }

        schedule();
        
        panic("process_exit: schedule() returned!");
    }
}

// Print all processes (for debugging)
void process_print_all(void) {
    terminal_writestring("\nProcess List:\n");
    terminal_writestring("PID  Name                     State      Ticks\n");
    terminal_writestring("---  ----------------------  ---------  ------\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = process_table[i];
        if (proc) {
            // Print PID with padding
            terminal_print_uint(proc->pid);
            if (proc->pid < 10) terminal_writestring("    ");
            else if (proc->pid < 100) terminal_writestring("   ");
            else terminal_writestring("  ");

            // Print name with padding (up to 24 chars)
            terminal_writestring(proc->name);
            int name_len = 0;
            const char* p = proc->name;
            while (*p++) name_len++;
            for (int j = name_len; j < 24; j++) terminal_writestring(" ");

            // Print state
            const char* state_str = "UNKNOWN";
            switch (proc->state) {
                case PROCESS_STATE_READY:      state_str = "READY    "; break;
                case PROCESS_STATE_RUNNING:    state_str = "RUNNING  "; break;
                case PROCESS_STATE_BLOCKED:    state_str = "BLOCKED  "; break;
                case PROCESS_STATE_WAITING:    state_str = "WAITING  "; break;
                case PROCESS_STATE_ZOMBIE:     state_str = "ZOMBIE   "; break;
                case PROCESS_STATE_TERMINATED: state_str = "TERM     "; break;
            }
            terminal_writestring(state_str);
            terminal_writestring("  ");

            // Print ticks
            terminal_print_uint(proc->ticks_total);
            terminal_writestring("\n");
        }
    }
}

// Helper functions for fork/exec

// Allocate a new process structure
process_t* allocate_process_struct(void) {
    process_t* proc = alloc_process_common("fork-child", 1);
    if (!proc) {
        return NULL;
    }

    proc->name[0] = '\0';
    return proc;
}

// Free a process structure
void free_process_struct(process_t* process) {
    if (!process) return;

    // Remove from process table
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i] == process) {
            process_table[i] = NULL;
            break;
        }
    }

    // Close open file descriptors
    cleanup_process_fds(process);

    // Free resources
    if (process->kernel_stack) {
        kfree(process->kernel_stack);
    }

    if (process->kind == PROCESS_KIND_USER && process->page_directory) {
        vmm_destroy_address_space(process->page_directory);
    }

    kfree(process);
}

// Find a zombie child process
process_t* find_zombie_child(uint32_t parent_pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = process_table[i];
        if (proc && proc->parent_pid == parent_pid && 
            proc->state == PROCESS_STATE_ZOMBIE) {
            return proc;
        }
    }
    return NULL;
}

void process_enter_user_mode(void) {
    process_t* current = process_get_current();
    if (!current || current->kind != PROCESS_KIND_USER || current->user_entry == 0) {
        panic("process_enter_user_mode: Invalid user process state");
    }

    switch_to_user_mode((void*)current->user_entry, (void*)current->stack_top);
    panic("process_enter_user_mode: Returned from user mode");
}
