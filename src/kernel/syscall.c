#include <stdint.h>
#include <stddef.h>
#include "kernel/isr.h"
#include "drivers/terminal.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "mm/kmalloc.h"
#include "mm/vmm.h"
#include "lib/string.h"
#include "mm/pmm.h"
#include "fs/fs.h"
#include "ipc/pipe.h"
#include "ipc/signal.h"
#include "lib/elf.h"

// System call numbers
#define SYS_EXIT    1
#define SYS_WRITE   2
#define SYS_READ    3
#define SYS_GETPID  4
#define SYS_SLEEP   5
#define SYS_SBRK    6
#define SYS_FORK    7
#define SYS_WAIT    8
#define SYS_EXECVE  9
#define SYS_PS      10
#define SYS_OPEN    11
#define SYS_CLOSE   12
#define SYS_STAT    13
#define SYS_MKDIR   14
#define SYS_READDIR 15
#define SYS_KILL    16
#define SYS_PIPE    17
#define SYS_DUP2    18

// File descriptors
#define STDIN   0
#define STDOUT  1
#define STDERR  2
#define O_CREAT 0x1

// Maximum number of system calls
#define MAX_SYSCALLS 64

// System call function type (32-bit)
typedef uint32_t (*syscall_func_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

// System call table
static syscall_func_t syscall_table[MAX_SYSCALLS];

// File descriptor table (per-process)
#define MAX_FDS 16
typedef struct {
    fs_node_t* node;
    pipe_t* pipe;
    uint32_t offset;
    int flags;
    int is_pipe;
} fd_entry_t;

// Forward declarations
static fd_entry_t* get_fd_table(void);
static void string_concat(char* dest, const char* src);
static void int_to_string(uint32_t num, char* buf);
extern void interrupt_return_trampoline(void);
static uint32_t sys_fork_with_regs(registers_t* regs);
static uint32_t sys_execve_with_regs(uint32_t path_ptr, uint32_t argv_ptr, uint32_t envp_ptr,
                                     uint32_t arg4, uint32_t arg5, registers_t* regs);

static int is_user_process(process_t* process) {
    return process && process->kind == PROCESS_KIND_USER;
}

static int validate_user_buffer(process_t* process, uint32_t addr, size_t len, bool write) {
    if (!is_user_process(process) || len == 0) {
        return 0;
    }

    if (addr < USER_VADDR_MIN || addr >= KERNEL_BASE) {
        return -1;
    }

    if (addr + len < addr || addr + len > KERNEL_BASE) {
        return -1;
    }

    uint32_t start = addr & ~(PAGE_SIZE - 1);
    uint32_t end = (addr + len - 1) & ~(PAGE_SIZE - 1);

    for (uint32_t page = start; page <= end; page += PAGE_SIZE) {
        uint32_t flags = vmm_get_page_flags(process->page_directory, page);
        if (!(flags & PAGE_PRESENT) || !(flags & PAGE_USER)) {
            return -1;
        }
        if (write && !(flags & PAGE_WRITABLE)) {
            return -1;
        }
    }

    return 0;
}

static int copy_from_user(void* dst, uint32_t src, size_t len) {
    process_t* current = process_get_current();
    if (validate_user_buffer(current, src, len, false) < 0) {
        return -1;
    }

    memcpy(dst, (const void*)src, len);
    return 0;
}

static int copy_to_user(uint32_t dst, const void* src, size_t len) {
    process_t* current = process_get_current();
    if (validate_user_buffer(current, dst, len, true) < 0) {
        return -1;
    }

    memcpy((void*)dst, src, len);
    return 0;
}

static int copy_string_from_user(char* dst, size_t dst_len, uint32_t src) {
    process_t* current = process_get_current();
    size_t i;

    if (dst_len == 0) {
        return -1;
    }

    if (!is_user_process(current)) {
        strncpy(dst, (const char*)src, dst_len - 1);
        dst[dst_len - 1] = '\0';
        return 0;
    }

    for (i = 0; i < dst_len - 1; i++) {
        if (validate_user_buffer(current, src + i, 1, false) < 0) {
            return -1;
        }

        dst[i] = ((const char*)src)[i];
        if (dst[i] == '\0') {
            return 0;
        }
    }

    dst[dst_len - 1] = '\0';
    return -1;
}

// sys_exit: Terminate current process
static uint32_t sys_exit(uint32_t status, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    process_t* current = process_get_current();
    if (!current) {
        return 0;
    }

    terminal_writestring("\n[SYSCALL] Process ");
    terminal_writestring(current->name);
    terminal_writestring(" exiting with status: ");
    terminal_print_int((int)status);
    terminal_writestring("\n");

    process_exit((int)status);
    return (uint32_t)-1;
}

// sys_write: Write to file descriptor
static uint32_t sys_write(uint32_t fd, uint32_t buf_ptr, uint32_t count, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    process_t* current = process_get_current();

    if (buf_ptr == 0 || count == 0) {
        return 0;
    }

    if (validate_user_buffer(current, buf_ptr, count, false) < 0) {
        return (uint32_t)-1;
    }

    const char* buf = (const char*)buf_ptr;

    if (fd == STDOUT || fd == STDERR) {
        for (size_t i = 0; i < count; i++) {
            terminal_putchar(buf[i]);
        }
        return count;
    }

    fd_entry_t* fd_table = get_fd_table();
    if (fd_table && fd < MAX_FDS) {
        if (fd_table[fd].is_pipe && fd_table[fd].pipe) {
            return pipe_write(fd_table[fd].pipe, buf, count);
        } else if (fd_table[fd].node) {
            fs_node_t* node = fd_table[fd].node;
            int written = fs_write(node, fd_table[fd].offset, count, (uint8_t*)buf);
            if (written > 0) {
                fd_table[fd].offset += written;
            }
            return written;
        }
    }

    return (uint32_t)-1;
}

// sys_read: Read from file descriptor
static uint32_t sys_read(uint32_t fd, uint32_t buf_ptr, uint32_t count, uint32_t arg4, uint32_t arg5) {
    (void)arg4; (void)arg5;
    process_t* current = process_get_current();

    if (buf_ptr == 0 || count == 0) {
        return 0;
    }

    if (validate_user_buffer(current, buf_ptr, count, true) < 0) {
        return (uint32_t)-1;
    }

    char* buf = (char*)buf_ptr;

    if (fd == STDIN) {
        size_t read = 0;

        while (read < count) {
            if (keyboard_has_char()) {
                char c = keyboard_getchar();
                buf[read++] = c;

                if (c == '\n') {
                    break;
                }
            } else {
                process_yield();
            }
        }

        return read;
    }

    fd_entry_t* fd_table = get_fd_table();
    if (fd_table && fd < MAX_FDS) {
        if (fd_table[fd].is_pipe && fd_table[fd].pipe) {
            return pipe_read(fd_table[fd].pipe, buf, count);
        } else if (fd_table[fd].node) {
            fs_node_t* node = fd_table[fd].node;
            int bytes_read = fs_read(node, fd_table[fd].offset, count, (uint8_t*)buf);
            if (bytes_read > 0) {
                fd_table[fd].offset += bytes_read;
            }
            return bytes_read;
        }
    }

    return (uint32_t)-1;
}

// sys_getpid: Get current process ID
static uint32_t sys_getpid(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return process_get_pid();
}

// sys_sleep: Sleep for milliseconds
static uint32_t sys_sleep(uint32_t ms, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    sleep_ms(ms);
    return 0;
}

// sys_sbrk: Extend data segment
static uint32_t sys_sbrk(uint32_t increment, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    process_t* current = process_get_current();
    if (!current) {
        return (uint32_t)-1;
    }

    if (!is_user_process(current)) {
        return (uint32_t)-1;
    }

    uint32_t old_heap = current->heap_current;

    if (increment == 0) {
        return old_heap;
    }

    int32_t signed_inc = (int32_t)increment;
    uint32_t new_heap;

    // Check for overflow/underflow before computing
    if (signed_inc > 0) {
        if (old_heap > (uint32_t)0xFFFFFFFF - (uint32_t)signed_inc) {
            return (uint32_t)-1;
        }
        new_heap = old_heap + (uint32_t)signed_inc;
    } else {
        uint32_t abs_dec = (uint32_t)(-signed_inc);
        if (abs_dec > old_heap) {
            return (uint32_t)-1;
        }
        new_heap = old_heap - abs_dec;
    }

    if (new_heap > current->heap_max) {
        return (uint32_t)-1;
    }

    if (new_heap < current->heap_start) {
        return (uint32_t)-1;
    }

    if ((int32_t)increment > 0) {
        uint32_t old_page = current->heap_current & ~(PAGE_SIZE - 1);
        uint32_t new_page = (new_heap + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint32_t page = old_page; page < new_page; page += PAGE_SIZE) {
            if (page >= current->heap_current) {
                if (vmm_alloc_user_pages(current, page, 1) < 0) {
                    return (uint32_t)-1;
                }
            }
        }
    }

    current->heap_current = new_heap;
    return old_heap;
}

// sys_fork: Create a child process
static uint32_t sys_fork(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    return (uint32_t)-1;
}

static uint32_t sys_fork_with_regs(registers_t* regs) {
    process_t* parent = process_get_current();
    if (!parent) {
        return (uint32_t)-1;
    }

    if (!regs || !is_user_process(parent) || (regs->cs & 0x3) != 0x3) {
        terminal_writestring("[FORK] Refusing to fork a kernel thread\n");
        return (uint32_t)-1;
    }

    terminal_writestring("[FORK] Starting fork from PID ");
    terminal_print_uint(parent->pid);
    terminal_writestring("\n");

    process_t* child = allocate_process_struct();
    if (!child) {
        terminal_writestring("[FORK] Failed to allocate child process\n");
        return (uint32_t)-1;
    }

    strncpy(child->name, parent->name, 24);
    string_concat(child->name, "[child]");

    child->page_directory = vmm_clone_address_space(parent->page_directory);
    if (!child->page_directory) {
        terminal_writestring("[FORK] Failed to clone address space\n");
        free_process_struct(child);
        return (uint32_t)-1;
    }

    child->context = parent->context;
    child->kind = parent->kind;
    child->parent_pid = parent->pid;
    child->state = PROCESS_STATE_READY;
    child->priority = parent->priority;
    child->ticks_remaining = DEFAULT_QUANTUM;
    child->ticks_total = 0;

    child->heap_start = parent->heap_start;
    child->heap_current = parent->heap_current;
    child->heap_max = parent->heap_max;
    child->stack_bottom = parent->stack_bottom;
    child->stack_top = parent->stack_top;
    child->pages_allocated = parent->pages_allocated;
    child->page_faults = 0;

    child->pending_signals = 0;
    child->signal_mask = parent->signal_mask;
    for (int i = 0; i < 32; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }

    if (parent->fd_table && child->fd_table) {
        fd_entry_t* parent_fds = (fd_entry_t*)parent->fd_table;
        fd_entry_t* child_fds = (fd_entry_t*)child->fd_table;

        for (int i = 0; i < MAX_FDS; i++) {
            child_fds[i] = parent_fds[i];
        }
    }

    child->user_entry = parent->user_entry;
    registers_t* child_regs =
        (registers_t*)((uint8_t*)child->kernel_stack + KERNEL_STACK_SIZE - sizeof(registers_t));
    *child_regs = *regs;
    child_regs->eax = 0;

    child->context.edi = 0;
    child->context.esi = 0;
    child->context.ebx = 0;
    child->context.ebp = 0;
    child->context.esp = (uint32_t)child_regs;
    child->context.eip = (uint32_t)interrupt_return_trampoline;
    child->context.eflags = regs->eflags;

    ready_queue_push(child);

    terminal_writestring("[FORK] Created child PID ");
    terminal_print_uint(child->pid);
    terminal_writestring("\n");

    return child->pid;
}

// sys_wait: Wait for child process to exit
static uint32_t sys_wait(uint32_t status_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    process_t* parent = process_get_current();
    if (!parent) {
        return (uint32_t)-1;
    }

    terminal_writestring("[WAIT] Process waiting for child\n");

    while (1) {
        process_t* child = find_zombie_child(parent->pid);
        if (child) {
            if (status_ptr) {
                int status = child->exit_status;
                if (copy_to_user(status_ptr, &status, sizeof(status)) < 0) {
                    if (!is_user_process(parent)) {
                        *(int*)status_ptr = status;
                    } else {
                        return (uint32_t)-1;
                    }
                }
            }

            uint32_t child_pid = child->pid;

            terminal_writestring("[WAIT] Reaping child PID ");
            terminal_print_uint(child_pid);
            terminal_writestring("\n");

            free_process_struct(child);

            return child_pid;
        }

        parent->state = PROCESS_STATE_WAITING;
        schedule();
    }
}

static uint32_t sys_execve(uint32_t path_ptr, uint32_t argv_ptr, uint32_t envp_ptr, uint32_t arg4, uint32_t arg5) {
    (void)path_ptr; (void)argv_ptr; (void)envp_ptr; (void)arg4; (void)arg5;
    return (uint32_t)-1;
}

// Maximum argv entries and total string data for execve
#define EXEC_MAX_ARGC 16
#define EXEC_MAX_STRINGS 2048

static uint32_t sys_execve_with_regs(uint32_t path_ptr, uint32_t argv_ptr, uint32_t envp_ptr,
                                     uint32_t arg4, uint32_t arg5, registers_t* regs) {
    (void)envp_ptr; (void)arg4; (void)arg5;

    process_t* current = process_get_current();
    char path[128];
    fs_node_t* node;
    uint8_t* image = NULL;
    process_t new_image;
    uint32_t* old_pd;

    // Kernel buffer for argv strings (copied before address space switch)
    char argv_buf[EXEC_MAX_STRINGS];
    int argv_offsets[EXEC_MAX_ARGC];  // offset into argv_buf for each arg
    int argv_lens[EXEC_MAX_ARGC];     // length including null terminator
    int exec_argc = 0;
    int argv_total = 0;

    if (!current || !regs || !is_user_process(current) || path_ptr == 0) {
        return (uint32_t)-1;
    }

    if (copy_string_from_user(path, sizeof(path), path_ptr) < 0) {
        return (uint32_t)-1;
    }

    // Copy argv strings from caller's address space into kernel buffer
    if (argv_ptr) {
        for (int i = 0; i < EXEC_MAX_ARGC; i++) {
            uint32_t str_ptr;
            if (copy_from_user(&str_ptr, argv_ptr + i * sizeof(uint32_t), sizeof(uint32_t)) < 0) {
                break;
            }
            if (str_ptr == 0) break;  // NULL terminator

            char arg[256];
            if (copy_string_from_user(arg, sizeof(arg), str_ptr) < 0) {
                break;
            }

            int len = 0;
            while (arg[len]) len++;
            len++;  // include null terminator

            if (argv_total + len > EXEC_MAX_STRINGS) break;

            argv_offsets[exec_argc] = argv_total;
            argv_lens[exec_argc] = len;
            memcpy(argv_buf + argv_total, arg, len);
            argv_total += len;
            exec_argc++;
        }
    }

    terminal_writestring("[EXEC] Executing: ");
    terminal_writestring(path);
    terminal_writestring("\n");

    node = fs_resolve_path(path);
    if (!node || node->type != FS_TYPE_FILE || node->size == 0) {
        terminal_writestring("[EXEC] Program not found\n");
        return (uint32_t)-1;
    }

    image = (uint8_t*)kmalloc(node->size);
    if (!image) {
        return (uint32_t)-1;
    }

    if (fs_read(node, 0, node->size, image) != (int)node->size) {
        kfree(image);
        return (uint32_t)-1;
    }

    memset(&new_image, 0, sizeof(new_image));
    new_image.kind = PROCESS_KIND_USER;
    new_image.page_directory = vmm_create_address_space();
    if (!new_image.page_directory) {
        kfree(image);
        return (uint32_t)-1;
    }

    if (vmm_setup_user_stack(&new_image) < 0 ||
        vmm_setup_user_heap(&new_image) < 0 ||
        elf_load(&new_image, image, node->size) < 0) {
        vmm_destroy_address_space(new_image.page_directory);
        kfree(image);
        return (uint32_t)-1;
    }

    old_pd = current->page_directory;

    current->page_directory = new_image.page_directory;
    current->heap_start = new_image.heap_start;
    current->heap_current = new_image.heap_current;
    current->heap_max = new_image.heap_max;
    current->stack_bottom = new_image.stack_bottom;
    current->stack_top = new_image.stack_top;
    current->pages_allocated = new_image.pages_allocated;
    current->page_faults = 0;
    current->user_entry = new_image.user_entry;

    vmm_switch_address_space(current->page_directory);
    vmm_destroy_address_space(old_pd);
    kfree(image);

    // Set up user stack with argc/argv
    // Layout (growing downward):
    //   [string data]        <- arg strings stored here
    //   argv[argc] = NULL
    //   argv[1] pointer
    //   argv[0] pointer
    //   argc                 <- ESP points here
    uint32_t sp = current->stack_top;

    // Pre-check: ensure argv data fits within the user stack
    // Need: argv_total bytes for strings + (exec_argc+1)*4 for pointers + 8 for argc/argv_base
    uint32_t needed = (uint32_t)argv_total + ((uint32_t)exec_argc + 1) * 4 + 8 + 4; // +4 for alignment slack
    if (needed > sp - current->stack_bottom) {
        // Not enough stack space for argv — fail the exec
        return (uint32_t)-1;
    }

    // Copy arg strings onto user stack and record their user-space addresses
    uint32_t str_addrs[EXEC_MAX_ARGC];
    for (int i = exec_argc - 1; i >= 0; i--) {
        sp -= argv_lens[i];
        memcpy((void*)sp, argv_buf + argv_offsets[i], argv_lens[i]);
        str_addrs[i] = sp;
    }

    // Align to 4 bytes
    sp &= ~3;

    // Push NULL terminator for argv
    sp -= 4;
    *(uint32_t*)sp = 0;

    // Push argv pointers (reverse order so argv[0] is at lowest address)
    for (int i = exec_argc - 1; i >= 0; i--) {
        sp -= 4;
        *(uint32_t*)sp = str_addrs[i];
    }

    // Save argv pointer (points to argv[0])
    uint32_t argv_base = sp;

    // Push argv pointer
    sp -= 4;
    *(uint32_t*)sp = argv_base;

    // Push argc
    sp -= 4;
    *(uint32_t*)sp = (uint32_t)exec_argc;

    regs->eip = current->user_entry;
    regs->esp = sp;
    regs->eax = 0;

    return 0;
}

// sys_ps: List processes
static uint32_t sys_ps(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    terminal_writestring("PID  PPID  STATE     NAME\n");
    terminal_writestring("---  ----  --------  ----------\n");

    extern process_t* process_table[];
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = process_table[i];
        if (p) {
            const char* state_str = "UNKNOWN";
            switch (p->state) {
                case PROCESS_STATE_READY:      state_str = "READY"; break;
                case PROCESS_STATE_RUNNING:    state_str = "RUN"; break;
                case PROCESS_STATE_BLOCKED:    state_str = "BLOCK"; break;
                case PROCESS_STATE_WAITING:    state_str = "WAIT"; break;
                case PROCESS_STATE_ZOMBIE:     state_str = "ZOMBIE"; break;
                case PROCESS_STATE_TERMINATED: state_str = "TERM"; break;
            }

            char pid_str[16];
            int_to_string(p->pid, pid_str);
            terminal_writestring(pid_str);
            terminal_writestring("    ");

            char ppid_str[16];
            int_to_string(p->parent_pid, ppid_str);
            terminal_writestring(ppid_str);
            terminal_writestring("    ");

            terminal_writestring(state_str);

            int state_len = 0;
            while (state_str[state_len]) state_len++;
            for (int j = state_len; j < 8; j++) {
                terminal_writestring(" ");
            }
            terminal_writestring("  ");

            terminal_writestring(p->name);
            terminal_writestring("\n");
        }
    }

    return 0;
}

