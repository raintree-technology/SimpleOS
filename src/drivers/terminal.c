// This file implements terminal output functions for SimpleOS.
// It provides low-level screen manipulation for text mode display.

#include "../include/terminal.h"
#include <stddef.h>
#include <stdint.h>
#include "../include/ports.h"

// VGA text mode constants
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

// VGA color codes
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15
};

// VGA buffer and terminal state
volatile uint16_t* const VGA_BUFFER = (uint16_t*) VGA_MEMORY;
static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;

// Function to calculate string length
static size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

// Scroll the terminal up by one line
static void terminal_scroll(void) {
    // Move all lines up by one
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t from = (y + 1) * VGA_WIDTH + x;
            const size_t to = y * VGA_WIDTH + x;
            VGA_BUFFER[to] = VGA_BUFFER[from];
        }
    }
    
    // Clear the last line
    const size_t last_line_start = (VGA_HEIGHT - 1) * VGA_WIDTH;
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_BUFFER[last_line_start + x] = (uint16_t)' ' | (uint16_t)terminal_color << 8;
    }
    
    // Move cursor to last line
    terminal_row = VGA_HEIGHT - 1;
}

// Check if virtual terminals are enabled
static int vt_enabled = 0;

void terminal_putchar(char c) {
    // If VT enabled, use VT putchar
    if (vt_enabled) {
        extern void vt_putchar(char c);
        vt_putchar(c);
        return;
    }
    
    // Otherwise use regular terminal output
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
        }
    } else if (c == '\r') {
        terminal_column = 0;
    } else if (c == '\b') {
        // Backspace
        if (terminal_column > 0) {
            terminal_column--;
            const size_t index = terminal_row * VGA_WIDTH + terminal_column;
            VGA_BUFFER[index] = (uint16_t)' ' | (uint16_t)terminal_color << 8;
        }
    } else {
        const size_t index = terminal_row * VGA_WIDTH + terminal_column;
        VGA_BUFFER[index] = (uint16_t)c | (uint16_t)terminal_color << 8;
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                terminal_scroll();
            }
        }
    }
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_writestring(const char* data) {
    terminal_write(data, strlen(data));
}

void init_vga(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = VGA_COLOR_LIGHT_GREY | VGA_COLOR_BLACK << 4;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            VGA_BUFFER[index] = (uint16_t)' ' | (uint16_t)terminal_color << 8;
        }
    }
}

// Enable virtual terminals
void terminal_enable_vt(void) {
    vt_enabled = 1;
}

// Set cursor position
void terminal_set_cursor(uint16_t x, uint16_t y) {
    uint16_t pos = y * VGA_WIDTH + x;

    // Cursor LOW port to VGA INDEX register
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    // Cursor HIGH port to VGA INDEX register
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// Print signed integer (32-bit to avoid 64-bit division)
void terminal_print_int(int32_t num) {
    char buffer[12];  // -2147483648 is 11 chars + null
    int i = 0;
    int negative = 0;

    if (num == 0) {
        terminal_putchar('0');
        return;
    }

    if (num < 0) {
        negative = 1;
        num = -num;
    }

    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }

    if (negative) {
        terminal_putchar('-');
    }

    while (i > 0) {
        terminal_putchar(buffer[--i]);
    }
}

// Print unsigned integer (32-bit to avoid 64-bit division)
void terminal_print_uint(uint32_t num) {
    char buffer[11];  // 4294967295 is 10 chars + null
    int i = 0;

    if (num == 0) {
        terminal_putchar('0');
        return;
    }

    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }

    while (i > 0) {
        terminal_putchar(buffer[--i]);
    }
}

// Print hexadecimal with 0x prefix (32-bit)
void terminal_print_hex(uint32_t num) {
    static const char hex_chars[] = "0123456789abcdef";
    char buffer[9];  // 8 hex digits + null
    int i = 0;

    terminal_writestring("0x");

    if (num == 0) {
        terminal_putchar('0');
        return;
    }

    while (num > 0) {
        buffer[i++] = hex_chars[num & 0xF];
        num >>= 4;
    }

    while (i > 0) {
        terminal_putchar(buffer[--i]);
    }
}