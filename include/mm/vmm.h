#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include "process.h"

// Virtual Memory Manager - manages virtual address spaces
// 32-bit version using 2-level paging (Page Directory -> Page Table)

// Page flags (same for 32-bit)
#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITABLE   (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_WRITE_THROUGH (1 << 3)
#define PAGE_CACHE_DISABLE (1 << 4)
#define PAGE_ACCESSED   (1 << 5)
#define PAGE_DIRTY      (1 << 6)
#define PAGE_HUGE       (1 << 7)   // 4MB pages in 32-bit
#define PAGE_GLOBAL     (1 << 8)

// Standard user space memory layout (32-bit)
#define USER_STACK_TOP    0xBFFFF000  // Below 3GB kernel space
#define USER_STACK_SIZE   0x100000    // 1MB stack
#define USER_HEAP_START   0x400000    // After typical ELF load address
#define USER_CODE_START   0x100000    // Default code location
#define KERNEL_BASE       0xC0000000  // 3GB mark for kernel (higher half)

// Page table indices from virtual address (32-bit, 2-level)
#define PD_INDEX(addr)   (((addr) >> 22) & 0x3FF)   // Page directory index (10 bits)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x3FF)   // Page table index (10 bits)

// Create a new page directory for a process
uint32_t* vmm_create_address_space(void);

// Destroy a page directory
void vmm_destroy_address_space(uint32_t* page_dir);

// Map a page in a specific address space
int vmm_map_page(uint32_t* page_dir, uint32_t virt, uint32_t phys, uint32_t flags);

// Unmap a page
void vmm_unmap_page(uint32_t* page_dir, uint32_t virt);

// Get physical address from virtual
uint32_t vmm_get_physical(uint32_t* page_dir, uint32_t virt);

// Switch to a different address space
void vmm_switch_address_space(uint32_t* page_dir);

// Process-specific memory functions
int vmm_alloc_user_pages(process_t* process, uint32_t virt_addr, size_t count);
int vmm_setup_user_stack(process_t* process);
int vmm_setup_user_heap(process_t* process);

// Address space cloning for fork
uint32_t* vmm_clone_address_space(uint32_t* parent_page_dir);
void vmm_clear_user_space(uint32_t* page_dir);

#endif // VMM_H
