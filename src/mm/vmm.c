#include "mm/vmm.h"
#include "mm/pmm.h"
#include "drivers/terminal.h"
#include "kernel/panic.h"
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

    // Share only the kernel's low identity-mapped window.
    if (page_directory) {
        for (int i = 0; i < KERNEL_SHARED_PDE_COUNT; i++) {
            if (page_directory[i] & PAGE_PRESENT) {
                new_pd[i] = page_directory[i];
            }
        }
    }

    return new_pd;
}

// Free a page table (uses unref for COW-aware freeing)
static void free_pt(uint32_t pt_phys) {
    uint32_t* pt = (uint32_t*)pt_phys;

    // Free all mapped pages (only user pages, not kernel)
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (pt[i] & PAGE_PRESENT) {
            uint32_t page_phys = pt[i] & ~0xFFF;
            // Only free user pages (PAGE_USER flag set)
            if (pt[i] & PAGE_USER) {
                pmm_unref_page((void*)page_phys);
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

    for (int i = PD_INDEX(USER_VADDR_MIN); i < PD_INDEX(KERNEL_BASE); i++) {
        if (pd_to_destroy[i] & PAGE_PRESENT) {
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

    if ((flags & PAGE_USER) && (virt < USER_VADDR_MIN || virt >= KERNEL_BASE)) {
        return -1;
    }

    uint32_t pd_flags = PAGE_WRITABLE;
    if (flags & PAGE_USER) {
        pd_flags |= PAGE_USER;
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

void vmm_unmap_page(uint32_t* pd, uint32_t virt) {
    virt &= ~0xFFF;

    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return;

    uint32_t* pt = (uint32_t*)(pd[pd_idx] & ~0xFFF);
    pt[pt_idx] = 0;

    // Flush TLB
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

uint32_t vmm_get_physical(uint32_t* pd, uint32_t virt) {
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    uint32_t* pt = (uint32_t*)(pd[pd_idx] & ~0xFFF);

    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;

    // Return physical address plus offset
    return (pt[pt_idx] & ~0xFFF) | (virt & 0xFFF);
}

uint32_t vmm_get_page_flags(uint32_t* pd, uint32_t virt) {
    size_t pd_idx = PD_INDEX(virt);
    size_t pt_idx = PT_INDEX(virt);

    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        return 0;
    }

    uint32_t* pt = (uint32_t*)(pd[pd_idx] & ~0xFFF);
    if (!(pt[pt_idx] & PAGE_PRESENT)) {
        return 0;
    }

    return pt[pt_idx] & 0xFFF;
}

// Switch to a different address space
void vmm_switch_address_space(uint32_t* new_pd) {
    asm volatile("mov %0, %%cr3" : : "r"(new_pd) : "memory");
}

// Allocate user pages for a process
int vmm_alloc_user_pages(process_t* process, uint32_t virt_addr, size_t count) {
    if (virt_addr < USER_VADDR_MIN || virt_addr >= KERNEL_BASE) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        uint32_t virt = virt_addr + (i * PAGE_SIZE);
        if (virt < USER_VADDR_MIN || virt >= KERNEL_BASE) {
            return -1;
        }

        void* phys_page = pmm_alloc_page();
        if (!phys_page) {
            return -1;
        }
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

// COW-aware page table cloning: share pages read-only with COW flag
static uint32_t cow_clone_pt(uint32_t* parent_pt, uint32_t* child_pt) {
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (parent_pt[i] & PAGE_PRESENT) {
            uint32_t page_phys = parent_pt[i] & ~0xFFF;
            uint32_t flags = parent_pt[i] & 0xFFF;

            if (flags & PAGE_USER) {
                // Mark as COW: remove writable, set COW bit
                uint32_t cow_flags = (flags & ~PAGE_WRITABLE) | PAGE_COW;

                // Update parent PTE to also be read-only + COW
                parent_pt[i] = page_phys | cow_flags | PAGE_PRESENT;

                // Child shares the same physical page
                child_pt[i] = page_phys | cow_flags | PAGE_PRESENT;

                // Bump reference count
                pmm_ref_page((void*)page_phys);
            } else {
                // Kernel page — share directly
                child_pt[i] = parent_pt[i];
            }
        } else {
            child_pt[i] = 0;
        }
    }
    return 0;
}

// Clone an entire address space (for fork) using COW
uint32_t* vmm_clone_address_space(uint32_t* parent_pd) {
    // Allocate new page directory
    uint32_t* child_pd = (uint32_t*)pmm_alloc_page();
    if (!child_pd) return NULL;

    // Clear the new page directory
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        child_pd[i] = 0;
    }

    // Share kernel identity mappings
    for (int i = 0; i < KERNEL_SHARED_PDE_COUNT; i++) {
        child_pd[i] = parent_pd[i];
    }

    // COW-clone user space page tables
    for (int i = PD_INDEX(USER_VADDR_MIN); i < PD_INDEX(KERNEL_BASE); i++) {
        if (parent_pd[i] & PAGE_PRESENT) {
            uint32_t* parent_pt = (uint32_t*)(parent_pd[i] & ~0xFFF);

            // Allocate a new page table for the child
            uint32_t* child_pt = (uint32_t*)pmm_alloc_page();
            if (!child_pt) {
                vmm_destroy_address_space(child_pd);
                return NULL;
            }

            // COW-clone entries
            cow_clone_pt(parent_pt, child_pt);

            child_pd[i] = ((uint32_t)child_pt) | (parent_pd[i] & 0xFFF);
        }
    }

    // Flush parent's TLB since we changed its PTEs to read-only
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");

    return child_pd;
}

// Handle a COW page fault: copy the shared page and remap as writable
bool vmm_handle_cow_fault(uint32_t* pd, uint32_t faulting_address) {
    faulting_address &= ~0xFFF;  // Page-align

    size_t pd_idx = PD_INDEX(faulting_address);
    size_t pt_idx = PT_INDEX(faulting_address);

    // Must have a present page directory entry
    if (!(pd[pd_idx] & PAGE_PRESENT)) return false;

    uint32_t* pt = (uint32_t*)(pd[pd_idx] & ~0xFFF);

    // Must be a present page with COW flag
    if (!(pt[pt_idx] & PAGE_PRESENT)) return false;
    if (!(pt[pt_idx] & PAGE_COW)) return false;

    uint32_t old_phys = pt[pt_idx] & ~0xFFF;
    uint32_t flags = pt[pt_idx] & 0xFFF;

    // Check if we're the sole owner (refcount == 1)
    if (pmm_get_refcount((void*)old_phys) == 1) {
        // No need to copy — just make it writable and clear COW
        pt[pt_idx] = old_phys | ((flags | PAGE_WRITABLE) & ~PAGE_COW) | PAGE_PRESENT;
        asm volatile("invlpg (%0)" : : "r"(faulting_address) : "memory");
        return true;
    }

    // Allocate a new page (raw — we're about to overwrite it)
    void* new_page = pmm_alloc_page_raw();
    if (!new_page) {
        return false;  // OOM during COW — caller should kill process
    }

    // Copy contents (uint32_t-width for speed)
    uint32_t* src = (uint32_t*)old_phys;
    uint32_t* dst = (uint32_t*)new_page;
    for (int i = 0; i < PAGE_SIZE / 4; i++) {
        dst[i] = src[i];
    }

    // Drop reference to old shared page
    pmm_unref_page((void*)old_phys);

    // Remap with new page: writable, COW cleared
    uint32_t new_flags = (flags | PAGE_WRITABLE) & ~PAGE_COW;
    pt[pt_idx] = ((uint32_t)new_page) | new_flags | PAGE_PRESENT;

    // Flush TLB for this address
    asm volatile("invlpg (%0)" : : "r"(faulting_address) : "memory");

    return true;
}

// Clear user space mappings (for exec)
void vmm_clear_user_space(uint32_t* pd) {
    for (int i = PD_INDEX(USER_VADDR_MIN); i < PD_INDEX(KERNEL_BASE); i++) {
        if (pd[i] & PAGE_PRESENT) {
            free_pt(pd[i] & ~0xFFF);
            pd[i] = 0;
        }
    }

    // Flush TLB
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");
}
