#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/terminal.h"
#include "../include/panic.h"
#include <stddef.h>

// Current kernel page directory (set during boot)
extern uint32_t* page_directory;  // From kernel.c

// Number of entries per page table/directory (32-bit uses 1024)
#define ENTRIES_PER_TABLE 1024

// Get or create a page table entry
static uint32_t* vmm_get_or_create_table(uint32_t* page_dir, size_t index, uint32_t flags) {
    uint32_t entry = page_dir[index];

    if (!(entry & PAGE_PRESENT)) {
        // Allocate new page table
        void* new_table = pmm_alloc_page();
        if (!new_table) {
            return NULL;
        }

        // Clear the new page table
        uint32_t* table = (uint32_t*)new_table;
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            table[i] = 0;
        }

        // Set entry with appropriate flags
        page_dir[index] = ((uint32_t)new_table & ~0xFFF) | flags | PAGE_PRESENT;
        return table;
    }

    // Return existing table
    return (uint32_t*)(entry & ~0xFFF);
}

// Create a new address space for a process
uint32_t* vmm_create_address_space(void) {
    // Allocate a new page directory
    uint32_t* new_pd = (uint32_t*)pmm_alloc_page();
    if (!new_pd) {
        return NULL;
    }

    // Clear the new page directory
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        new_pd[i] = 0;
    }

    // Copy kernel mappings (upper quarter of address space, 768-1023)
    // Kernel space is at 0xC0000000 and above in higher-half design
    // For now, copy the identity-mapped first 4MB for kernel access
    if (page_directory) {
        for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
            if (page_directory[i] & PAGE_PRESENT) {
                new_pd[i] = page_directory[i];
            }
        }
    }

    return new_pd;
}

// Free a page table
static void free_pt(uint32_t pt_phys) {
    uint32_t* pt = (uint32_t*)pt_phys;

    // Free all mapped pages (only user pages, not kernel)
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (pt[i] & PAGE_PRESENT) {
            uint32_t page_phys = pt[i] & ~0xFFF;
            // Only free user pages (PAGE_USER flag set)
            if (pt[i] & PAGE_USER) {
                pmm_free_page((void*)page_phys);
            }
        }
    }

    // Free the page table itself
    pmm_free_page((void*)pt_phys);
}

// Destroy an address space
void vmm_destroy_address_space(uint32_t* pd_to_destroy) {
    if (!pd_to_destroy || pd_to_destroy == page_directory) {
        return;  // Don't destroy kernel page directory
    }

    // Free user space page tables (entries 0-767, below 0xC0000000)
    // Entries 768+ are kernel space, shared across processes
    for (int i = 0; i < 768; i++) {
        if (pd_to_destroy[i] & PAGE_PRESENT) {
            // Only free if it's a user-mode page table
            if (pd_to_destroy[i] & PAGE_USER) {
                free_pt(pd_to_destroy[i] & ~0xFFF);
            }
        }
    }

    // Free the page directory itself
    pmm_free_page(pd_to_destroy);
}

// Map a page in a specific address space
int vmm_map_page(uint32_t* pd, uint32_t virt, uint32_t phys, uint32_t flags) {
    // Ensure page-aligned addresses
    virt &= ~0xFFF;
    phys &= ~0xFFF;

    // Get indices
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);

    // Determine flags for page table entry in page directory
    uint32_t pd_flags = PAGE_WRITABLE;
    if (virt < KERNEL_BASE) {
        pd_flags |= PAGE_USER;  // User space pages
    }

    // Get or create page table
    uint32_t* pt = vmm_get_or_create_table(pd, pd_idx, pd_flags);
    if (!pt) return -1;

    // Map the page
    pt[pt_idx] = (phys & ~0xFFF) | flags | PAGE_PRESENT;

    // Flush TLB for this address
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");

    return 0;
}

// Unmap a page
void vmm_unmap_page(uint32_t* pd, uint32_t virt) {
    virt &= ~0xFFF;

    // Get indices
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);

    // Check if page directory entry exists
    if (!(pd[pd_idx] & PAGE_PRESENT)) return;

    // Get page table
    uint32_t* pt = (uint32_t*)(pd[pd_idx] & ~0xFFF);

    // Clear the page table entry
    pt[pt_idx] = 0;

    // Flush TLB
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// Get physical address from virtual
uint32_t vmm_get_physical(uint32_t* pd, uint32_t virt) {
    // Get indices
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);

    // Check page directory
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    uint32_t* pt = (uint32_t*)(pd[pd_idx] & ~0xFFF);

    // Check page table
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;

    // Return physical address plus offset
    return (pt[pt_idx] & ~0xFFF) | (virt & 0xFFF);
}

