#include "drivers/vt.h"
#include "drivers/terminal.h"
#include "lib/string.h"
#include "drivers/ports.h"

// Virtual terminals
static virtual_terminal_t terminals[NUM_VIRTUAL_TERMINALS];
static int current_terminal = 0;

// VGA memory
static uint16_t* vga_buffer = (uint16_t*)0xB8000;

void vt_init(void) {
    for (int i = 0; i < NUM_VIRTUAL_TERMINALS; i++) {
        // Clear buffer
        for (int j = 0; j < 80 * 25; j++) {
            terminals[i].buffer[j] = ' ' | (0x0F << 8);  // White on black
        }
        terminals[i].cursor_x = 0;
        terminals[i].cursor_y = 0;
        terminals[i].color = 0x0F;  // White on black
        terminals[i].active = 0;
        terminals[i].shell_pid = -1;
    }
    
    // Activate first terminal
    terminals[0].active = 1;
    vt_refresh();
}

void vt_switch(int terminal) {
    if (terminal < 0 || terminal >= NUM_VIRTUAL_TERMINALS) return;
    if (terminal == current_terminal) return;
    
    // Save current terminal state is already in buffer
    
    // Switch to new terminal
    current_terminal = terminal;
    
    // Mark as active
    terminals[terminal].active = 1;
    
    // Refresh screen with new terminal's content
    vt_refresh();
    
    // Update cursor position
    terminal_set_cursor(terminals[terminal].cursor_x, terminals[terminal].cursor_y);
}

int vt_get_current(void) {
    return current_terminal;
}

void vt_putchar(char c) {
    virtual_terminal_t* vt = &terminals[current_terminal];
    
    if (c == '\n') {
        vt->cursor_x = 0;
        vt->cursor_y++;
    } else if (c == '\r') {
        vt->cursor_x = 0;
    } else if (c == '\b') {
        if (vt->cursor_x > 0) {
            vt->cursor_x--;
            vt->buffer[vt->cursor_y * 80 + vt->cursor_x] = ' ' | (vt->color << 8);
        }
    } else if (c == '\t') {
        // Tab to next 8-character boundary
        vt->cursor_x = (vt->cursor_x + 8) & ~7;
        if (vt->cursor_x >= 80) {
            vt->cursor_x = 0;
            vt->cursor_y++;
        }
    } else if (c >= ' ' && c < 127) {
        // Regular character
        if (vt->cursor_x >= 80) {
            vt->cursor_x = 0;
            vt->cursor_y++;
        }

        if (vt->cursor_y >= 25) {
            // Scroll up
            for (int i = 0; i < 24 * 80; i++) {
                vt->buffer[i] = vt->buffer[i + 80];
            }
            for (int i = 24 * 80; i < 25 * 80; i++) {
                vt->buffer[i] = ' ' | (vt->color << 8);
            }
            vt->cursor_y = 24;
        }

        vt->buffer[vt->cursor_y * 80 + vt->cursor_x] = c | (vt->color << 8);
        vt->cursor_x++;
    }

    // Scroll check for newline/tab (shared with regular char path above)
    if (vt->cursor_y >= 25) {
        for (int i = 0; i < 24 * 80; i++) {
            vt->buffer[i] = vt->buffer[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; i++) {
            vt->buffer[i] = ' ' | (vt->color << 8);
        }
        vt->cursor_y = 24;
    }

    // Update physical screen if this is the current terminal
    if (vt == &terminals[current_terminal]) {
        vt_refresh();
        terminal_set_cursor(vt->cursor_x, vt->cursor_y);
    }
}

// Internal: write char to VT buffer without refreshing screen
static void vt_putchar_norefresh(char c) {
    virtual_terminal_t* vt = &terminals[current_terminal];

    if (c == '\n') {
        vt->cursor_x = 0;
        vt->cursor_y++;
    } else if (c == '\r') {
        vt->cursor_x = 0;
    } else if (c == '\b') {
        if (vt->cursor_x > 0) {
            vt->cursor_x--;
            vt->buffer[vt->cursor_y * 80 + vt->cursor_x] = ' ' | (vt->color << 8);
        }
    } else if (c == '\t') {
        vt->cursor_x = (vt->cursor_x + 8) & ~7;
        if (vt->cursor_x >= 80) {
            vt->cursor_x = 0;
            vt->cursor_y++;
        }
    } else if (c >= ' ' && c < 127) {
        if (vt->cursor_x >= 80) {
            vt->cursor_x = 0;
            vt->cursor_y++;
        }

        if (vt->cursor_y >= 25) {
            for (int i = 0; i < 24 * 80; i++) {
                vt->buffer[i] = vt->buffer[i + 80];
            }
            for (int i = 24 * 80; i < 25 * 80; i++) {
                vt->buffer[i] = ' ' | (vt->color << 8);
            }
            vt->cursor_y = 24;
        }

        vt->buffer[vt->cursor_y * 80 + vt->cursor_x] = c | (vt->color << 8);
        vt->cursor_x++;
    }

    if (vt->cursor_y >= 25) {
        for (int i = 0; i < 24 * 80; i++) {
            vt->buffer[i] = vt->buffer[i + 80];
        }
        for (int i = 24 * 80; i < 25 * 80; i++) {
            vt->buffer[i] = ' ' | (vt->color << 8);
        }
        vt->cursor_y = 24;
    }
}

// Write string to current virtual terminal (single refresh at end)
void vt_writestring(const char* str) {
    while (*str) {
        vt_putchar_norefresh(*str++);
    }
    vt_refresh();
    virtual_terminal_t* vt = &terminals[current_terminal];
    terminal_set_cursor(vt->cursor_x, vt->cursor_y);
}

// Clear current virtual terminal
void vt_clear(void) {
    virtual_terminal_t* vt = &terminals[current_terminal];
    
    for (int i = 0; i < 80 * 25; i++) {
        vt->buffer[i] = ' ' | (vt->color << 8);
    }
    
    vt->cursor_x = 0;
    vt->cursor_y = 0;
    
    vt_refresh();
    terminal_set_cursor(0, 0);
}

// Update physical screen from current VT
void vt_refresh(void) {
    virtual_terminal_t* vt = &terminals[current_terminal];
    
    // Copy virtual terminal buffer to VGA memory
    memcpy(vga_buffer, vt->buffer, sizeof(vt->buffer));
    
    // Show terminal indicator in top-right corner
    const char* indicators[] = {"[F1]", "[F2]", "[F3]", "[F4]"};
    const char* indicator = indicators[current_terminal];
    int x = 76;  // Right side of screen
    
    for (int i = 0; indicator[i]; i++) {
        vga_buffer[x + i] = indicator[i] | (0x1F << 8);  // White on blue
    }
}

// Get VT for specific terminal
virtual_terminal_t* vt_get(int terminal) {
    if (terminal < 0 || terminal >= NUM_VIRTUAL_TERMINALS) return NULL;
    return &terminals[terminal];
}
