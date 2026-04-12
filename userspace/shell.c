// SimpleOS Shell - standalone userspace ELF binary
// Runs as a user-mode process with fork/exec for external commands.

#include "ulib.h"

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

// Job management
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

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].job_id = next_job_id++;
            jobs[i].pid = pid;
            str_ncpy(jobs[i].command, command, 255);
            jobs[i].command[255] = '\0';
            jobs[i].state = JOB_STATE_RUNNING;
            jobs[i].background = background;

            if (background) {
                WRITE_STR(1, "[");
                char buf[16];
                int_to_str(jobs[i].job_id, buf);
                WRITE_STR(1, buf);
                WRITE_STR(1, "] ");
                int_to_str(pid, buf);
                WRITE_STR(1, buf);
                WRITE_STR(1, "\n");
            }

            return jobs[i].job_id;
        }
    }
    return -1;
}

static void jobs_list(void) {
    if (!jobs_initialized) return;

    WRITE_STR(1, "Job ID  PID     State    Command\n");
    WRITE_STR(1, "------  -----   -------  --------\n");

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid > 0) {
            char num[16];

            int_to_str(jobs[i].job_id, num);
            WRITE_STR(1, "[");
            WRITE_STR(1, num);
            WRITE_STR(1, "]     ");

            int_to_str(jobs[i].pid, num);
            WRITE_STR(1, num);
            WRITE_STR(1, "     ");

            const char* state = "Running";
            if (jobs[i].state == JOB_STATE_STOPPED) state = "Stopped";
            else if (jobs[i].state == JOB_STATE_DONE) state = "Done";
            WRITE_STR(1, state);
            WRITE_STR(1, "  ");

            WRITE_STR(1, jobs[i].command);
            WRITE_STR(1, "\n");
        }
    }
}

static job_t* jobs_get_by_id(int job_id) {
    if (!jobs_initialized) return 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].job_id == job_id) return &jobs[i];
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



// Clear current line and redraw
static void redraw_line(const char* prompt) {
    WRITE_STR(1, "\r");
    WRITE_STR(1, "\033[K");
    WRITE_STR(1, prompt);
    sys_write(1, cmd_buffer, cmd_len);
    if (cmd_pos < cmd_len) {
        char esc[16];
        int back = cmd_len - cmd_pos;
        esc[0] = '\033';
        esc[1] = '[';
        int_to_str(back, esc + 2);
        int len = str_len(esc);
        esc[len] = 'D';
        esc[len + 1] = '\0';
        WRITE_STR(1, esc);
    }
}

static void add_to_history(const char* cmd) {
    if (str_len(cmd) == 0) return;
    if (history_count > 0 && str_cmp(history[(history_count - 1) % HISTORY_SIZE], cmd) == 0)
        return;
    str_cpy(history[history_count % HISTORY_SIZE], cmd);
    history_count++;
    history_pos = history_count;
}

