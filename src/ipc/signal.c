#include "ipc/signal.h"
#include "kernel/process.h"
#include "drivers/terminal.h"
#include "kernel/scheduler.h"

// External process table for lookup
extern process_t* process_table[];

// Find process by PID
process_t* process_find_by_pid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            return process_table[i];
        }
    }
    return NULL;
}

// Initialize signal handling for a process
void signal_init_process(process_t* proc) {
    if (!proc) return;

    proc->pending_signals = 0;
    proc->signal_mask = 0;

    // Set all handlers to SIG_DFL
    for (int i = 0; i < NSIG; i++) {
        proc->signal_handlers[i] = SIG_DFL;
    }
}

// Initialize signal subsystem
void signal_init(void) {
    terminal_writestring("Signal subsystem initialized\n");
}

// Send a signal to a process
void signal_send(int pid, int sig) {
    if (sig < 1 || sig >= NSIG) {
        return;
    }

    process_t* proc = process_find_by_pid(pid);
    if (!proc) {
        terminal_writestring("[SIGNAL] Process not found: PID ");
        terminal_print_int(pid);
        terminal_writestring("\n");
        return;
    }

    // Handle uncatchable signals immediately
    if (sig == SIGKILL) {
        terminal_writestring("[SIGNAL] SIGKILL -> ");
        terminal_writestring(proc->name);
        terminal_writestring("\n");
        proc->state = PROCESS_STATE_TERMINATED;
        proc->exit_status = 128 + sig;
        return;
    }

    if (sig == SIGSTOP) {
        terminal_writestring("[SIGNAL] SIGSTOP -> ");
        terminal_writestring(proc->name);
        terminal_writestring("\n");
        if (proc->state == PROCESS_STATE_RUNNING ||
            proc->state == PROCESS_STATE_READY) {
            proc->state = PROCESS_STATE_BLOCKED;
        }
        return;
    }

    if (sig == SIGCONT) {
        terminal_writestring("[SIGNAL] SIGCONT -> ");
        terminal_writestring(proc->name);
        terminal_writestring("\n");
        if (proc->state == PROCESS_STATE_BLOCKED) {
            proc->state = PROCESS_STATE_READY;
            extern void ready_queue_push(process_t* proc);
            ready_queue_push(proc);
        }
        // Clear any pending SIGSTOP/SIGTSTP
        proc->pending_signals &= ~((1 << SIGSTOP) | (1 << SIGTSTP));
        return;
    }

    // Set pending signal bit
    proc->pending_signals |= (1 << sig);

    // If process is blocked and not masked, wake it up
    if (proc->state == PROCESS_STATE_BLOCKED) {
        if (!(proc->signal_mask & (1 << sig))) {
            proc->state = PROCESS_STATE_READY;
            extern void ready_queue_push(process_t* proc);
            ready_queue_push(proc);
        }
    }
}

// Check if current process has pending signals
int signal_pending(void) {
    process_t* current = process_get_current();
    if (!current) return 0;

    // Check for unmasked pending signals
    uint32_t actionable = current->pending_signals & ~current->signal_mask;
    return actionable != 0;
}

// Handle pending signals for current process
void signal_handle(void) {
    process_t* current = process_get_current();
    if (!current) return;

    // Get actionable signals (pending and not masked)
    uint32_t actionable = current->pending_signals & ~current->signal_mask;
    if (actionable == 0) return;

    // Handle each pending signal
    for (int sig = 1; sig < NSIG; sig++) {
        if (!(actionable & (1 << sig))) continue;

        // Clear the pending bit
        current->pending_signals &= ~(1 << sig);

        // Get handler
        sighandler_t handler = (sighandler_t)current->signal_handlers[sig];

        if (handler == SIG_IGN) {
            // Ignored
            continue;
        }

        if (handler == SIG_DFL) {
            // Default action
            switch (sig) {
                case SIGINT:
                case SIGTERM:
                    terminal_writestring("[SIGNAL] Default action: terminate ");
                    terminal_writestring(current->name);
                    terminal_writestring("\n");
                    current->state = PROCESS_STATE_TERMINATED;
                    current->exit_status = 128 + sig;
                    schedule();
                    return;

                case SIGTSTP:
                    terminal_writestring("[SIGNAL] Default action: stop ");
                    terminal_writestring(current->name);
                    terminal_writestring("\n");
                    current->state = PROCESS_STATE_BLOCKED;
                    schedule();
                    return;

                case SIGCONT:
                    // Already handled in signal_send
                    break;

                default:
                    // Most signals terminate by default
                    terminal_writestring("[SIGNAL] Unhandled signal ");
                    terminal_print_int(sig);
                    terminal_writestring(" -> terminate ");
                    terminal_writestring(current->name);
                    terminal_writestring("\n");
                    current->state = PROCESS_STATE_TERMINATED;
                    current->exit_status = 128 + sig;
                    schedule();
                    return;
            }
        } else {
            // Custom handler - call it
            // Note: In a real OS, this would switch to user mode
            // For now, we just call it directly
            terminal_writestring("[SIGNAL] Calling handler for signal ");
            terminal_print_int(sig);
            terminal_writestring("\n");
            handler(sig);
        }
    }
}

// Set signal handler (sys_signal syscall)
sighandler_t sys_signal(int signum, sighandler_t handler) {
    if (signum < 1 || signum >= NSIG) {
        return SIG_DFL;
    }

    // SIGKILL and SIGSTOP cannot be caught
    if (signum == SIGKILL || signum == SIGSTOP) {
        return SIG_DFL;
    }

    process_t* current = process_get_current();
    if (!current) {
        return SIG_DFL;
    }

    sighandler_t old_handler = (sighandler_t)current->signal_handlers[signum];
    current->signal_handlers[signum] = (void*)handler;

    return old_handler;
}
