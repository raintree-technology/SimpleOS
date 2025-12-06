// This file implements keyboard input handling for SimpleOS.
// It sets up the keyboard interrupt handler and translates scancodes to ASCII characters.

#include <stdint.h>
#include <stdbool.h>
#include "../include/ports.h"
#include "../include/terminal.h"
#include "../include/isr.h"
#include "../include/keyboard.h"
#include "../include/process.h"
#include "../include/signal.h"

#define KEYBOARD_DATA_PORT 0x60
#define IRQ1 33  // Keyboard IRQ

// Keyboard buffer (simple circular buffer)
static char kbd_buffer[256];
static uint8_t kbd_read_pos = 0;
static uint8_t kbd_write_pos = 0;

// Control key state
static bool ctrl_pressed = false;
static bool alt_pressed = false;

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

// Keyboard interrupt handler
static void keyboard_callback(registers_t* regs) {
    (void)regs;  // Unused parameter
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle control key state
    if (scancode == KEY_CTRL) {
        ctrl_pressed = true;
        return;
    } else if (scancode == KEY_CTRL_RELEASE) {
        ctrl_pressed = false;
        return;
    }
    
    // Handle alt key state
    if (scancode == KEY_ALT) {
        alt_pressed = true;
        return;
    } else if (scancode == KEY_ALT_RELEASE) {
        alt_pressed = false;
        return;
    }
    
    // Handle Alt+F1-F4 for virtual terminal switching
    if (alt_pressed && scancode >= KEY_F1 && scancode <= KEY_F4) {
        extern void vt_switch(int terminal);
        vt_switch(scancode - KEY_F1);  // F1=0, F2=1, etc.
        return;
    }
    
    if (!(scancode & 0x80)) {  // Key press
        // Handle special keys (arrow keys, etc)
        if (scancode == KEY_UP || scancode == KEY_DOWN || 
            scancode == KEY_LEFT || scancode == KEY_RIGHT) {
            // Store escape sequence for arrow keys
            uint8_t next_write = (kbd_write_pos + 1) % 256;
            if (next_write != kbd_read_pos) {
                kbd_buffer[kbd_write_pos] = '\033';  // ESC
                kbd_write_pos = next_write;
                
                next_write = (kbd_write_pos + 1) % 256;
                if (next_write != kbd_read_pos) {
                    kbd_buffer[kbd_write_pos] = '[';
                    kbd_write_pos = next_write;
                    
                    next_write = (kbd_write_pos + 1) % 256;
                    if (next_write != kbd_read_pos) {
                        // Map to ANSI sequences
                        char arrow_char = 'A';  // Default UP
                        if (scancode == KEY_DOWN) arrow_char = 'B';
                        else if (scancode == KEY_RIGHT) arrow_char = 'C';
                        else if (scancode == KEY_LEFT) arrow_char = 'D';
                        
                        kbd_buffer[kbd_write_pos] = arrow_char;
                        kbd_write_pos = next_write;
                    }
                }
            }
        } else if (scancode == KEY_TAB) {
            // TAB key - add to buffer as special character
            uint8_t next_write = (kbd_write_pos + 1) % 256;
            if (next_write != kbd_read_pos) {
                kbd_buffer[kbd_write_pos] = '\t';
                kbd_write_pos = next_write;
            }
        } else {
            char c = kbd_us[scancode];
            if (c) {
                // Check for Ctrl+C (SIGINT)
                if (ctrl_pressed && (c == 'c' || c == 'C')) {
                    terminal_writestring("^C\n");
                    // Send SIGINT to foreground process
                    process_t* current = process_get_current();
                    if (current && current->pid != 1) {  // Don't kill init
                        signal_send(current->pid, SIGINT);
                    }
                    return;
                }

                // Check for Ctrl+Z (SIGTSTP)
                if (ctrl_pressed && (c == 'z' || c == 'Z')) {
                    terminal_writestring("^Z\n");
                    // Send SIGTSTP to foreground process
                    process_t* current = process_get_current();
                    if (current && current->pid != 1) {  // Don't stop init
                        signal_send(current->pid, SIGTSTP);
                    }
                    return;
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