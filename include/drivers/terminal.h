#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>
#include <stdint.h>

// Terminal initialization
void init_vga(void);

// Basic output
void terminal_putchar(char c);
void terminal_write(const char* data, size_t size);
void terminal_writestring(const char* data);

// Number printing (32-bit to avoid 64-bit division)
void terminal_print_int(int32_t num);
void terminal_print_uint(uint32_t num);
void terminal_print_hex(uint32_t num);

// Cursor control
void terminal_set_cursor(uint16_t x, uint16_t y);

// Virtual terminal support
void terminal_enable_vt(void);

#endif // TERMINAL_H
