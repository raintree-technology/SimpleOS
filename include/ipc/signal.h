#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>

// Forward declaration
struct process;
typedef struct process process_t;

// Signal numbers
#define SIGINT  2   // Interrupt (Ctrl+C)
#define SIGKILL 9   // Kill (cannot be caught)
#define SIGTERM 15  // Terminate
#define SIGCONT 18  // Continue process
#define SIGSTOP 19  // Stop process (cannot be caught)
#define SIGTSTP 20  // Terminal stop (Ctrl+Z)

// Number of signals
#define NSIG 32

// Signal handler type
typedef void (*sighandler_t)(int);

// Special signal handlers
#define SIG_DFL ((sighandler_t)0)  // Default handler
#define SIG_IGN ((sighandler_t)1)  // Ignore signal

// Signal functions
int sys_kill(int pid, int sig);
sighandler_t sys_signal(int signum, sighandler_t handler);

// Internal signal handling
void signal_init(void);
void signal_init_process(process_t* proc);  // Initialize signal state for a new process
void signal_send(int pid, int sig);
void signal_handle(void);
int signal_pending(void);  // Check if current process has pending signals

// Process lookup helper
process_t* process_find_by_pid(uint32_t pid);

#endif