// Standalone wc: counts lines, words, and characters from stdin
// Built as a userspace ELF binary for /bin/wc

#include "ulib.h"

void wc_main(void) {
    int lines = 0;
    int words = 0;
    int chars = 0;
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

    char num_buf[32];

    WRITE_STR(1, "  ");
    int_to_str(lines, num_buf);
    WRITE_STR(1, num_buf);

    WRITE_STR(1, "  ");
    int_to_str(words, num_buf);
    WRITE_STR(1, num_buf);

    WRITE_STR(1, "  ");
    int_to_str(chars, num_buf);
    WRITE_STR(1, num_buf);

    WRITE_STR(1, "\n");

    sys_exit(0);
}

// Entry point
__asm__(
    ".globl _start\n"
    "_start:\n"
    "    call wc_main\n"
    "    movl $1, %eax\n"
    "    xorl %ebx, %ebx\n"
    "    int $0x80\n"
);
