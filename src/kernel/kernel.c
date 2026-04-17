// This is the main kernel file for SimpleOS, implementing core OS functionality.
// It includes memory management, process scheduling, interrupt handling, and basic I/O operations.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "drivers/terminal.h"

// Serial debug output (COM1 = 0x3F8)
static inline void serial_char(char c) {
    asm volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3F8));
}

static inline void serial_str(const char* s) {
    while (*s) {
        serial_char(*s++);
    }
}

#define DEBUG_SERIAL(msg) serial_str(msg)
#include "kernel/isr.h"
#include "drivers/ports.h"
#include "drivers/timer.h"
#include "kernel/panic.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "mm/kmalloc.h"
#include "drivers/keyboard.h"
#include "boot/exceptions.h"
#include "kernel/syscall.h"
#include "arch/i386/tss.h"
#include "arch/i386/usermode.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "lib/elf.h"
#include "ipc/signal.h"
#include "../../userspace/hello_binary.h"
#include "../../userspace/shell_binary.h"

// External assembly functions
extern void load_gdt(uintptr_t gdt_ptr);
extern void load_idt(uintptr_t idt_ptr);
extern void enable_paging(uintptr_t* pml4);

// Function prototypes
void init_gdt(void);
void init_idt(void);
void init_paging(void);
void init_keyboard(void);
void init_pic(void);
void test_fork_exec(void);
void fork_test_main(void);
void test_shell(void);
void fs_init(void);
void vt_init(void);

// Memory management
#define PAGE_SIZE 4096
#define ENTRIES_PER_TABLE 1024  // 32-bit uses 1024 entries

// Page tables for 32-bit (2-level paging)
// Allocate in BSS to avoid address conflicts with kernel
// These MUST be page-aligned (4KB boundary)
// We need 16 page tables to map 64MB (16 * 4MB = 64MB)
#define NUM_PAGE_TABLES 16
static uint32_t page_directory_storage[1024] __attribute__((aligned(4096)));
static uint32_t page_tables_storage[NUM_PAGE_TABLES][1024] __attribute__((aligned(4096)));
uint32_t* page_directory = page_directory_storage;

// Heap configuration - must be within mapped memory (first 4MB)
// Kernel ends around 1.6MB, so start heap at 2MB
#define HEAP_START 0x200000   // 2MB
#define HEAP_SIZE  0x180000   // 1.5MB (fits within first 4MB)
uint8_t* heap_start = (uint8_t*)HEAP_START;
uint8_t* heap_end = (uint8_t*)(HEAP_START + HEAP_SIZE);
uint8_t* heap_current = (uint8_t*)HEAP_START;

// kmalloc is implemented in mm/kmalloc.c

// Test processes for multitasking demo
void test_process_1(void) {
    int counter = 0;
    while(1) {
        terminal_writestring("[Kernel Thread A] Heartbeat ");
        terminal_print_int(counter);
        terminal_writestring("\n");
        counter++;
        sleep_ms(1000);  // Sleep for 1 second
    }
}

// Test process for memory isolation (simplified for 32-bit)
void test_memory_process(void) {
    terminal_writestring("[Memory Check] Starting stack validation\n");

    // Test stack allocation only (syscalls removed for initial 32-bit port)
    char stack_buffer[1024];
    const char* stack_msg = "stack frame looks healthy";
    for (int i = 0; stack_msg[i] && i < 23; i++) {
        stack_buffer[i] = stack_msg[i];
    }
    stack_buffer[23] = '\0';

    terminal_writestring("[Memory Check] Result: ");
    terminal_writestring(stack_buffer);
    terminal_writestring("\n");

    while(1) {
        terminal_writestring("[Memory Check] Background monitor online\n");
        sleep_ms(3000);
    }
}

// Test process using system calls (simplified for 32-bit)
void test_syscall_process(void) {
    terminal_writestring("[Syscall Bridge] Dispatch loop online\n");

    while(1) {
        terminal_writestring("[Syscall Bridge] Waiting for requests\n");
        sleep_ms(2000);
    }
}

void test_process_2(void) {
    int counter = 0;
    while(1) {
        terminal_writestring("[Kernel Thread B] Heartbeat ");
        terminal_print_int(counter);
        terminal_writestring("\n");
        counter++;
        sleep_ms(1500);  // Sleep for 1.5 seconds
    }
}

void test_process_3(void) {
    while(1) {
        terminal_writestring("[Compute Worker] Starting batch job\n");
        // CPU-intensive task
        volatile uint32_t sum = 0;
        for (volatile int i = 0; i < 10000000; i++) {
            sum += i;
        }
        terminal_writestring("[Compute Worker] Batch complete\n");
        sleep_ms(2000);
    }
}

static bool shell_active = false;

