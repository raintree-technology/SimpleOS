// Shared userspace library for SimpleOS standalone ELF programs.
// Provides syscall wrappers (INT 0x80) and string utilities.

#ifndef SIMPLEOS_ULIB_H
#define SIMPLEOS_ULIB_H

#include <stdint.h>
#include <stddef.h>

// ---- Syscall primitives (i386 INT 0x80 ABI) ----

static inline uint32_t _syscall0(uint32_t num) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline uint32_t _syscall1(uint32_t num, uint32_t a1) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1) : "memory");
    return ret;
}

static inline uint32_t _syscall2(uint32_t num, uint32_t a1, uint32_t a2) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static inline uint32_t _syscall3(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

// ---- Syscall numbers ----

#define SYS_EXIT    1
#define SYS_WRITE   2
#define SYS_READ    3
#define SYS_GETPID  4
#define SYS_SLEEP   5
#define SYS_FORK    7
#define SYS_WAIT    8
#define SYS_EXECVE  9
#define SYS_PS      10
#define SYS_OPEN    11
#define SYS_CLOSE   12
#define SYS_READDIR 15
#define SYS_KILL    16
#define SYS_PIPE    17
#define SYS_DUP2    18

// ---- Syscall wrappers ----

static inline void sys_exit(int code) {
    _syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

static inline int sys_write(int fd, const char* buf, int len) {
    return (int)_syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, (uint32_t)len);
}

static inline int sys_read(int fd, char* buf, int len) {
    return (int)_syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, (uint32_t)len);
}

static inline int sys_getpid(void) {
    return (int)_syscall0(SYS_GETPID);
}

static inline void sys_sleep(int ms) {
    _syscall1(SYS_SLEEP, (uint32_t)ms);
}

static inline int sys_fork(void) {
    return (int)_syscall0(SYS_FORK);
}

static inline int sys_wait(int* status) {
    return (int)_syscall1(SYS_WAIT, (uint32_t)status);
}

static inline int sys_execve(const char* path, char* const argv[], char* const envp[]) {
    return (int)_syscall3(SYS_EXECVE, (uint32_t)path, (uint32_t)argv, (uint32_t)envp);
}

static inline int sys_ps(void) {
    return (int)_syscall0(SYS_PS);
}

static inline int sys_open(const char* path, int flags, int mode) {
    return (int)_syscall3(SYS_OPEN, (uint32_t)path, (uint32_t)flags, (uint32_t)mode);
}

static inline int sys_close(int fd) {
    return (int)_syscall1(SYS_CLOSE, (uint32_t)fd);
}

static inline int sys_readdir(int fd, void* dirent) {
    return (int)_syscall2(SYS_READDIR, (uint32_t)fd, (uint32_t)dirent);
}

static inline int sys_kill(int pid, int sig) {
    return (int)_syscall2(SYS_KILL, (uint32_t)pid, (uint32_t)sig);
}

static inline int sys_pipe(int pipefd[2]) {
    return (int)_syscall1(SYS_PIPE, (uint32_t)pipefd);
}

static inline int sys_dup2(int oldfd, int newfd) {
    return (int)_syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}

// ---- String utilities ----

static inline int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static inline int str_cmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline char* str_ncpy(char* dst, const char* src, int n) {
    int i = 0;
    while (i < n && src[i]) { dst[i] = src[i]; i++; }
    while (i < n) { dst[i++] = '\0'; }
    return dst;
}

static inline void int_to_str(int num, char* buf) {
    int i = 0;
    unsigned int magnitude;

    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    if (num < 0) {
        buf[i++] = '-';
        magnitude = (unsigned int)(-(num + 1)) + 1;
    } else {
        magnitude = (unsigned int)num;
    }

    int digits = 0;
    unsigned int temp = magnitude;
    while (temp > 0) { digits++; temp /= 10; }

    buf[i + digits] = '\0';
    for (int j = digits - 1; j >= 0; j--) {
        buf[i + j] = '0' + (magnitude % 10);
        magnitude /= 10;
    }
}

static inline char* str_cpy(char* dst, const char* src) {
    char* d = dst;
    while (*src) *d++ = *src++;
    *d = '\0';
    return dst;
}

static inline char* str_chr(const char* s, char c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return 0;
}

static inline char* str_str(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return 0;
}

#define WRITE_STR(fd, s) sys_write(fd, s, str_len(s))

#endif // SIMPLEOS_ULIB_H