static void int_to_string(uint32_t num, char* buf) {
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    uint32_t temp = num;
    int digits = 0;
    while (temp > 0) {
        digits++;
        temp /= 10;
    }

    buf[digits] = '\0';
    for (int j = digits - 1; j >= 0; j--) {
        buf[j] = '0' + (num % 10);
        num /= 10;
    }
}

static void string_concat(char* dest, const char* src) {
    while (*dest) dest++;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

// Get current process's fd table
static fd_entry_t* get_fd_table(void) {
    process_t* current = process_get_current();
    if (!current || !current->fd_table) {
        return NULL;
    }
    return (fd_entry_t*)current->fd_table;
}

// Initialize fd table for a process
void init_process_fd_table(process_t* proc) {
    if (!proc) return;

    proc->fd_table = kmalloc(sizeof(fd_entry_t) * MAX_FDS);
    if (!proc->fd_table) return;

    fd_entry_t* fds = (fd_entry_t*)proc->fd_table;

    for (int i = 0; i < MAX_FDS; i++) {
        fds[i].node = NULL;
        fds[i].pipe = NULL;
        fds[i].offset = 0;
        fds[i].flags = 0;
        fds[i].is_pipe = 0;
    }
}

// Close a single fd entry, releasing its resources
static void close_fd_entry(fd_entry_t* entry) {
    if (entry->is_pipe && entry->pipe) {
        pipe_destroy(entry->pipe);
    } else if (entry->node) {
        fs_close(entry->node);
    }
    entry->node = NULL;
    entry->pipe = NULL;
    entry->offset = 0;
    entry->flags = 0;
    entry->is_pipe = 0;
}

// Close all open file descriptors for a process (called during destroy/free)
void cleanup_process_fds(process_t* proc) {
    if (!proc || !proc->fd_table) return;

    fd_entry_t* fds = (fd_entry_t*)proc->fd_table;
    for (int i = 0; i < MAX_FDS; i++) {
        if (fds[i].node || fds[i].pipe) {
            close_fd_entry(&fds[i]);
        }
    }
}

// sys_open: Open a file
static uint32_t sys_open(uint32_t path_ptr, uint32_t flags, uint32_t mode, uint32_t arg4, uint32_t arg5) {
    (void)mode; (void)arg4; (void)arg5;
    char path[128];
    if (!path_ptr) return (uint32_t)-1;
    if (copy_string_from_user(path, sizeof(path), path_ptr) < 0) return (uint32_t)-1;

    fd_entry_t* fd_table = get_fd_table();
    if (!fd_table) return (uint32_t)-1;

    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!fd_table[i].node && !fd_table[i].pipe) {
            fd = i;
            break;
        }
    }

    if (fd == -1) return (uint32_t)-1;

    fs_node_t* node = fs_resolve_path(path);
    if (!node && (flags & O_CREAT)) {
        node = ramfs_create_file_path(path);
        if (!node) return (uint32_t)-1;
    }

    if (!node) return (uint32_t)-1;

    fd_table[fd].node = node;
    fd_table[fd].offset = 0;
    fd_table[fd].flags = flags;
    fd_table[fd].is_pipe = 0;

    return fd;
}