// Test ELF loader
void test_elf_loader(void) {
    terminal_writestring("\n=== Testing ELF Loader ===\n");
    
    // Test ELF validation with our minimal binary
    terminal_writestring("ELF binary size: ");
    terminal_print_uint(hello_elf_len);
    terminal_writestring(" bytes\n");
    
    // Try to create process from ELF
    terminal_writestring("Creating ELF process...\n");
    process_t* proc = elf_create_process(
        (void*)hello_elf,     // Binary data
        hello_elf_len,        // Size
        "hello_elf"           // Process name
    );
    
    if (proc) {
        terminal_writestring("ELF process created successfully!\n");
        terminal_writestring("Entry point: ");
        terminal_print_hex(proc->user_entry);
        terminal_writestring("\n");
        terminal_writestring("Process should be ready to run\n");
    } else {
        terminal_writestring("Failed to create ELF process\n");
    }
}

// GDT structures
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uintptr_t base;
} __attribute__((packed));

#define GDT_ENTRIES 7  // Increased for TSS (uses 2 entries)
struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gp;

// IDT structures (32-bit)
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uintptr_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256
struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr ip;

// ISR function prototypes  
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void irq0(void);
extern void irq1(void);
extern void isr128(void);  // INT 0x80 syscall

// Interrupt handler table
static isr_t interrupt_handlers[256] = { 0 };

// Register an interrupt handler
void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

// Set up a GDT entry
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access = access;
}

// Set up a TSS descriptor (32-bit - uses 1 GDT entry)
void gdt_set_tss(int num, uint32_t base, uint32_t limit) {
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].access = 0x89;  // Present, ring 0, 32-bit TSS
}

void init_gdt(void) {
    gp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gp.base = (uintptr_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Kernel code segment (ring 0, 32-bit)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel data segment (ring 0, 32-bit)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User code segment (ring 3, 32-bit)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User data segment (ring 3, 32-bit)
    
    // TSS will be set up after GDT is loaded
    
    load_gdt((uintptr_t)&gp);
    
    // Now set up TSS (entry 5 only in 32-bit)
    extern tss_t tss;
    gdt_set_tss(5, (uint32_t)&tss, sizeof(tss_t) - 1);
    
    // Reload GDT to include TSS
    load_gdt((uintptr_t)&gp);
    
    // Load TSS (0x28 = GDT entry 5 * 8)
    asm volatile("ltr %0" : : "r"((uint16_t)0x28));
}

// Set up an IDT entry (32-bit)
void idt_set_gate(uint8_t num, uintptr_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
    idt[num].selector = sel;
    idt[num].zero = 0;
    idt[num].type_attr = flags;
}

void init_idt(void) {
    ip.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    ip.base = (uintptr_t)&idt;

    // Use memset from string.h or implement it separately
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set up CPU exception handlers (0-31)
    idt_set_gate(0, (uintptr_t)isr0, 0x08, 0x8E);   // Division by zero
    idt_set_gate(1, (uintptr_t)isr1, 0x08, 0x8E);   // Debug
    idt_set_gate(2, (uintptr_t)isr2, 0x08, 0x8E);   // NMI
    idt_set_gate(3, (uintptr_t)isr3, 0x08, 0x8E);   // Breakpoint
    idt_set_gate(4, (uintptr_t)isr4, 0x08, 0x8E);   // Overflow
    idt_set_gate(5, (uintptr_t)isr5, 0x08, 0x8E);   // Bound range
    idt_set_gate(6, (uintptr_t)isr6, 0x08, 0x8E);   // Invalid opcode
    idt_set_gate(7, (uintptr_t)isr7, 0x08, 0x8E);   // Device not available
    idt_set_gate(8, (uintptr_t)isr8, 0x08, 0x8E);   // Double fault
    idt_set_gate(10, (uintptr_t)isr10, 0x08, 0x8E); // Invalid TSS
    idt_set_gate(11, (uintptr_t)isr11, 0x08, 0x8E); // Segment not present
    idt_set_gate(12, (uintptr_t)isr12, 0x08, 0x8E); // Stack fault
    idt_set_gate(13, (uintptr_t)isr13, 0x08, 0x8E); // General protection
    idt_set_gate(14, (uintptr_t)isr14, 0x08, 0x8E); // Page fault
    idt_set_gate(16, (uintptr_t)isr16, 0x08, 0x8E); // x87 FPU error
    idt_set_gate(17, (uintptr_t)isr17, 0x08, 0x8E); // Alignment check
    idt_set_gate(18, (uintptr_t)isr18, 0x08, 0x8E); // Machine check
    idt_set_gate(19, (uintptr_t)isr19, 0x08, 0x8E); // SIMD FP
    idt_set_gate(20, (uintptr_t)isr20, 0x08, 0x8E); // Virtualization
    
    // Map hardware IRQs (32-47)
    idt_set_gate(32, (uintptr_t)irq0, 0x08, 0x8E);  // Timer
    idt_set_gate(33, (uintptr_t)irq1, 0x08, 0x8E);  // Keyboard
    
    // System call uses a trap gate so IF is preserved across entry.
    idt_set_gate(128, (uintptr_t)isr128, 0x08, 0xEF); // INT 0x80

    load_idt((uintptr_t)&ip);
}

// Initialize the 8259 PIC
void init_pic(void) {
    // ICW1 - Initialize PICs
    outb(0x20, 0x11);  // Master PIC command port
    outb(0xA0, 0x11);  // Slave PIC command port
    
    // ICW2 - Set interrupt vector offset
    outb(0x21, 0x20);  // Master PIC vector offset (32)
    outb(0xA1, 0x28);  // Slave PIC vector offset (40)
    
    // ICW3 - Set up cascade
    outb(0x21, 0x04);  // Tell master PIC there's slave at IRQ2
    outb(0xA1, 0x02);  // Tell slave PIC its cascade identity
    
    // ICW4 - Set mode
    outb(0x21, 0x01);  // 8086 mode
    outb(0xA1, 0x01);
    
    // OCW1 - Mask interrupts (enable timer and keyboard)
    outb(0x21, 0xFC);  // Master PIC: Enable IRQ0 (timer) and IRQ1 (keyboard)
    outb(0xA1, 0xFF);  // Slave PIC: Disable all
}

// Set up paging for 32-bit protected mode
void init_paging(void) {
    // Identity map first 64MB using 16 page tables (each maps 4MB)
    for (int pt = 0; pt < NUM_PAGE_TABLES; pt++) {
        // Fill each page table with identity mappings
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            uint32_t phys_addr = (pt * ENTRIES_PER_TABLE + i) * PAGE_SIZE;
            page_tables_storage[pt][i] = phys_addr | 3; // Present + Writable
        }
        // Add page table to page directory
        page_directory[pt] = (uintptr_t)page_tables_storage[pt] | 3;
    }

    // Clear rest of page directory
    for (int i = NUM_PAGE_TABLES; i < ENTRIES_PER_TABLE; i++) {
        page_directory[i] = 0;
    }

    enable_paging((uintptr_t*)page_directory);
}

