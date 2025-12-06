#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

// Physical Memory Manager - manages physical page allocation

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

// Page aligned addresses
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

// Initialize physical memory manager
void pmm_init(uint32_t memory_size);

// Allocate a single physical page
void* pmm_alloc_page(void);

// Free a physical page
void pmm_free_page(void* page);

// Allocate multiple contiguous pages
void* pmm_alloc_pages(size_t count);

// Free multiple contiguous pages
void pmm_free_pages(void* page, size_t count);

// Get memory statistics
void pmm_get_stats(size_t* total_pages, size_t* free_pages, size_t* used_pages);

#endif // PMM_H