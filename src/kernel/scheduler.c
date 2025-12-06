#include "../include/process.h"
#include "../include/timer.h"
#include "../include/terminal.h"
#include "../include/panic.h"
#include "../include/vmm.h"
#include "../include/signal.h"

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

    // Disable interrupts during scheduling
    asm volatile("cli");

    // Handle pending signals for current process
    if (signal_pending()) {
        signal_handle();
    }

    schedule_count++;

    process_t* current = process_get_current();
    process_t* next = NULL;
    
    // If current process is still runnable, put it back in ready queue
    if (current && current->state == PROCESS_STATE_RUNNING) {
        current->state = PROCESS_STATE_READY;
        ready_queue_push(current);
    }
    
    // Get next process from ready queue
    next = ready_queue_pop();
    
    // If no process is ready, use idle process
    if (!next) {
        next = process_table[0];  // Idle process
    }
    
    // If switching to a different process
    if (current != next) {
        next->state = PROCESS_STATE_RUNNING;
        
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
    
    // Re-enable interrupts
    asm volatile("sti");
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
    terminal_writestring("Scheduler initialized (disabled)\n");
}

// Enable scheduler
void scheduler_enable(void) {
    scheduler_enabled = true;
    terminal_writestring("Scheduler enabled\n");
    
    // Schedule first process
    schedule();
}

// Disable scheduler
void scheduler_disable(void) {
    scheduler_enabled = false;
    terminal_writestring("Scheduler disabled\n");
}

// Get scheduler statistics
void scheduler_stats(void) {
    terminal_writestring("Scheduler statistics:\n");
    terminal_writestring("  Schedule count: ");
    terminal_print_uint(schedule_count);
    terminal_writestring("\n");
}