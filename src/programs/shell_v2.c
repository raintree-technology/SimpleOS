#include "../../include/string.h"
#include <stdint.h>

// Forward declarations
static int sys_write(int fd, const char* buf, int len);
static int str_len(const char* s);
static void int_to_str(int num, char* buf);
static int sys_pipe(int pipefd[2]);
static int sys_dup2(int oldfd, int newfd);

#define MAX_CMD_LEN 256
#define MAX_ARGS 16
#define HISTORY_SIZE 10
#define MAX_PATH 128

// Command history
static char history[HISTORY_SIZE][MAX_CMD_LEN];
static int history_count = 0;
static int history_pos = 0;

// Current command line buffer
static char cmd_buffer[MAX_CMD_LEN];
static int cmd_pos = 0;
static int cmd_len = 0;

// Forward declaration for fs_node_t
typedef struct {
    char name[32];
    uint32_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t first_block;
    uint32_t created_time;
    uint32_t modified_time;
    uint32_t inode_num;
} fs_node_t;

// Job management structures
#define MAX_JOBS 16
#define JOB_STATE_RUNNING 0
#define JOB_STATE_STOPPED 1
#define JOB_STATE_DONE 2

typedef struct {
    int job_id;
    int pid;
    char command[256];
    int state;
    int background;
} job_t;

// Job management (userspace implementation)
static job_t jobs[MAX_JOBS];
static int next_job_id = 1;
static int jobs_initialized = 0;

static void jobs_init(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].job_id = 0;
        jobs[i].pid = 0;
        jobs[i].state = JOB_STATE_DONE;
    }
    jobs_initialized = 1;
}

static int jobs_add(int pid, const char* command, int background) {
    if (!jobs_initialized) jobs_init();
    
    // Find free slot
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].job_id = next_job_id++;
            jobs[i].pid = pid;
            strncpy(jobs[i].command, command, 255);
            jobs[i].command[255] = '\0';
            jobs[i].state = JOB_STATE_RUNNING;
            jobs[i].background = background;
            
            if (background) {
                sys_write(1, "[", 1);
                char buf[16];
                int_to_str(jobs[i].job_id, buf);
                sys_write(1, buf, str_len(buf));
                sys_write(1, "] ", 2);
                int_to_str(pid, buf);
                sys_write(1, buf, str_len(buf));
                sys_write(1, "\n", 1);
            }
            
            return jobs[i].job_id;
        }
    }
    return -1;
}

static void jobs_list(void) {
    if (!jobs_initialized) return;
    
    sys_write(1, "Job ID  PID     State    Command\n", 33);
    sys_write(1, "------  -----   -------  --------\n", 34);
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid > 0) {
            char buf[256];
            char num[16];
            
            // Job ID
            int_to_str(jobs[i].job_id, num);
            sys_write(1, "[", 1);
            sys_write(1, num, str_len(num));
            sys_write(1, "]", 1);
            sys_write(1, "     ", 5);
            
            // PID
            int_to_str(jobs[i].pid, num);
            sys_write(1, num, str_len(num));
            sys_write(1, "     ", 5);
            
            // State
            const char* state = "Running";
            if (jobs[i].state == JOB_STATE_STOPPED) state = "Stopped";
            else if (jobs[i].state == JOB_STATE_DONE) state = "Done";
            sys_write(1, state, str_len(state));
            sys_write(1, "  ", 2);
            
            // Command
            sys_write(1, jobs[i].command, str_len(jobs[i].command));
            sys_write(1, "\n", 1);
        }
    }
}

static job_t* jobs_get_by_id(int job_id) {
    if (!jobs_initialized) return 0;
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].job_id == job_id) {
            return &jobs[i];
        }
    }
    return 0;
}

static void jobs_remove(int pid) {
    if (!jobs_initialized) return;
    
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            jobs[i].pid = 0;
            jobs[i].state = JOB_STATE_DONE;
            break;
        }
    }
}

// System call wrappers
static int sys_write(int fd, const char* buf, int len) {
    int ret;
    asm volatile(
        "mov $2, %%rax\n"      // SYS_WRITE
        "mov %1, %%rdi\n"      // fd
        "mov %2, %%rsi\n"      // buffer
        "mov %3, %%rdx\n"      // length
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(fd), "r"(buf), "r"(len)
        : "rax", "rdi", "rsi", "rdx"
    );
    return ret;
}