// sys_close: Close a file descriptor
static uint32_t sys_close(uint32_t fd, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;

    fd_entry_t* fd_table = get_fd_table();
    if (!fd_table || fd >= MAX_FDS) {
        return (uint32_t)-1;
    }

    if (!fd_table[fd].node && !fd_table[fd].pipe) {
        return (uint32_t)-1;
    }

    close_fd_entry(&fd_table[fd]);

    return 0;
}

// sys_stat: Get file information
static uint32_t sys_stat(uint32_t path_ptr, uint32_t stat_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    char path[128];
    if (!path_ptr || !stat_ptr) return (uint32_t)-1;
    if (copy_string_from_user(path, sizeof(path), path_ptr) < 0) return (uint32_t)-1;

    struct stat {
        uint32_t size;
        uint32_t type;
    } st;

    fs_node_t* node = fs_resolve_path(path);
    if (!node) return (uint32_t)-1;

    st.size = node->size;
    st.type = node->type;

    if (copy_to_user(stat_ptr, &st, sizeof(st)) < 0) {
        if (!is_user_process(process_get_current())) {
            memcpy((void*)stat_ptr, &st, sizeof(st));
        } else {
            return (uint32_t)-1;
        }
    }

    return 0;
}

// sys_mkdir: Create directory
static uint32_t sys_mkdir(uint32_t path_ptr, uint32_t mode, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)mode; (void)arg3; (void)arg4; (void)arg5;
    char path[128];
    if (!path_ptr) return (uint32_t)-1;
    if (copy_string_from_user(path, sizeof(path), path_ptr) < 0) return (uint32_t)-1;

    fs_node_t* dir = ramfs_create_dir_path(path);
    if (!dir) return (uint32_t)-1;

    return 0;
}