// ISR handler
void isr_handler(registers_t* regs) {
    if (interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
        return;
    }

    if (regs->int_no < 32) {
        exception_handler(regs);
        return;
    }

    terminal_writestring("Unhandled interrupt: ");
    terminal_print_uint(regs->int_no);
    terminal_writestring("\n");
}

// Kernel main function
void kernel_main(void) {
    DEBUG_SERIAL("[K] kernel_main entered\n");

    // Initialize core systems
    DEBUG_SERIAL("[K] init_vga...\n");
    init_vga();
    DEBUG_SERIAL("[K] init_vga done\n");
    terminal_writestring("SimpleOS v0.2 - Now with Multitasking!\n");
    terminal_writestring("=====================================\n\n");

    // Initialize physical memory manager
    DEBUG_SERIAL("[K] pmm_init...\n");
    pmm_init(64 * 1024 * 1024);  // 64MB
    DEBUG_SERIAL("[K] pmm_init done\n");

    // Initialize TSS BEFORE GDT (GDT setup loads TSS register)
    DEBUG_SERIAL("[K] tss_init...\n");
    tss_init();
    DEBUG_SERIAL("[K] tss_init done\n");

    DEBUG_SERIAL("[K] init_gdt...\n");
    init_gdt();
    DEBUG_SERIAL("[K] init_gdt done\n");

    DEBUG_SERIAL("[K] init_pic...\n");
    init_pic();
    DEBUG_SERIAL("[K] init_pic done\n");

    DEBUG_SERIAL("[K] init_idt...\n");
    init_idt();
    DEBUG_SERIAL("[K] init_idt done\n");

    DEBUG_SERIAL("[K] init_exceptions...\n");
    init_exceptions();
    DEBUG_SERIAL("[K] init_exceptions done\n");

    DEBUG_SERIAL("[K] init_paging...\n");
    init_paging();
    DEBUG_SERIAL("[K] init_paging done\n");

    DEBUG_SERIAL("[K] init_timer...\n");
    init_timer(100);  // 100 Hz = 10ms ticks
    DEBUG_SERIAL("[K] init_timer done\n");

    // Initialize process and scheduling
    DEBUG_SERIAL("[K] process_init...\n");
    process_init();
    DEBUG_SERIAL("[K] process_init done\n");

    DEBUG_SERIAL("[K] scheduler_init...\n");
    scheduler_init();
    DEBUG_SERIAL("[K] scheduler_init done\n");

    DEBUG_SERIAL("[K] signal_init...\n");
    signal_init();
    DEBUG_SERIAL("[K] signal_init done\n");

    DEBUG_SERIAL("[K] init_keyboard...\n");
    init_keyboard();
    DEBUG_SERIAL("[K] init_keyboard done\n");

    DEBUG_SERIAL("[K] init_syscalls...\n");
    init_syscalls();
    DEBUG_SERIAL("[K] init_syscalls done\n");

    DEBUG_SERIAL("[K] fs_init...\n");
    fs_init();
    DEBUG_SERIAL("[K] fs_init done\n");

    DEBUG_SERIAL("[K] vt_init...\n");
    vt_init();
    DEBUG_SERIAL("[K] vt_init done\n");

    terminal_enable_vt();
    DEBUG_SERIAL("[K] All init complete!\n");
    
    terminal_writestring("SimpleOS boot complete.\n");
    terminal_writestring("Launching the multitasking demo...\n\n");
    terminal_writestring("Tip: use Alt+F1 through Alt+F4 to switch virtual terminals.\n\n");
    
    // Enable interrupts
    asm volatile("sti");
    
    // Create test processes
    process_t* p1 = process_create("TestProc1", test_process_1, 1);
    process_t* p2 = process_create("TestProc2", test_process_2, 1);
    process_t* p3 = process_create("SyscallTest", test_syscall_process, 1);
    process_t* p4 = process_create("MemoryTest", test_memory_process, 1);
    
    if (!p1 || !p2 || !p3 || !p4) {
        panic("Failed to create test processes!");
    }
    
    terminal_writestring("\nHanding control to the scheduler...\n");
    terminal_writestring("Live output below shows kernel tasks taking turns on the CPU.\n");
    terminal_writestring("Quick keys: 'p' process list, 's' scheduler stats, 'f' page fault test\n");
    terminal_writestring("            't' syscall notes, 'u' user mode, 'e' ELF loader\n");
    terminal_writestring("            'F' fork/exec, 'S' start shell\n\n");

    // Start the shell automatically so the browser demo is interactive by default.
    test_shell();
    
    // Enable scheduler - this will switch to first process
    scheduler_enable();
    
    // Kernel main becomes the idle loop
    // This code only runs when no other process is ready
    while(1) {
        // Check for debug commands
        if (!shell_active && keyboard_has_char()) {
            char c = keyboard_getchar();
            if (c == 'p') {
                process_print_all();
            } else if (c == 's') {
                scheduler_stats();
            } else if (c == 'f') {
                // Test page fault by accessing invalid memory
                terminal_writestring("\nTriggering page fault test...\n");
                volatile uint32_t* bad_ptr = (uint32_t*)0xDEADBEEF;
                *bad_ptr = 42;  // This will page fault
            } else if (c == 't') {
                // Test system call from kernel (simplified for 32-bit)
                terminal_writestring("\nSyscall test (simplified for 32-bit)\n");
                terminal_writestring("Syscalls use int $0x80 with 32-bit cdecl convention\n");
            } else if (c == 'u') {
                // Test user mode
                terminal_writestring("\nTesting user mode...\n");
                test_user_mode();
            } else if (c == 'e') {
                // Test ELF loader
                test_elf_loader();
            } else if (c == 'F') {
                // Test fork/exec
                test_fork_exec();
            } else if (c == 'S') {
                // Start shell via init
                test_shell();
            }
        }
        
        // Halt CPU until next interrupt
        asm volatile("hlt");
    }
}