static int sys_read(int fd, char* buf, int len) {
    int ret;
    asm volatile(
        "mov $3, %%rax\n"      // SYS_READ
        "mov %1, %%rdi\n"      // fd
        "mov %2, %%rsi\n"      // buffer
        "mov %3, %%rdx\n"      // length
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(fd), "r"(buf), "r"(len)
        : "rax", "rdi", "rsi", "rdx"
    );
    return ret;
}

static int sys_fork(void) {
    int ret;
    asm volatile(
        "mov $7, %%rax\n"      // SYS_FORK
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        :
        : "rax"
    );
    return ret;
}

static int sys_wait(int* status) {
    int ret;
    asm volatile(
        "mov $8, %%rax\n"      // SYS_WAIT
        "mov %1, %%rdi\n"      // status pointer
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(status)
        : "rax", "rdi"
    );
    return ret;
}

static void sys_exit(int code) {
    asm volatile(
        "mov $1, %%rax\n"      // SYS_EXIT
        "mov %0, %%rdi\n"      // exit code
        "int $0x80"
        :
        : "r"(code)
        : "rax", "rdi"
    );
}

static int sys_execve(const char* path, char* const argv[], char* const envp[]) {
    int ret;
    asm volatile(
        "mov $9, %%rax\n"      // SYS_EXECVE
        "mov %1, %%rdi\n"      // path
        "mov %2, %%rsi\n"      // argv
        "mov %3, %%rdx\n"      // envp
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(path), "r"(argv), "r"(envp)
        : "rax", "rdi", "rsi", "rdx"
    );
    return ret;
}

static void sys_sleep(int ms) {
    asm volatile(
        "mov $5, %%rax\n"      // SYS_SLEEP
        "mov %0, %%rdi\n"      // milliseconds
        "int $0x80"
        :
        : "r"(ms)
        : "rax", "rdi"
    );
}

static int sys_getpid(void) {
    int ret;
    asm volatile(
        "mov $4, %%rax\n"      // SYS_GETPID
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        :
        : "rax"
    );
    return ret;
}

// String utilities
static int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int str_cmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static void str_cpy(char* dst, const char* src) {
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

static char* str_chr(const char* s, char c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return 0;
}

// Number to string conversion
static void int_to_str(int num, char* buf) {
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    int i = 0;
    int temp = num;
    
    if (num < 0) {
        buf[i++] = '-';
        num = -num;
    }
    
    int digits = 0;
    temp = num;
    while (temp > 0) {
        digits++;
        temp /= 10;
    }
    
    buf[i + digits] = '\0';
    for (int j = digits - 1; j >= 0; j--) {
        buf[i + j] = '0' + (num % 10);
        num /= 10;
    }
}

// Clear current line and redraw
static void redraw_line(const char* prompt) {
    // Move cursor to beginning of line
    sys_write(1, "\r", 1);
    // Clear line
    sys_write(1, "\033[K", 3);
    // Redraw prompt and command
    sys_write(1, prompt, str_len(prompt));
    sys_write(1, cmd_buffer, cmd_len);
    // Position cursor
    if (cmd_pos < cmd_len) {
        char esc[16];
        int back = cmd_len - cmd_pos;
        esc[0] = '\033';
        esc[1] = '[';
        int_to_str(back, esc + 2);
        int len = str_len(esc);
        esc[len] = 'D';
        esc[len + 1] = '\0';
        sys_write(1, esc, str_len(esc));
    }
}

// Add command to history
static void add_to_history(const char* cmd) {
    if (str_len(cmd) == 0) return;
    
    // Don't add duplicates
    if (history_count > 0 && str_cmp(history[(history_count - 1) % HISTORY_SIZE], cmd) == 0) {
        return;
    }
    
    str_cpy(history[history_count % HISTORY_SIZE], cmd);
    history_count++;
    history_pos = history_count;
}

// Parse command for pipes and redirections
typedef struct {
    char* commands[MAX_ARGS];
    int num_commands;
    char* input_file;
    char* output_file;
    int append_output;
    int background;
} ParsedCommand;

static void parse_command_line(char* cmd, ParsedCommand* parsed) {
    parsed->num_commands = 0;
    parsed->input_file = 0;
    parsed->output_file = 0;
    parsed->append_output = 0;
    parsed->background = 0;
    
    // Check for background process
    int len = str_len(cmd);
    if (len > 0 && cmd[len - 1] == '&') {
        parsed->background = 1;
        cmd[len - 1] = '\0';
        len--;
    }
    
    // Split by pipes
    char* start = cmd;
    parsed->commands[0] = start;
    parsed->num_commands = 1;
    
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '|') {
            cmd[i] = '\0';
            if (parsed->num_commands < MAX_ARGS - 1) {
                parsed->commands[parsed->num_commands++] = &cmd[i + 1];
            }
        } else if (cmd[i] == '>') {
            cmd[i] = '\0';
            if (cmd[i + 1] == '>') {
                parsed->append_output = 1;
                i++;
                cmd[i] = '\0';
            }
            // Skip whitespace
            i++;
            while (cmd[i] == ' ' || cmd[i] == '\t') i++;
            parsed->output_file = &cmd[i];
            break;
        } else if (cmd[i] == '<') {
            cmd[i] = '\0';
            i++;
            while (cmd[i] == ' ' || cmd[i] == '\t') i++;
            parsed->input_file = &cmd[i];
        }
    }
}

