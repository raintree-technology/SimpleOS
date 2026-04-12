#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/process.h"

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
#define PAGE_COW        (1 << 9)   // OS-defined: copy-on-write (AVL bit 0)

// Runtime layout in the current low-mapped kernel design.
// The kernel owns the first 64MB via shared identity mappings.
// User virtual memory must live above that shared kernel window.
#define KERNEL_IDENTITY_LIMIT 0x04000000  // 64MB
#define USER_VADDR_MIN    0x08000000      // 128MB
#define USER_CODE_START   0x08048000      // Standard ELF32 text base
#define USER_HEAP_START   0x09000000      // Keep heap above loaded image area
#define USER_STACK_TOP    0xBFFFF000      // Below 3GB kernel boundary
#define USER_STACK_SIZE   0x00100000      // 1MB stack
#define KERNEL_BASE       0xC0000000      // Upper boundary for user addresses
#define KERNEL_SHARED_PDE_COUNT (KERNEL_IDENTITY_LIMIT >> 22)

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
uint32_t vmm_get_page_flags(uint32_t* page_dir, uint32_t virt);

// Switch to a different address space
void vmm_switch_address_space(uint32_t* page_dir);

// Process-specific memory functions
int vmm_alloc_user_pages(process_t* process, uint32_t virt_addr, size_t count);
int vmm_setup_user_stack(process_t* process);
int vmm_setup_user_heap(process_t* process);

// Address space cloning for fork (uses COW)
uint32_t* vmm_clone_address_space(uint32_t* parent_page_dir);
void vmm_clear_user_space(uint32_t* page_dir);

// COW fault handler — returns true if the fault was handled
bool vmm_handle_cow_fault(uint32_t* pd, uint32_t faulting_address);

#endif // VMM_H