// sys_readdir: Read directory entries
static uint32_t sys_readdir(uint32_t fd, uint32_t dirent_ptr, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    if (!dirent_ptr) return (uint32_t)-1;

    fd_entry_t* fd_table = get_fd_table();
    if (!fd_table || fd >= MAX_FDS || !fd_table[fd].node) {
        return (uint32_t)-1;
    }

    fs_node_t* node = fd_table[fd].node;
    if (node->type != FS_TYPE_DIR) {
        return (uint32_t)-1;
    }

    struct dirent {
        char name[32];
        uint32_t type;
    } de;

    fs_dirent_t* entry = fs_readdir(node, fd_table[fd].offset);
    if (!entry) return 0;

    strncpy(de.name, entry->name, 31);
    de.name[31] = '\0';
    de.type = FS_TYPE_FILE;

    if (copy_to_user(dirent_ptr, &de, sizeof(de)) < 0) {
        if (!is_user_process(process_get_current())) {
            memcpy((void*)dirent_ptr, &de, sizeof(de));
        } else {
            return (uint32_t)-1;
        }
    }

    fd_table[fd].offset++;
    return 1;
}

// syscall_kill: Send signal to process
static uint32_t syscall_kill(uint32_t pid, uint32_t sig, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;

    extern void signal_send(int pid, int sig);
    signal_send((int)pid, (int)sig);

    return 0;
}