// Parse single command into argv array
static int parse_command(char* cmd, char* argv[]) {
    int argc = 0;
    int in_token = 0;
    
    for (int i = 0; cmd[i] && argc < MAX_ARGS - 1; i++) {
        if (cmd[i] == ' ' || cmd[i] == '\t') {
            if (in_token) {
                cmd[i] = '\0';
                in_token = 0;
            }
        } else {
            if (!in_token) {
                argv[argc++] = &cmd[i];
                in_token = 1;
            }
        }
    }
    argv[argc] = 0;
    return argc;
}

// Execute command with potential pipe
static int execute_pipe(char* cmd1, char* cmd2);

// Execute a single command (with optional background flag)
static int execute_command_bg(char* cmd, int background) {
    char* argv[MAX_ARGS];
    int argc = parse_command(cmd, argv);
    if (argc == 0) return 0;
    
    // Built-in commands
    if (str_cmp(argv[0], "help") == 0) {
        sys_write(1, "Commands:\n", 10);
        sys_write(1, "  help     - Show this help\n", 28);
        sys_write(1, "  ps       - List processes\n", 28);
        sys_write(1, "  echo     - Print arguments\n", 29);
        sys_write(1, "  fork     - Test fork\n", 23);
        sys_write(1, "  stress   - Stress test\n", 25);
        sys_write(1, "  clear    - Clear screen\n", 26);
        sys_write(1, "  history  - Show command history\n", 34);
        sys_write(1, "  exit     - Exit shell\n", 24);
        sys_write(1, "\nFeatures:\n", 11);
        sys_write(1, "  - Use UP/DOWN arrows for history\n", 35);
        sys_write(1, "  - Commands can be piped: cmd1 | cmd2\n", 39);
        sys_write(1, "  - Redirections: cmd > file, cmd < file\n", 41);
        sys_write(1, "  - Background: cmd &\n", 22);
        return 0;
    }
    else if (str_cmp(argv[0], "jobs") == 0) {
        jobs_list();
        return 0;
    }
    else if (str_cmp(argv[0], "fg") == 0) {
        if (argc < 2) {
            sys_write(1, "Usage: fg <job_id>\n", 19);
        } else {
            // Simple atoi
            int job_id = 0;
            for (int i = 0; argv[1][i]; i++) {
                if (argv[1][i] >= '0' && argv[1][i] <= '9') {
                    job_id = job_id * 10 + (argv[1][i] - '0');
                }
            }
            
            job_t* job = jobs_get_by_id(job_id);
            if (job) {
                // Wait for this specific process
                int status;
                int pid;
                do {
                    asm volatile(
                        "mov $8, %%rax\n"      // SYS_WAIT
                        "mov %1, %%rdi\n"      // status pointer
                        "int $0x80\n"
                        "mov %%rax, %0"
                        : "=r"(pid) : "r"(&status) : "rax", "rdi"
                    );
                } while (pid != job->pid && pid > 0);
                
                jobs_remove(job->pid);
            } else {
                sys_write(1, "fg: no such job\n", 16);
            }
        }
        return 0;
    }
    else if (str_cmp(argv[0], "ps") == 0) {
        asm volatile(
            "mov $10, %%rax\n"     // SYS_PS
            "int $0x80"
            : : : "rax"
        );
        return 0;
    }
    else if (str_cmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            sys_write(1, argv[i], str_len(argv[i]));
            if (i < argc - 1) sys_write(1, " ", 1);
        }
        sys_write(1, "\n", 1);
        return 0;
    }
    else if (str_cmp(argv[0], "history") == 0) {
        int start = history_count > HISTORY_SIZE ? history_count - HISTORY_SIZE : 0;
        for (int i = start; i < history_count; i++) {
            char buf[16];
            int_to_str(i + 1, buf);
            sys_write(1, "  ", 2);
            sys_write(1, buf, str_len(buf));
            sys_write(1, "  ", 2);
            sys_write(1, history[i % HISTORY_SIZE], str_len(history[i % HISTORY_SIZE]));
            sys_write(1, "\n", 1);
        }
        return 0;
    }
    else if (str_cmp(argv[0], "clear") == 0) {
        sys_write(1, "\033[2J\033[H", 7);
        return 0;
    }
    else if (str_cmp(argv[0], "exit") == 0) {
        sys_write(1, "Goodbye!\n", 9);
        sys_exit(0);
    }
    else if (str_cmp(argv[0], "fork") == 0) {
        int pid = sys_fork();
        if (pid == 0) {
            sys_write(1, "Child process running!\n", 23);
            sys_sleep(2000);
            sys_write(1, "Child exiting\n", 14);
            sys_exit(0);
        } else if (pid > 0) {
            char buf[64];
            sys_write(1, "Created child PID ", 18);
            int_to_str(pid, buf);
            sys_write(1, buf, str_len(buf));
            sys_write(1, "\n", 1);
            
            int status;
            sys_wait(&status);
            sys_write(1, "Child finished\n", 15);
        } else {
            sys_write(1, "Fork failed!\n", 13);
        }
        return 0;
    }
    else if (str_cmp(argv[0], "stress") == 0) {
        sys_write(1, "Starting stress test...\n", 24);
        
        for (int i = 0; i < 3; i++) {
            int pid = sys_fork();
            if (pid == 0) {
                char buf[64];
                sys_write(1, "Worker ", 7);
                int_to_str(sys_getpid(), buf);
                sys_write(1, buf, str_len(buf));
                sys_write(1, " running\n", 9);
                
                sys_sleep(1000 + i * 500);
                sys_exit(i);
            }
        }
        
        for (int i = 0; i < 3; i++) {
            int status;
            int pid = sys_wait(&status);
            char buf[64];
            sys_write(1, "Child ", 6);
            int_to_str(pid, buf);
            sys_write(1, buf, str_len(buf));
            sys_write(1, " exited\n", 8);
        }
        
        sys_write(1, "Stress test complete!\n", 22);
        return 0;
    }
    
    // Try to execute external program
    int pid = sys_fork();
    if (pid == 0) {
        sys_execve(argv[0], argv, 0);
        sys_write(1, "Command not found: ", 19);
        sys_write(1, argv[0], str_len(argv[0]));
        sys_write(1, "\n", 1);
        sys_exit(1);
    } else if (pid > 0) {
        if (background) {
            // Add to job list
            jobs_add(pid, cmd, 1);
            // Don't wait
        } else {
            // Wait for foreground process
            int status;
            sys_wait(&status);
            return status;
        }
    }
    
    return -1;
}

