// Standalone grep: reads stdin, prints lines matching pattern from argv[1]
// Built as a userspace ELF binary for /bin/grep
//
// Usage: grep <pattern>
// Reads from stdin, prints lines containing <pattern>.

#include "ulib.h"

static char rdbuf[512];
static int rdbuf_len = 0;
static int rdbuf_pos = 0;

static int read_char(char* out) {
    if (rdbuf_pos >= rdbuf_len) {
        rdbuf_len = sys_read(0, rdbuf, sizeof(rdbuf));
        rdbuf_pos = 0;
        if (rdbuf_len <= 0) return 0;
    }
    *out = rdbuf[rdbuf_pos++];
    return 1;
}

void grep_main(int argc, char** argv) {
    if (argc < 2) {
        WRITE_STR(2, "Usage: grep <pattern>\n");
        sys_exit(1);
    }

    const char* pattern = argv[1];
    char line[256];
    int line_pos = 0;

    while (1) {
        char c;
        if (!read_char(&c)) {
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

    sys_exit(0);
}

// Entry point: the kernel places [argc][argv_ptr] on the stack.
// 'call' pushes a return address, aligning the stack for cdecl.
__asm__(
    ".globl _start\n"
    "_start:\n"
    "    call grep_main\n"
    "    movl $1, %eax\n"
    "    xorl %ebx, %ebx\n"
    "    int $0x80\n"
);