// syscall_pipe: Create a pipe
static uint32_t syscall_pipe(uint32_t pipefd_ptr, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    int pipefd[2];
    if (!pipefd_ptr) return (uint32_t)-1;

    fd_entry_t* fd_table = get_fd_table();
    if (!fd_table) return (uint32_t)-1;

    pipe_t* pipe = pipe_create();
    if (!pipe) return (uint32_t)-1;

    int read_fd = -1, write_fd = -1;
    for (int i = 3; i < MAX_FDS; i++) {
        if (!fd_table[i].node && !fd_table[i].pipe) {
            if (read_fd == -1) {
                read_fd = i;
            } else {
                write_fd = i;
                break;
            }
        }
    }

    if (read_fd == -1 || write_fd == -1) {
        pipe_destroy(pipe);
        return (uint32_t)-1;
    }

    fd_table[read_fd].pipe = pipe;
    fd_table[read_fd].is_pipe = 1;
    fd_table[read_fd].flags = 0;

    fd_table[write_fd].pipe = pipe;
    fd_table[write_fd].is_pipe = 1;
    fd_table[write_fd].flags = 1;

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    if (copy_to_user(pipefd_ptr, pipefd, sizeof(pipefd)) < 0) {
        if (!is_user_process(process_get_current())) {
            memcpy((void*)pipefd_ptr, pipefd, sizeof(pipefd));
        } else {
            return (uint32_t)-1;
        }
    }

    return 0;
}