// Execute command wrapper (no background)
static int execute_command(char* cmd) {
    return execute_command_bg(cmd, 0);
}

// Execute two commands connected by a pipe
static int execute_pipe(char* cmd1, char* cmd2) {
    int pipefd[2];
    
    // Create pipe
    if (sys_pipe(pipefd) < 0) {
        sys_write(1, "Failed to create pipe\n", 22);
        return -1;
    }
    
    // Fork first child for cmd1
    int pid1 = sys_fork();
    if (pid1 == 0) {
        // Child 1: redirect stdout to pipe write end
        // Close unused read end
        asm volatile(
            "mov $12, %%rax\n"     // SYS_CLOSE
            "mov %0, %%rdi\n"      // pipefd[0]
            "int $0x80"
            : : "r"(pipefd[0]) : "rax", "rdi"
        );
        
        // Redirect stdout to pipe
        // In a real implementation, we'd dup2() here
        // For now, we'll modify how we execute
        
        // Parse and execute cmd1
        char* argv1[MAX_ARGS];
        int argc1 = parse_command(cmd1, argv1);
        if (argc1 > 0) {
            // Redirect stdout to pipe write end using dup2
            sys_dup2(pipefd[1], 1);
            
            // Close pipe write end (we're using it through stdout now)
            asm volatile(
                "mov $12, %%rax\n"     // SYS_CLOSE
                "mov %0, %%rdi\n"      // pipefd[1]
                "int $0x80"
                : : "r"(pipefd[1]) : "rax", "rdi"
            );
            
            // Execute command (it will write to stdout which is now the pipe)
            execute_command(cmd1);
        }
        
        // Close write end
        asm volatile(
            "mov $12, %%rax\n"     // SYS_CLOSE
            "mov %0, %%rdi\n"      // pipefd[1]
            "int $0x80"
            : : "r"(pipefd[1]) : "rax", "rdi"
        );
        
        sys_exit(0);
    }
    
    // Fork second child for cmd2
    int pid2 = sys_fork();
    if (pid2 == 0) {
        // Child 2: redirect stdin to pipe read end
        // Close unused write end
        asm volatile(
            "mov $12, %%rax\n"     // SYS_CLOSE
            "mov %0, %%rdi\n"      // pipefd[1]
            "int $0x80"
            : : "r"(pipefd[1]) : "rax", "rdi"
        );
        
        // Redirect stdin from pipe using dup2
        sys_dup2(pipefd[0], 0);
        
        // Close pipe read end (we're using it through stdin now)
        asm volatile(
            "mov $12, %%rax\n"     // SYS_CLOSE
            "mov %0, %%rdi\n"      // pipefd[0]
            "int $0x80"
            : : "r"(pipefd[0]) : "rax", "rdi"
        );
        
        // Parse and execute cmd2
        execute_command(cmd2);
        
        // Exit child process
        asm volatile(
            "mov $1, %%rax\n"     // SYS_EXIT
            "mov $0, %%rdi\n"
            "int $0x80"
            : : "r"(pipefd[0]) : "rax", "rdi"
        );
        
        sys_exit(0);
    }
    
    // Parent: close both ends of pipe
    asm volatile(
        "mov $12, %%rax\n"     // SYS_CLOSE
        "mov %0, %%rdi\n"      // pipefd[0]
        "int $0x80"
        : : "r"(pipefd[0]) : "rax", "rdi"
    );
    
    asm volatile(
        "mov $12, %%rax\n"     // SYS_CLOSE
        "mov %0, %%rdi\n"      // pipefd[1]
        "int $0x80"
        : : "r"(pipefd[1]) : "rax", "rdi"
    );
    
    // Wait for both children
    int status;
    sys_wait(&status);
    sys_wait(&status);
    
    return 0;
}

