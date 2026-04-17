#ifndef VT_H
#define VT_H

#include <stdint.h>
#include <stddef.h>

#define NUM_VIRTUAL_TERMINALS 4
#define VT_BUFFER_SIZE (80 * 25 * 2)  // 80x25 screen with attributes

typedef struct {
    uint16_t buffer[80 * 25];      // Character + attribute for each cell
    uint16_t cursor_x;
    uint16_t cursor_y;
    uint8_t color;
    int active;
    int shell_pid;                 // PID of shell running on this VT
} virtual_terminal_t;

void vt_init(void);

// Switch to virtual terminal (0-3)
void vt_switch(int terminal);

int vt_get_current(void);

// Write to current virtual terminal
void vt_putchar(char c);
void vt_writestring(const char* str);

void vt_clear(void);

// Update physical screen from current VT
void vt_refresh(void);

// Get VT for specific terminal
virtual_terminal_t* vt_get(int terminal);

#endif