// sys_dup2: Duplicate file descriptor
static uint32_t sys_dup2(uint32_t oldfd, uint32_t newfd, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg3; (void)arg4; (void)arg5;

    fd_entry_t* fd_table = get_fd_table();
    if (!fd_table) return (uint32_t)-1;

    if (oldfd >= MAX_FDS || newfd >= MAX_FDS) {
        return (uint32_t)-1;
    }

    if (!fd_table[oldfd].node && !fd_table[oldfd].pipe) {
        return (uint32_t)-1;
    }

    if (oldfd == newfd) {
        return newfd;
    }

    if (fd_table[newfd].node || fd_table[newfd].pipe) {
        if (fd_table[newfd].node) {
            fs_close(fd_table[newfd].node);
        }
        fd_table[newfd].node = NULL;
        fd_table[newfd].pipe = NULL;
    }

    fd_table[newfd] = fd_table[oldfd];

    return newfd;
}

// System call handler (called from INT 0x80)
void syscall_handler(registers_t* regs) {
    // 32-bit: System call number in EAX
    // Arguments in EBX, ECX, EDX, ESI, EDI (Linux i386 ABI)
    uint32_t syscall_num = regs->eax;

    uint32_t arg1 = regs->ebx;
    uint32_t arg2 = regs->ecx;
    uint32_t arg3 = regs->edx;
    uint32_t arg4 = regs->esi;
    uint32_t arg5 = regs->edi;

    if (syscall_num == SYS_FORK) {
        regs->eax = sys_fork_with_regs(regs);
        return;
    }

    if (syscall_num == SYS_EXECVE) {
        regs->eax = sys_execve_with_regs(arg1, arg2, arg3, arg4, arg5, regs);
        return;
    }

    if (syscall_num >= MAX_SYSCALLS || syscall_table[syscall_num] == NULL) {
        regs->eax = (uint32_t)-1;
        return;
    }

    uint32_t result = syscall_table[syscall_num](arg1, arg2, arg3, arg4, arg5);

    regs->eax = result;
}

// Initialize system call interface
void init_syscalls(void) {
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        syscall_table[i] = NULL;
    }

    syscall_table[SYS_EXIT] = sys_exit;
    syscall_table[SYS_WRITE] = sys_write;
    syscall_table[SYS_READ] = sys_read;
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_SLEEP] = sys_sleep;
    syscall_table[SYS_SBRK] = sys_sbrk;
    syscall_table[SYS_FORK] = sys_fork;
    syscall_table[SYS_WAIT] = sys_wait;
    syscall_table[SYS_EXECVE] = sys_execve;
    syscall_table[SYS_PS] = sys_ps;
    syscall_table[SYS_OPEN] = sys_open;
    syscall_table[SYS_CLOSE] = sys_close;
    syscall_table[SYS_STAT] = sys_stat;
    syscall_table[SYS_MKDIR] = sys_mkdir;
    syscall_table[SYS_READDIR] = sys_readdir;
    syscall_table[SYS_KILL] = syscall_kill;
    syscall_table[SYS_PIPE] = syscall_pipe;
    syscall_table[SYS_DUP2] = sys_dup2;

    register_interrupt_handler(0x80, syscall_handler);

    terminal_writestring("System call interface initialized (INT 0x80)\n");
}
