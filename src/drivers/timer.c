#include <stdint.h>
#include "../include/isr.h"
#include "../include/ports.h"
#include "../include/terminal.h"
#include "../include/scheduler.h"

// PIT (Programmable Interval Timer) constants
#define PIT_CHANNEL0_DATA 0x40
#define PIT_CHANNEL1_DATA 0x41
#define PIT_CHANNEL2_DATA 0x42
#define PIT_COMMAND 0x43

#define PIT_FREQUENCY 1193180  // Base frequency of PIT

// Timer state (32-bit for simplicity, avoids 64-bit division)
static uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 0;
static uint32_t ms_per_tick = 0;      // Pre-computed: 1000 / frequency

// Get system uptime in ticks
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

// Get uptime in milliseconds (avoids division by pre-computing)
uint32_t timer_get_ms(void) {
    return timer_ticks * ms_per_tick;
}

// Sleep for specified milliseconds
void sleep_ms(uint32_t ms) {
    uint32_t start = timer_get_ms();
    while (timer_get_ms() - start < ms) {
        asm volatile("hlt");  // Save CPU while waiting
    }
}

// Timer interrupt handler
static void timer_callback(registers_t* regs) {
    (void)regs;  // Unused
    timer_ticks++;

    // Trigger scheduler tick
    scheduler_tick();

    // Send End of Interrupt to PIC
    outb(0x20, 0x20);
}

// Initialize the timer
void init_timer(uint32_t frequency) {
    timer_frequency = frequency;
    ms_per_tick = 1000 / frequency;  // Pre-compute to avoid runtime division
    if (ms_per_tick == 0) ms_per_tick = 1;  // Minimum 1ms

    // Calculate PIT divisor
    uint32_t divisor = PIT_FREQUENCY / frequency;

    // Send the command byte (channel 0, lobyte/hibyte, rate generator)
    outb(PIT_COMMAND, 0x36);

    // Send the frequency divisor
    outb(PIT_CHANNEL0_DATA, divisor & 0xFF);         // Low byte
    outb(PIT_CHANNEL0_DATA, (divisor >> 8) & 0xFF);  // High byte

    // Register timer callback for IRQ0
    register_interrupt_handler(IRQ0, timer_callback);

    terminal_writestring("Timer initialized at ");
    terminal_print_uint(frequency);
    terminal_writestring(" Hz\n");
}
