// This file implements keyboard input handling for SimpleOS.
// It sets up the keyboard interrupt handler and translates scancodes to ASCII characters.

#include <stdint.h>
#include <stdbool.h>
#include "drivers/ports.h"
#include "drivers/terminal.h"
#include "kernel/isr.h"
#include "drivers/keyboard.h"
#include "kernel/process.h"
#include "ipc/signal.h"

#define KEYBOARD_DATA_PORT 0x60
#define IRQ1 33  // Keyboard IRQ

// Keyboard buffer (simple circular buffer)
static char kbd_buffer[256];
static uint8_t kbd_read_pos = 0;
static uint8_t kbd_write_pos = 0;

// Control key state
static bool ctrl_pressed = false;
static bool alt_pressed = false;
static bool shift_pressed = false;

// Special keys
#define KEY_UP    0x48
#define KEY_DOWN  0x50
#define KEY_LEFT  0x4B
#define KEY_RIGHT 0x4D
#define KEY_TAB   0x0F
#define KEY_CTRL  0x1D
#define KEY_CTRL_RELEASE 0x9D
#define KEY_ALT   0x38
#define KEY_ALT_RELEASE 0xB8
#define KEY_LEFT_SHIFT 0x2A
#define KEY_LEFT_SHIFT_RELEASE 0xAA
#define KEY_RIGHT_SHIFT 0x36
#define KEY_RIGHT_SHIFT_RELEASE 0xB6
#define KEY_F1    0x3B
#define KEY_F2    0x3C
#define KEY_F3    0x3D
#define KEY_F4    0x3E

// US keyboard layout scancodes (0-127)
static char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static char apply_shift(uint8_t scancode, char c) {
    if (!shift_pressed) {
        return c;
    }

    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }

    switch (scancode) {
        case 0x02: return '!';
        case 0x03: return '@';
        case 0x04: return '#';
        case 0x05: return '$';
        case 0x06: return '%';
        case 0x07: return '^';
        case 0x08: return '&';
        case 0x09: return '*';
        case 0x0A: return '(';
        case 0x0B: return ')';
        case 0x0C: return '_';
        case 0x0D: return '+';
        case 0x1A: return '{';
        case 0x1B: return '}';
        case 0x27: return ':';
        case 0x28: return '"';
        case 0x29: return '~';
        case 0x2B: return '|';
        case 0x33: return '<';
        case 0x34: return '>';
        case 0x35: return '?';
        default: return c;
    }
}

// Keyboard interrupt handler
static void keyboard_callback(registers_t* regs) {
    (void)regs;  // Unused parameter
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle control key state
    if (scancode == KEY_CTRL) {
        ctrl_pressed = true;
        goto done;
    } else if (scancode == KEY_CTRL_RELEASE) {
        ctrl_pressed = false;
        goto done;
    }
    
    // Handle alt key state
    if (scancode == KEY_ALT) {
        alt_pressed = true;
        goto done;
    } else if (scancode == KEY_ALT_RELEASE) {
        alt_pressed = false;
        goto done;
    }

    if (scancode == KEY_LEFT_SHIFT || scancode == KEY_RIGHT_SHIFT) {
        shift_pressed = true;
        goto done;
    } else if (scancode == KEY_LEFT_SHIFT_RELEASE || scancode == KEY_RIGHT_SHIFT_RELEASE) {
        shift_pressed = false;
        goto done;
    }
    
    // Handle Alt+F1-F4 for virtual terminal switching
    if (alt_pressed && scancode >= KEY_F1 && scancode <= KEY_F4) {
        extern void vt_switch(int terminal);
        vt_switch(scancode - KEY_F1);  // F1=0, F2=1, etc.
        goto done;
    }
    
    if (!(scancode & 0x80)) {  // Key press
        // Handle special keys (arrow keys, etc)
        if (scancode == KEY_UP || scancode == KEY_DOWN ||
            scancode == KEY_LEFT || scancode == KEY_RIGHT) {
            // Store escape sequence for arrow keys
            // Check that all 3 slots are available before writing any
            uint8_t pos1 = (kbd_write_pos + 1) % 256;
            uint8_t pos2 = (kbd_write_pos + 2) % 256;
            uint8_t pos3 = (kbd_write_pos + 3) % 256;
            if (pos1 != kbd_read_pos && pos2 != kbd_read_pos && pos3 != kbd_read_pos) {
                char arrow_char = 'A';  // Default UP
                if (scancode == KEY_DOWN) arrow_char = 'B';
                else if (scancode == KEY_RIGHT) arrow_char = 'C';
                else if (scancode == KEY_LEFT) arrow_char = 'D';

                kbd_buffer[kbd_write_pos] = '\033';  // ESC
                kbd_buffer[pos1] = '[';
                kbd_buffer[pos2] = arrow_char;
                kbd_write_pos = pos3;
            }
        } else if (scancode == KEY_TAB) {
            // TAB key - add to buffer as special character
            uint8_t next_write = (kbd_write_pos + 1) % 256;
            if (next_write != kbd_read_pos) {
                kbd_buffer[kbd_write_pos] = '\t';
                kbd_write_pos = next_write;
            }
        } else {
            char c = apply_shift(scancode, kbd_us[scancode]);
            if (c) {
                // Check for Ctrl+C (SIGINT)
                if (ctrl_pressed && (c == 'c' || c == 'C')) {
                    terminal_writestring("^C\n");
                    // Send SIGINT to foreground process
                    process_t* current = process_get_current();
                    if (current && current->pid != 1) {  // Don't kill init
                        signal_send(current->pid, SIGINT);
                    }
                    goto done;
                }

                // Check for Ctrl+Z (SIGTSTP)
                if (ctrl_pressed && (c == 'z' || c == 'Z')) {
                    terminal_writestring("^Z\n");
                    // Send SIGTSTP to foreground process
                    process_t* current = process_get_current();
                    if (current && current->pid != 1) {  // Don't stop init
                        signal_send(current->pid, SIGTSTP);
                    }
                    goto done;
                }
                
                // Add to buffer
                uint8_t next_write = (kbd_write_pos + 1) % 256;
                if (next_write != kbd_read_pos) {  // Buffer not full
                    kbd_buffer[kbd_write_pos] = c;
                    kbd_write_pos = next_write;
                }
                
                // Echo to terminal (except for special chars)
                if (c != '\033') {
                    terminal_putchar(c);
                }
            }
        }
    }

done:
    outb(0x20, 0x20);
}

// Check if keyboard has character available
bool keyboard_has_char(void) {
    return kbd_read_pos != kbd_write_pos;
}

// Get character from keyboard buffer
char keyboard_getchar(void) {
    if (kbd_read_pos == kbd_write_pos) {
        return 0;  // No character available
    }
    
    char c = kbd_buffer[kbd_read_pos];
    kbd_read_pos = (kbd_read_pos + 1) % 256;
    return c;
}

// Initialize keyboard and register interrupt handler
void init_keyboard() {
    kbd_read_pos = 0;
    kbd_write_pos = 0;
    register_interrupt_handler(IRQ1, keyboard_callback);
}