// sys_pipe wrapper
static int sys_pipe(int pipefd[2]) {
    int ret;
    asm volatile(
        "mov $17, %%rax\n"     // SYS_PIPE
        "mov %1, %%rdi\n"      // pipefd array
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(pipefd)
        : "rax", "rdi"
    );
    return ret;
}

static int sys_dup2(int oldfd, int newfd) {
    int ret;
    asm volatile(
        "mov $18, %%rax\n"     // SYS_DUP2
        "mov %1, %%rdi\n"      // oldfd
        "mov %2, %%rsi\n"      // newfd
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(oldfd), "r"(newfd)
        : "rax", "rdi", "rsi"
    );
    return ret;
}

// sys_open wrapper
static int sys_open(const char* path, int flags, int mode) {
    int ret;
    asm volatile(
        "mov $11, %%rax\n"     // SYS_OPEN
        "mov %1, %%rdi\n"      // path
        "mov %2, %%rsi\n"      // flags
        "mov %3, %%rdx\n"      // mode
        "int $0x80\n"
        "mov %%rax, %0"
        : "=r"(ret)
        : "r"(path), "r"(flags), "r"(mode)
        : "rax", "rdi", "rsi", "rdx"
    );
    return ret;
}

// Read a line with editing support
static int read_line(const char* prompt) {
    cmd_len = 0;
    cmd_pos = 0;
    cmd_buffer[0] = '\0';
    
    sys_write(1, prompt, str_len(prompt));
    
    while (1) {
        char c;
        if (sys_read(0, &c, 1) != 1) continue;
        
        if (c == '\n') {
            sys_write(1, "\n", 1);
            cmd_buffer[cmd_len] = '\0';
            return cmd_len;
        }
        else if (c == '\b') {
            if (cmd_pos > 0) {
                // Move characters back
                for (int i = cmd_pos - 1; i < cmd_len - 1; i++) {
                    cmd_buffer[i] = cmd_buffer[i + 1];
                }
                cmd_pos--;
                cmd_len--;
                redraw_line(prompt);
            }
        }
        else if (c == '\033') {
            // Escape sequence (arrow keys)
            char seq[2];
            if (sys_read(0, seq, 2) == 2 && seq[0] == '[') {
                if (seq[1] == 'A') {  // UP arrow
                    if (history_count > 0 && history_pos > 0) {
                        history_pos--;
                        if (history_pos < history_count) {
                            str_cpy(cmd_buffer, history[history_pos % HISTORY_SIZE]);
                            cmd_len = str_len(cmd_buffer);
                            cmd_pos = cmd_len;
                            redraw_line(prompt);
                        }
                    }
                }
                else if (seq[1] == 'B') {  // DOWN arrow
                    if (history_pos < history_count - 1) {
                        history_pos++;
                        str_cpy(cmd_buffer, history[history_pos % HISTORY_SIZE]);
                        cmd_len = str_len(cmd_buffer);
                        cmd_pos = cmd_len;
                        redraw_line(prompt);
                    } else if (history_pos < history_count) {
                        history_pos = history_count;
                        cmd_buffer[0] = '\0';
                        cmd_len = 0;
                        cmd_pos = 0;
                        redraw_line(prompt);
                    }
                }
                else if (seq[1] == 'C') {  // RIGHT arrow
                    if (cmd_pos < cmd_len) {
                        cmd_pos++;
                        sys_write(1, "\033[C", 3);
                    }
                }
                else if (seq[1] == 'D') {  // LEFT arrow
                    if (cmd_pos > 0) {
                        cmd_pos--;
                        sys_write(1, "\033[D", 3);
                    }
                }
            }
        }
        else if (c == '\t') {
            // Tab completion
            // Find the current word being typed
            int word_start = cmd_pos;
            while (word_start > 0 && cmd_buffer[word_start - 1] != ' ') {
                word_start--;
            }
            
            // Extract partial command
            char partial[MAX_CMD_LEN];
            int partial_len = cmd_pos - word_start;
            for (int i = 0; i < partial_len; i++) {
                partial[i] = cmd_buffer[word_start + i];
            }
            partial[partial_len] = '\0';
            
            // List of commands to match
            const char* commands[] = {
                "help", "ps", "echo", "fork", "stress", "ls", "cat", 
                "kill", "wc", "grep", "clear", "exit", "history", "jobs", "fg", NULL
            };
            
            // Find matches
            const char* match = NULL;
            int match_count = 0;
            
            for (int i = 0; commands[i]; i++) {
                // Check if command starts with partial
                int matches = 1;
                for (int j = 0; j < partial_len; j++) {
                    if (commands[i][j] != partial[j]) {
                        matches = 0;
                        break;
                    }
                }
                
                if (matches) {
                    match = commands[i];
                    match_count++;
                }
            }
            
            if (match_count == 1 && match) {
                // Complete the command
                int match_len = str_len(match);
                int to_add = match_len - partial_len;
                
                if (cmd_len + to_add < MAX_CMD_LEN - 1) {
                    // Insert completion
                    for (int i = cmd_len; i >= cmd_pos; i--) {
                        cmd_buffer[i + to_add] = cmd_buffer[i];
                    }
                    
                    for (int i = 0; i < to_add; i++) {
                        cmd_buffer[cmd_pos + i] = match[partial_len + i];
                    }
                    
                    cmd_pos += to_add;
                    cmd_len += to_add;
                    
                    // Redraw line
                    redraw_line(prompt);
                }
            } else if (match_count > 1) {
                // Show all matches
                sys_write(1, "\n", 1);
                for (int i = 0; commands[i]; i++) {
                    int matches = 1;
                    for (int j = 0; j < partial_len; j++) {
                        if (commands[i][j] != partial[j]) {
                            matches = 0;
                            break;
                        }
                    }
                    
                    if (matches) {
                        sys_write(1, commands[i], str_len(commands[i]));
                        sys_write(1, "  ", 2);
                    }
                }
                sys_write(1, "\n", 1);
                redraw_line(prompt);
            }
        }
        else if (c >= 32 && c < 127 && cmd_len < MAX_CMD_LEN - 1) {
            // Insert character at cursor position
            for (int i = cmd_len; i > cmd_pos; i--) {
                cmd_buffer[i] = cmd_buffer[i - 1];
            }
            cmd_buffer[cmd_pos] = c;
            cmd_pos++;
            cmd_len++;
            cmd_buffer[cmd_len] = '\0';
            
            // Redraw from cursor position
            sys_write(1, &cmd_buffer[cmd_pos - 1], cmd_len - cmd_pos + 1);
            if (cmd_pos < cmd_len) {
                // Move cursor back
                char esc[16];
                int back = cmd_len - cmd_pos;
                esc[0] = '\033';
                esc[1] = '[';
                int_to_str(back, esc + 2);
                int len = str_len(esc);
                esc[len] = 'D';
                esc[len + 1] = '\0';
                sys_write(1, esc, str_len(esc));
            }
        }
    }
}