// Parsed command line
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

    int len = str_len(cmd);
    if (len > 0 && cmd[len - 1] == '&') {
        parsed->background = 1;
        cmd[len - 1] = '\0';
        len--;
    }

    parsed->commands[0] = cmd;
    parsed->num_commands = 1;

    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '|') {
            cmd[i] = '\0';
            if (parsed->num_commands < MAX_ARGS - 1)
                parsed->commands[parsed->num_commands++] = &cmd[i + 1];
        } else if (cmd[i] == '>') {
            cmd[i] = '\0';
            if (cmd[i + 1] == '>') {
                parsed->append_output = 1;
                i++;
                cmd[i] = '\0';
            }
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

static int parse_command(char* cmd, char* argv[]) {
    int argc = 0;
    int in_token = 0;

    for (int i = 0; cmd[i] && argc < MAX_ARGS - 1; i++) {
        if (cmd[i] == ' ' || cmd[i] == '\t') {
            if (in_token) { cmd[i] = '\0'; in_token = 0; }
        } else {
            if (!in_token) { argv[argc++] = &cmd[i]; in_token = 1; }
        }
    }
    argv[argc] = 0;
    return argc;
}

static int execute_pipe(char* cmd1, char* cmd2);

static int execute_command_bg(char* cmd, int background) {
    char* argv[MAX_ARGS];
    int argc = parse_command(cmd, argv);
    if (argc == 0) return 0;

    // Built-in commands
    if (str_cmp(argv[0], "help") == 0) {
        WRITE_STR(1, "Commands:\n");
        WRITE_STR(1, "  help     - Show this help\n");
        WRITE_STR(1, "  ps       - List processes\n");
        WRITE_STR(1, "  echo     - Print arguments\n");
        WRITE_STR(1, "  ls       - List files\n");
        WRITE_STR(1, "  cat      - Show file contents\n");
        WRITE_STR(1, "  kill     - Kill a process\n");
        WRITE_STR(1, "  grep     - Search for pattern in stdin\n");
        WRITE_STR(1, "  wc       - Count lines/words/chars from stdin\n");
        WRITE_STR(1, "  fork     - Test fork\n");
        WRITE_STR(1, "  stress   - Stress test\n");
        WRITE_STR(1, "  clear    - Clear screen\n");
        WRITE_STR(1, "  history  - Show command history\n");
        WRITE_STR(1, "  jobs     - List background jobs\n");
        WRITE_STR(1, "  fg       - Bring job to foreground\n");
        WRITE_STR(1, "  exit     - Exit shell\n");
        WRITE_STR(1, "\nFeatures:\n");
        WRITE_STR(1, "  - UP/DOWN arrows for history\n");
        WRITE_STR(1, "  - Tab completion for commands\n");
        WRITE_STR(1, "  - Pipes: cmd1 | cmd2\n");
        WRITE_STR(1, "  - Redirections: cmd > file, cmd < file\n");
        WRITE_STR(1, "  - Background: cmd &\n");
        return 0;
    }
    else if (str_cmp(argv[0], "jobs") == 0) {
        jobs_list();
        return 0;
    }
    else if (str_cmp(argv[0], "fg") == 0) {
        if (argc < 2) {
            WRITE_STR(1, "Usage: fg <job_id>\n");
        } else {
            int job_id = 0;
            for (int i = 0; argv[1][i]; i++) {
                if (argv[1][i] >= '0' && argv[1][i] <= '9')
                    job_id = job_id * 10 + (argv[1][i] - '0');
            }
            job_t* job = jobs_get_by_id(job_id);
            if (job) {
                int status, pid;
                do { pid = sys_wait(&status); } while (pid != job->pid && pid > 0);
                jobs_remove(job->pid);
            } else {
                WRITE_STR(1, "fg: no such job\n");
            }
        }
        return 0;
    }
    else if (str_cmp(argv[0], "ps") == 0) {
        sys_ps();
        return 0;
    }
    else if (str_cmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            WRITE_STR(1, argv[i]);
            if (i < argc - 1) WRITE_STR(1, " ");
        }
        WRITE_STR(1, "\n");
        return 0;
    }
    else if (str_cmp(argv[0], "history") == 0) {
        int start = history_count > HISTORY_SIZE ? history_count - HISTORY_SIZE : 0;
        for (int i = start; i < history_count; i++) {
            char buf[16];
            int_to_str(i + 1, buf);
            WRITE_STR(1, "  ");
            WRITE_STR(1, buf);
            WRITE_STR(1, "  ");
            WRITE_STR(1, history[i % HISTORY_SIZE]);
            WRITE_STR(1, "\n");
        }
        return 0;
    }
    else if (str_cmp(argv[0], "clear") == 0) {
        WRITE_STR(1, "\033[2J\033[H");
        return 0;
    }
    else if (str_cmp(argv[0], "ls") == 0) {
        const char* path = (argc >= 2) ? argv[1] : "/";
        int fd = sys_open(path, 0, 0);
        if (fd >= 0) {
            struct { char name[32]; uint32_t type; } dirent;
            while (1) {
                int ret = sys_readdir(fd, &dirent);
                if (ret <= 0) break;
                WRITE_STR(1, dirent.name);
                WRITE_STR(1, "\n");
            }
            sys_close(fd);
        } else {
            WRITE_STR(1, "Failed to open directory\n");
        }
        return 0;
    }
    else if (str_cmp(argv[0], "cat") == 0) {
        if (argc < 2) {
            WRITE_STR(1, "Usage: cat <filename>\n");
        } else {
            int fd = sys_open(argv[1], 0, 0);
            if (fd >= 0) {
                char buffer[256];
                while (1) {
                    int bytes = sys_read(fd, buffer, 256);
                    if (bytes <= 0) break;
                    sys_write(1, buffer, bytes);
                }
                sys_close(fd);
            } else {
                WRITE_STR(1, "File not found: ");
                WRITE_STR(1, argv[1]);
                WRITE_STR(1, "\n");
            }
        }
        return 0;
    }
    else if (str_cmp(argv[0], "kill") == 0) {
        if (argc < 2) {
            WRITE_STR(1, "Usage: kill <pid>\n");
        } else {
            int pid = 0;
            int valid = 1;
            for (int i = 0; argv[1][i]; i++) {
                if (argv[1][i] >= '0' && argv[1][i] <= '9')
                    pid = pid * 10 + (argv[1][i] - '0');
                else { WRITE_STR(1, "Invalid PID\n"); valid = 0; break; }
            }
            if (valid && pid > 0) {
                sys_kill(pid, 9);
                WRITE_STR(1, "Sent SIGKILL to process ");
                WRITE_STR(1, argv[1]);
                WRITE_STR(1, "\n");
            }
        }
        return 0;
    }
    else if (str_cmp(argv[0], "grep") == 0) {
        if (argc < 2) {
            WRITE_STR(1, "Usage: grep <pattern>\n");
        } else {
            const char* pattern = argv[1];
            char line[256];
            int line_pos = 0;
            while (1) {
                char c;
                int bytes = sys_read(0, &c, 1);
                if (bytes <= 0) {
                    if (line_pos > 0) {
                        line[line_pos] = '\0';
                        if (str_str(line, pattern)) {
                            sys_write(1, line, line_pos);
                            WRITE_STR(1, "\n");
                        }
                    }
                    break;
                }
                if (c == '\n') {
                    line[line_pos] = '\0';
                    if (str_str(line, pattern)) {
                        sys_write(1, line, line_pos);
                        WRITE_STR(1, "\n");
                    }
                    line_pos = 0;
                } else if (line_pos < 255) {
                    line[line_pos++] = c;
                }
            }
        }
        return 0;
    }
    else if (str_cmp(argv[0], "wc") == 0) {
        int lines = 0, words = 0, chars = 0;
        int in_word = 0;
        char buffer[256];
        while (1) {
            int bytes = sys_read(0, buffer, sizeof(buffer));
            if (bytes <= 0) break;
            for (int i = 0; i < bytes; i++) {
                chars++;
                if (buffer[i] == '\n') lines++;
                if (buffer[i] == ' ' || buffer[i] == '\t' || buffer[i] == '\n') {
                    if (in_word) { words++; in_word = 0; }
                } else {
                    in_word = 1;
                }
            }
        }
        if (in_word) words++;
        char buf[16];
        WRITE_STR(1, "  "); int_to_str(lines, buf); WRITE_STR(1, buf);
        WRITE_STR(1, "  "); int_to_str(words, buf); WRITE_STR(1, buf);
        WRITE_STR(1, "  "); int_to_str(chars, buf); WRITE_STR(1, buf);
        WRITE_STR(1, "\n");
        return 0;
    }
    else if (str_cmp(argv[0], "exit") == 0) {
        WRITE_STR(1, "Goodbye!\n");
        sys_exit(0);
    }
    else if (str_cmp(argv[0], "fork") == 0) {
        int pid = sys_fork();
        if (pid == 0) {
            WRITE_STR(1, "Child process running!\n");
            sys_sleep(2000);
            WRITE_STR(1, "Child exiting\n");
            sys_exit(0);
        } else if (pid > 0) {
            char buf[64];
            WRITE_STR(1, "Created child PID ");
            int_to_str(pid, buf);
            WRITE_STR(1, buf);
            WRITE_STR(1, "\n");
            int status;
            sys_wait(&status);
            WRITE_STR(1, "Child finished\n");
        } else {
            WRITE_STR(1, "Fork failed!\n");
        }
        return 0;
    }
    else if (str_cmp(argv[0], "stress") == 0) {
        WRITE_STR(1, "Starting stress test...\n");
        for (int i = 0; i < 3; i++) {
            int pid = sys_fork();
            if (pid == 0) {
                char buf[64];
                WRITE_STR(1, "Worker ");
                int_to_str(sys_getpid(), buf);
                WRITE_STR(1, buf);
                WRITE_STR(1, " running\n");
                sys_sleep(1000 + i * 500);
                sys_exit(i);
            }
        }
        for (int i = 0; i < 3; i++) {
            int status;
            int pid = sys_wait(&status);
            char buf[64];
            WRITE_STR(1, "Child ");
            int_to_str(pid, buf);
            WRITE_STR(1, buf);
            WRITE_STR(1, " exited\n");
        }
        WRITE_STR(1, "Stress test complete!\n");
        return 0;
    }

    // External command: fork + exec with /bin/ prefix search
    int pid = sys_fork();
    if (pid == 0) {
        // Child: try /bin/<cmd> first
        char path[MAX_PATH];
        int j = 0;
        const char* prefix = "/bin/";
        while (prefix[j]) { path[j] = prefix[j]; j++; }
        int k = 0;
        while (argv[0][k] && j < MAX_PATH - 1) { path[j++] = argv[0][k++]; }
        path[j] = '\0';

        sys_execve(path, argv, 0);
        sys_execve(argv[0], argv, 0);
        WRITE_STR(1, "Command not found: ");
        WRITE_STR(1, argv[0]);
        WRITE_STR(1, "\n");
        sys_exit(1);
    } else if (pid > 0) {
        if (background) {
            jobs_add(pid, cmd, 1);
        } else {
            int status;
            sys_wait(&status);
            return status;
        }
    } else {
        WRITE_STR(1, "Command not found: ");
        WRITE_STR(1, argv[0]);
        WRITE_STR(1, "\n");
    }

    return -1;
}

