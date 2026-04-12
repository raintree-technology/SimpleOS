#include "kernel/process.h"
#include "drivers/timer.h"
#include "drivers/terminal.h"
#include "kernel/panic.h"
#include "mm/vmm.h"
#include "ipc/signal.h"
#include "arch/i386/tss.h"

// External assembly function
extern void context_switch(context_t* old_context, context_t* new_context);

// Scheduler state
static bool scheduler_enabled = false;
static uint32_t schedule_count = 0;

// External references
extern process_t* current_process;
extern process_t* process_table[];
process_t* ready_queue_pop(void);
void ready_queue_push(process_t* proc);

// Round-robin scheduler
void schedule(void) {
    if (!scheduler_enabled) {
        return;
    }

    // Save and disable interrupts during scheduling
    uint32_t eflags;
    asm volatile("pushfl; popl %0; cli" : "=r"(eflags));

    schedule_count++;

    process_t* current = process_get_current();
    process_t* next = NULL;

    // If current process is still runnable, put it back in ready queue
    if (current && current->state == PROCESS_STATE_RUNNING) {
        current->state = PROCESS_STATE_READY;
        ready_queue_push(current);
    }

    // Pop next READY process; skip and re-enqueue non-READY entries
    for (;;) {
        next = ready_queue_pop();
        if (!next) {
            break;
        }
        if (next->state == PROCESS_STATE_READY) {
            break;
        }
        // Non-READY process: don't discard it, leave it off the ready queue
        // (it's BLOCKED/WAITING/ZOMBIE/TERMINATED — not runnable)
    }

    // If no process is ready, use idle process
    if (!next) {
        next = process_table[0];  // Idle process
    }

    // If switching to a different process
    if (current != next) {
        next->state = PROCESS_STATE_RUNNING;

        if (next->kernel_stack) {
            tss_set_kernel_stack((uint32_t)next->kernel_stack + next->kernel_stack_size);
        }

        // Update current process pointer
        current_process = next;

        // Switch page tables if different
        if (!current || current->page_directory != next->page_directory) {
            if (next->page_directory) {
                vmm_switch_address_space(next->page_directory);
            }
        }

        // Perform context switch
        if (current) {
            context_switch(&current->context, &next->context);
        } else {
            // Initial switch, no old context to save
            context_switch(NULL, &next->context);
        }
    }

    // Restore interrupt flag to its previous state
    if (eflags & 0x200) {
        asm volatile("sti");
    }

    // Handle pending signals after scheduling, with interrupts in a safe state
    if (signal_pending()) {
        signal_handle();
    }
}

// Timer callback for preemptive scheduling
void scheduler_tick(void) {
    if (!scheduler_enabled) {
        return;
    }
    
    process_t* current = process_get_current();
    if (!current) {
        return;
    }
    
    // Update process statistics
    current->ticks_total++;
    
    // Check if quantum expired
    if (current->ticks_remaining > 0) {
        current->ticks_remaining--;
    }
    
    // If quantum expired, schedule next process
    if (current->ticks_remaining == 0) {
        current->ticks_remaining = DEFAULT_QUANTUM;  // Reset quantum
        schedule();
    }
}

// Initialize scheduler
void scheduler_init(void) {
    scheduler_enabled = false;
    schedule_count = 0;
    terminal_writestring("Scheduler ready (standby)\n");
}

// Enable scheduler
void scheduler_enable(void) {
    scheduler_enabled = true;
    terminal_writestring("Scheduler live\n");
    
    // Schedule first process
    schedule();
}

// Disable scheduler
void scheduler_disable(void) {
    scheduler_enabled = false;
    terminal_writestring("Scheduler paused\n");
}

// Get scheduler statistics
void scheduler_stats(void) {
    terminal_writestring("Scheduler statistics:\n");
    terminal_writestring("  Schedule count: ");
    terminal_print_uint(schedule_count);
    terminal_writestring("\n");
}