void shell_v2_main(void) {
    sys_write(1, "\n=== SimpleOS Shell v2 ===\n", 27);
    sys_write(1, "Enhanced with history and line editing\n", 39);
    sys_write(1, "Type 'help' for commands\n\n", 26);
    
    while (1) {
        history_pos = history_count;
        
        if (read_line("$ ") <= 0) continue;
        
        // Add to history
        add_to_history(cmd_buffer);
        
        // Parse for pipes and redirections
        ParsedCommand parsed;
        parse_command_line(cmd_buffer, &parsed);
        
        // Initialize jobs if needed
        if (!jobs_initialized) {
            jobs_init();
        }
        
        // Handle pipes
        if (parsed.num_commands == 2) {
            execute_pipe(parsed.commands[0], parsed.commands[1]);
        } else if (parsed.num_commands == 1) {
            // Handle redirections
            int saved_stdin = -1;
            int saved_stdout = -1;
            
            if (parsed.input_file) {
                // Open input file and redirect stdin
                int fd = sys_open(parsed.input_file, 0, 0);
                if (fd < 0) {
                    sys_write(1, "Failed to open input file: ", 27);
                    sys_write(1, parsed.input_file, str_len(parsed.input_file));
                    sys_write(1, "\n", 1);
                    continue;
                }
                saved_stdin = fd;
                sys_dup2(fd, 0);  // Redirect stdin from file
            }
            
            if (parsed.output_file) {
                // Open/create output file and redirect stdout
                int fd = sys_open(parsed.output_file, 1, 0);  // Create if needed
                if (fd < 0) {
                    sys_write(1, "Failed to open output file: ", 28);
                    sys_write(1, parsed.output_file, str_len(parsed.output_file));
                    sys_write(1, "\n", 1);
                    if (saved_stdin >= 0) {
                        // Close the file we opened
                        asm volatile(
                            "mov $12, %%rax\n"     // SYS_CLOSE
                            "mov %0, %%rdi\n"      // saved_stdin
                            "int $0x80"
                            : : "r"(saved_stdin) : "rax", "rdi"
                        );
                    }
                    continue;
                }
                saved_stdout = fd;
                sys_dup2(fd, 1);  // Redirect stdout to file
                
                if (parsed.append_output) {
                    // TODO: Seek to end for append mode
                }
            }
            
            // Execute command with redirections
            execute_command_bg(parsed.commands[0], parsed.background);
            
            // Close files we opened for redirection
            if (saved_stdin >= 0) {
                asm volatile(
                    "mov $12, %%rax\n"     // SYS_CLOSE
                    "mov %0, %%rdi\n"      // saved_stdin
                    "int $0x80"
                    : : "r"(saved_stdin) : "rax", "rdi"
                );
            }
            if (saved_stdout >= 0) {
                asm volatile(
                    "mov $12, %%rax\n"     // SYS_CLOSE
                    "mov %0, %%rdi\n"      // saved_stdout
                    "int $0x80"
                    : : "r"(saved_stdout) : "rax", "rdi"
                );
            }
        } else {
            sys_write(1, "Complex pipes/redirections not yet supported\n", 45);
        }
    }
}