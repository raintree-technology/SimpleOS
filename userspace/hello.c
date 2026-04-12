// Simple test program
#include "ulib.h"

void hello_main(void) {
    WRITE_STR(1, "Hello from ELF!\n");
    sys_exit(0);
}

__asm__(
    ".globl _start\n"
    "_start:\n"
    "    call hello_main\n"
    "    movl $1, %eax\n"
    "    xorl %ebx, %ebx\n"
    "    int $0x80\n"
);
