// This file implements low-level I/O port operations for SimpleOS.
// It provides functions to read from and write to hardware ports using inline assembly.

#include "drivers/ports.h"

// read a byte from a port
uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__("in %%dx, %%al" : "=a" (result) : "d" (port));
    return result;
}

// send a byte to a port
void outb(uint16_t port, uint8_t data) {
    __asm__("out %%al, %%dx" : : "a" (data), "d" (port));
}