// Switch to a different address space
void vmm_switch_address_space(uint32_t* new_pd) {
    asm volatile("mov %0, %%cr3" : : "r"(new_pd) : "memory");
}

// Allocate user pages for a process
int vmm_alloc_user_pages(process_t* process, uint32_t virt_addr, size_t count) {
    // Ensure user space address
    if (virt_addr >= KERNEL_BASE) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        void* phys_page = pmm_alloc_page();
        if (!phys_page) {
            return -1;
        }

        uint32_t virt = virt_addr + (i * PAGE_SIZE);
        uint32_t phys = (uint32_t)phys_page;

        if (vmm_map_page(process->page_directory, virt, phys,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) < 0) {
            pmm_free_page(phys_page);
            return -1;
        }

        process->pages_allocated++;
    }

    return 0;
}

// Set up user stack for a process
int vmm_setup_user_stack(process_t* process) {
    process->stack_top = USER_STACK_TOP;
    process->stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    // Allocate pages for stack
    size_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    return vmm_alloc_user_pages(process, process->stack_bottom, stack_pages);
}

// Set up user heap for a process
int vmm_setup_user_heap(process_t* process) {
    process->heap_start = USER_HEAP_START;
    process->heap_current = USER_HEAP_START;
    process->heap_max = USER_HEAP_START + 0x10000000;  // 256MB heap limit

    // Don't pre-allocate heap pages, they'll be allocated on demand via sbrk
    return 0;
}

// Helper: clone a single page
static uint32_t clone_page(uint32_t parent_page_phys, uint32_t flags) {
    // Allocate new page
    void* child_page = pmm_alloc_page();
    if (!child_page) return 0;

    // Copy contents
    uint8_t* src = (uint8_t*)parent_page_phys;
    uint8_t* dst = (uint8_t*)child_page;

    for (int i = 0; i < PAGE_SIZE; i++) {
        dst[i] = src[i];
    }

    return ((uint32_t)child_page) | flags;
}

// Helper: clone a page table
static uint32_t clone_pt(uint32_t parent_pt_phys) {
    uint32_t* parent_pt = (uint32_t*)parent_pt_phys;
    uint32_t* child_pt = (uint32_t*)pmm_alloc_page();

    if (!child_pt) return 0;

    // Clear child page table
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        child_pt[i] = 0;
    }

    // Clone each present page
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (parent_pt[i] & PAGE_PRESENT) {
            uint32_t page_phys = parent_pt[i] & ~0xFFF;
            uint32_t flags = parent_pt[i] & 0xFFF;
            child_pt[i] = clone_page(page_phys, flags);

            if (!child_pt[i]) {
                // Cleanup on failure
                pmm_free_page(child_pt);
                return 0;
            }
        }
    }

    return (uint32_t)child_pt;
}

// Clone an entire address space (for fork)
uint32_t* vmm_clone_address_space(uint32_t* parent_pd) {
    // Allocate new page directory
    uint32_t* child_pd = (uint32_t*)pmm_alloc_page();
    if (!child_pd) return NULL;

    // Clear the new page directory
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        child_pd[i] = 0;
    }

    // Copy kernel mappings (entries 768-1023, 0xC0000000+)
    for (int i = 768; i < ENTRIES_PER_TABLE; i++) {
        child_pd[i] = parent_pd[i];
    }

    // Clone user mappings (entries 0-767, below 0xC0000000)
    for (int i = 0; i < 768; i++) {
        if (parent_pd[i] & PAGE_PRESENT) {
            uint32_t child_pt = clone_pt(parent_pd[i] & ~0xFFF);
            if (!child_pt) {
                // Cleanup on failure
                vmm_destroy_address_space(child_pd);
                return NULL;
            }
            child_pd[i] = child_pt | (parent_pd[i] & 0xFFF);
        }
    }

    return child_pd;
}

// Clear user space mappings (for exec)
void vmm_clear_user_space(uint32_t* pd) {
    // Clear user space entries (0-767), recursively freeing all pages
    for (int i = 0; i < 768; i++) {
        if (pd[i] & PAGE_PRESENT) {
            free_pt(pd[i] & ~0xFFF);
            pd[i] = 0;
        }
    }

    // Flush TLB
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");
}