static int execute_command(char* cmd) {
    return execute_command_bg(cmd, 0);
}

static int execute_pipe(char* cmd1, char* cmd2) {
    int pipefd[2];
    if (sys_pipe(pipefd) < 0) {
        WRITE_STR(1, "Failed to create pipe\n");
        return -1;
    }

    int pid1 = sys_fork();
    if (pid1 == 0) {
        sys_close(pipefd[0]);
        sys_dup2(pipefd[1], 1);
        sys_close(pipefd[1]);
        execute_command(cmd1);
        sys_exit(0);
    }

    int pid2 = sys_fork();
    if (pid2 == 0) {
        sys_close(pipefd[1]);
        sys_dup2(pipefd[0], 0);
        sys_close(pipefd[0]);
        execute_command(cmd2);
        sys_exit(0);
    }

    sys_close(pipefd[0]);
    sys_close(pipefd[1]);

    int status;
    sys_wait(&status);
    sys_wait(&status);
    return 0;
}

// Read a line with editing support
static int read_line(const char* prompt) {
    cmd_len = 0;
    cmd_pos = 0;
    cmd_buffer[0] = '\0';

    WRITE_STR(1, prompt);

    while (1) {
        char c;
        if (sys_read(0, &c, 1) != 1) continue;

        if (c == '\n') {
            WRITE_STR(1, "\n");
            cmd_buffer[cmd_len] = '\0';
            return cmd_len;
        }
        else if (c == '\b') {
            if (cmd_pos > 0) {
                for (int i = cmd_pos - 1; i < cmd_len - 1; i++)
                    cmd_buffer[i] = cmd_buffer[i + 1];
                cmd_pos--;
                cmd_len--;
                redraw_line(prompt);
            }
        }
        else if (c == '\033') {
            char seq[2];
            if (sys_read(0, seq, 2) == 2 && seq[0] == '[') {
                if (seq[1] == 'A') {
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
                else if (seq[1] == 'B') {
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
                else if (seq[1] == 'C') {
                    if (cmd_pos < cmd_len) { cmd_pos++; WRITE_STR(1, "\033[C"); }
                }
                else if (seq[1] == 'D') {
                    if (cmd_pos > 0) { cmd_pos--; WRITE_STR(1, "\033[D"); }
                }
            }
        }
        else if (c == '\t') {
            int word_start = cmd_pos;
            while (word_start > 0 && cmd_buffer[word_start - 1] != ' ')
                word_start--;

            char partial[MAX_CMD_LEN];
            int partial_len = cmd_pos - word_start;
            for (int i = 0; i < partial_len; i++)
                partial[i] = cmd_buffer[word_start + i];
            partial[partial_len] = '\0';

            const char* commands[] = {
                "help", "ps", "echo", "fork", "stress", "ls", "cat",
                "kill", "wc", "grep", "clear", "exit", "history",
                "jobs", "fg", "hello", NULL
            };

            const char* match = NULL;
            int match_count = 0;
            for (int i = 0; commands[i]; i++) {
                int matches = 1;
                for (int j = 0; j < partial_len; j++) {
                    if (commands[i][j] != partial[j]) { matches = 0; break; }
                }
                if (matches) { match = commands[i]; match_count++; }
            }

            if (match_count == 1 && match) {
                int match_len = str_len(match);
                int to_add = match_len - partial_len;
                if (cmd_len + to_add < MAX_CMD_LEN - 1) {
                    for (int i = cmd_len; i >= cmd_pos; i--)
                        cmd_buffer[i + to_add] = cmd_buffer[i];
                    for (int i = 0; i < to_add; i++)
                        cmd_buffer[cmd_pos + i] = match[partial_len + i];
                    cmd_pos += to_add;
                    cmd_len += to_add;
                    redraw_line(prompt);
                }
            } else if (match_count > 1) {
                WRITE_STR(1, "\n");
                for (int i = 0; commands[i]; i++) {
                    int matches = 1;
                    for (int j = 0; j < partial_len; j++) {
                        if (commands[i][j] != partial[j]) { matches = 0; break; }
                    }
                    if (matches) { WRITE_STR(1, commands[i]); WRITE_STR(1, "  "); }
                }
                WRITE_STR(1, "\n");
                redraw_line(prompt);
            }
        }
        else if (c >= 32 && c < 127 && cmd_len < MAX_CMD_LEN - 1) {
            for (int i = cmd_len; i > cmd_pos; i--)
                cmd_buffer[i] = cmd_buffer[i - 1];
            cmd_buffer[cmd_pos] = c;
            cmd_pos++;
            cmd_len++;
            cmd_buffer[cmd_len] = '\0';

            sys_write(1, &cmd_buffer[cmd_pos - 1], cmd_len - cmd_pos + 1);
            if (cmd_pos < cmd_len) {
                char esc[16];
                int back = cmd_len - cmd_pos;
                esc[0] = '\033';
                esc[1] = '[';
                int_to_str(back, esc + 2);
                int len = str_len(esc);
                esc[len] = 'D';
                esc[len + 1] = '\0';
                WRITE_STR(1, esc);
            }
        }
    }
}

void shell_main(void) {
    WRITE_STR(1, "\n=== SimpleOS Shell ===\n");
    WRITE_STR(1, "History, line editing, tab completion\n");
    WRITE_STR(1, "Type 'help' for commands\n\n");

    while (1) {
        history_pos = history_count;

        if (read_line("$ ") <= 0) continue;

        add_to_history(cmd_buffer);

        ParsedCommand parsed;
        parse_command_line(cmd_buffer, &parsed);

        if (!jobs_initialized) jobs_init();

        if (parsed.num_commands == 2) {
            execute_pipe(parsed.commands[0], parsed.commands[1]);
        } else if (parsed.num_commands == 1) {
            int saved_stdin = -1;
            int saved_stdout = -1;

            if (parsed.input_file) {
                int fd = sys_open(parsed.input_file, 0, 0);
                if (fd < 0) {
                    WRITE_STR(1, "Failed to open input file: ");
                    WRITE_STR(1, parsed.input_file);
                    WRITE_STR(1, "\n");
                    continue;
                }
                saved_stdin = fd;
                sys_dup2(fd, 0);
            }

            if (parsed.output_file) {
                int fd = sys_open(parsed.output_file, 1, 0);
                if (fd < 0) {
                    WRITE_STR(1, "Failed to open output file: ");
                    WRITE_STR(1, parsed.output_file);
                    WRITE_STR(1, "\n");
                    if (saved_stdin >= 0) sys_close(saved_stdin);
                    continue;
                }
                saved_stdout = fd;
                sys_dup2(fd, 1);
            }

            execute_command_bg(parsed.commands[0], parsed.background);

            if (saved_stdin >= 0) sys_close(saved_stdin);
            if (saved_stdout >= 0) sys_close(saved_stdout);
        } else {
            WRITE_STR(1, "Complex pipes not yet supported\n");
        }
    }
}

// Entry point (asm to avoid compiler-generated ret with no return address)
__asm__(
    ".globl _start\n"
    "_start:\n"
    "    call shell_main\n"
    "    movl $1, %eax\n"
    "    xorl %ebx, %ebx\n"
    "    int $0x80\n"
);
