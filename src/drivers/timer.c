#include <stdint.h>
#include "kernel/isr.h"
#include "drivers/ports.h"
#include "drivers/terminal.h"
#include "kernel/scheduler.h"
#include "kernel/process.h"

// PIT (Programmable Interval Timer) constants
#define PIT_CHANNEL0_DATA 0x40
#define PIT_CHANNEL1_DATA 0x41
#define PIT_CHANNEL2_DATA 0x42
#define PIT_COMMAND 0x43

#define PIT_FREQUENCY 1193180  // Base frequency of PIT

// Timer state
static uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 0;

extern process_t* process_table[];

// Get system uptime in ticks
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

// Get uptime in milliseconds
uint32_t timer_get_ms(void) {
    return timer_ticks * 1000 / timer_frequency;
}

// Sleep for specified milliseconds
void sleep_ms(uint32_t ms) {
    process_t* current = process_get_current();

    if (current && current->pid != 0 && timer_frequency != 0) {
        uint32_t sleep_ticks = (ms * timer_frequency + 999) / 1000;
        uint32_t target;

        if (sleep_ticks == 0) {
            sleep_ticks = 1;
        }

        target = timer_ticks + sleep_ticks;
        while ((int32_t)(target - timer_ticks) > 0) {
            current->wakeup_tick = target;
            current->state = PROCESS_STATE_BLOCKED;
            schedule();
        }
        current->wakeup_tick = 0;
        return;
    }

    uint32_t start = timer_get_ms();
    while (timer_get_ms() - start < ms) {
        asm volatile("hlt");  // Save CPU while waiting
    }
}

// Timer interrupt handler
static void timer_callback(registers_t* regs) {
    (void)regs;  // Unused
    timer_ticks++;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = process_table[i];
        if (!proc) {
            continue;
        }

        if (proc->state == PROCESS_STATE_BLOCKED &&
            proc->wakeup_tick != 0 &&
            (int32_t)(proc->wakeup_tick - timer_ticks) <= 0) {
            proc->wakeup_tick = 0;
            process_unblock(proc);
        }
    }

    // Ack the IRQ before scheduling so the PIC does not stall if we switch away.
    outb(0x20, 0x20);

    // Trigger scheduler tick
    scheduler_tick();
}

// Initialize the timer
void init_timer(uint32_t frequency) {
    timer_frequency = frequency;

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