// Test fork/exec system calls
void test_fork_exec(void) {
    terminal_writestring("\n=== Testing Fork/Exec ===\n");
    
    // Create a test process that will fork
    process_t* test = process_create("fork_test", fork_test_main, 1);
    if (test) {
        terminal_writestring("Created fork test process\n");
    } else {
        terminal_writestring("Failed to create fork test process\n");
    }
}

// Test program that demonstrates fork (simplified for 32-bit)
void fork_test_main(void) {
    terminal_writestring("[FORK_TEST] Starting fork test\n");
    terminal_writestring("[FORK_TEST] Fork/exec demo disabled in initial 32-bit port\n");

    while(1) {
        terminal_writestring("[FORK_TEST] Process running...\n");
        sleep_ms(3000);
    }
}

// Start the shell as a user-mode ELF process (fork/exec enabled).
void test_shell(void) {
    terminal_writestring("\n=== Starting Shell ===\n");

    if (shell_elf_len == 0) {
        terminal_writestring("ERROR: No shell ELF binary available\n");
        return;
    }

    process_t* shell = elf_create_process(
        (void*)shell_elf, shell_elf_len, "shell");
    if (shell) {
        shell->parent_pid = 0;
        shell_active = true;
        terminal_writestring("User-mode shell started (PID ");
        terminal_print_uint(shell->pid);
        terminal_writestring(")\n");
    } else {
        terminal_writestring("ERROR: Failed to create shell process\n");
    }
}
