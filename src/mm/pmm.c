#include <stdbool.h>
#include <stdint.h>
#include "mm/pmm.h"
#include "drivers/terminal.h"
#include "kernel/panic.h"

// Simple bitmap-based physical memory manager

// Start managing memory after 4MB (kernel and initial structures)
#define PMM_START 0x400000
#define BITMAP_SIZE (8 * 1024)  // Support up to 256MB of RAM (8KB bitmap, more reasonable for v86)
#define MAX_PAGES (BITMAP_SIZE * 8)  // Total pages the bitmap can track

static uint32_t pmm_bitmap[BITMAP_SIZE / 4];  // Bitmap of free pages
static uint8_t pmm_refcount[MAX_PAGES];        // Per-page reference counts
static size_t pmm_total_pages = 0;
static size_t pmm_free_page_count = 0;
static size_t pmm_reserved_pages = 0;
static size_t pmm_next_hint = 0;  // Next-free search hint

// Find first free page in bitmap, starting from hint
static int bitmap_find_free(void) {
    size_t words = BITMAP_SIZE / 4;

    // Search from hint to end
    for (size_t i = pmm_next_hint; i < words; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (int bit = 0; bit < 32; bit++) {
                if (!(pmm_bitmap[i] & (1 << bit))) {
                    pmm_next_hint = i;  // Update hint
                    return i * 32 + bit;
                }
            }
        }
    }

    // Wrap around: search from start to hint
    for (size_t i = 0; i < pmm_next_hint; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (int bit = 0; bit < 32; bit++) {
                if (!(pmm_bitmap[i] & (1 << bit))) {
                    pmm_next_hint = i;
                    return i * 32 + bit;
                }
            }
        }
    }

    return -1;  // No free pages
}

// Mark page as used
static void bitmap_set(size_t page) {
    size_t idx = page / 32;
    size_t bit = page % 32;
    pmm_bitmap[idx] |= (1 << bit);
}

// Mark page as free
static void bitmap_clear(size_t page) {
    size_t idx = page / 32;
    size_t bit = page % 32;
    pmm_bitmap[idx] &= ~(1 << bit);
}

// Test if page is used
static bool bitmap_test(size_t page) {
    size_t idx = page / 32;
    size_t bit = page % 32;
    return pmm_bitmap[idx] & (1 << bit);
}

// Initialize physical memory manager
void pmm_init(uint32_t memory_size) {
    // Calculate total pages
    pmm_total_pages = memory_size / PAGE_SIZE;

    // Initially mark all pages as used, refcount 0
    for (size_t i = 0; i < BITMAP_SIZE / 4; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
    for (size_t i = 0; i < MAX_PAGES; i++) {
        pmm_refcount[i] = 0;
    }

    // Mark available pages as free (after kernel)
    size_t first_free_page = PMM_START / PAGE_SIZE;
    size_t last_page = pmm_total_pages;

    if (last_page > MAX_PAGES) {
        last_page = MAX_PAGES;  // Limit to bitmap size
    }

    pmm_free_page_count = 0;
    for (size_t page = first_free_page; page < last_page; page++) {
        bitmap_clear(page);
        pmm_free_page_count++;
    }

    pmm_reserved_pages = first_free_page;  // Pages before PMM_START
    pmm_next_hint = first_free_page / 32;  // Start hint at first free region

    terminal_writestring("PMM initialized: ");
    terminal_print_uint((pmm_total_pages * PAGE_SIZE) / (1024 * 1024));
    terminal_writestring(" MB total, ");
    terminal_print_uint((pmm_free_page_count * PAGE_SIZE) / (1024 * 1024));
    terminal_writestring(" MB free\n");
}

// Internal: allocate a page without zeroing
static void* pmm_alloc_page_internal(void) {
    int page = bitmap_find_free();
    if (page == -1) {
        return NULL;
    }

    bitmap_set(page);
    pmm_free_page_count--;
    pmm_refcount[page] = 1;

    return (void*)((uint32_t)page * PAGE_SIZE);
}

// Allocate a single physical page (zeroed)
void* pmm_alloc_page(void) {
    void* addr = pmm_alloc_page_internal();
    if (!addr) return NULL;

    // Clear the page
    uint32_t* ptr = (uint32_t*)addr;
    for (int i = 0; i < PAGE_SIZE / 4; i++) {
        ptr[i] = 0;
    }

    return addr;
}

// Allocate a single physical page (not zeroed)
void* pmm_alloc_page_raw(void) {
    return pmm_alloc_page_internal();
}

// Increment reference count for a page
void pmm_ref_page(void* page_addr) {
    uint32_t addr = (uint32_t)page_addr;
    size_t page = addr / PAGE_SIZE;

    if (page >= pmm_total_pages) return;
    if (pmm_refcount[page] < 255) {
        pmm_refcount[page]++;
    }
}

// Decrement reference count; free page when it reaches 0
void pmm_unref_page(void* page_addr) {
    uint32_t addr = (uint32_t)page_addr;

    if (addr % PAGE_SIZE != 0 || addr < PMM_START) return;

    size_t page = addr / PAGE_SIZE;
    if (page >= pmm_total_pages) return;

    if (pmm_refcount[page] == 0) return;

    pmm_refcount[page]--;
    if (pmm_refcount[page] == 0) {
        bitmap_clear(page);
        pmm_free_page_count++;

        // Update hint if this is before current hint
        size_t word = page / 32;
        if (word < pmm_next_hint) {
            pmm_next_hint = word;
        }
    }
}

// Get reference count for a page
uint8_t pmm_get_refcount(void* page_addr) {
    uint32_t addr = (uint32_t)page_addr;
    size_t page = addr / PAGE_SIZE;

    if (page >= pmm_total_pages) return 0;
    return pmm_refcount[page];
}

// Free a physical page
void pmm_free_page(void* page_addr) {
    uint32_t addr = (uint32_t)page_addr;

    // Validate address
    if (addr % PAGE_SIZE != 0 || addr < PMM_START) {
        panic("pmm_free_page: Invalid page address");
        return;
    }

    size_t page = addr / PAGE_SIZE;
    if (page >= pmm_total_pages) {
        panic("pmm_free_page: Page out of range");
        return;
    }

    if (!bitmap_test(page)) {
        panic("pmm_free_page: Double free detected");
        return;
    }

    // If refcounted, use unref instead
    if (pmm_refcount[page] > 0) {
        pmm_unref_page(page_addr);
        return;
    }

    bitmap_clear(page);
    pmm_free_page_count++;

    // Update hint
    size_t word = page / 32;
    if (word < pmm_next_hint) {
        pmm_next_hint = word;
    }
}

// Allocate multiple contiguous pages
void* pmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;

    // Find contiguous free pages, skipping ahead on failure
    for (size_t start = PMM_START / PAGE_SIZE; start + count <= pmm_total_pages; ) {
        bool found = true;

        // Check if all pages are free
        for (size_t i = 0; i < count; i++) {
            if (bitmap_test(start + i)) {
                // Skip past the occupied page
                start = start + i + 1;
                found = false;
                break;
            }
        }

        if (found) {
            // Allocate all pages
            for (size_t i = 0; i < count; i++) {
                bitmap_set(start + i);
                pmm_refcount[start + i] = 1;
            }
            pmm_free_page_count -= count;

            // Clear the pages
            uint32_t addr = start * PAGE_SIZE;
            uint32_t* ptr = (uint32_t*)addr;
            for (size_t i = 0; i < count * PAGE_SIZE / 4; i++) {
                ptr[i] = 0;
            }

            return (void*)addr;
        }
    }

    return NULL;  // No contiguous block found
}

// Free multiple contiguous pages
void pmm_free_pages(void* page_addr, size_t count) {
    if (count == 0) return;

    uint32_t addr = (uint32_t)page_addr;

    for (size_t i = 0; i < count; i++) {
        pmm_free_page((void*)(addr + i * PAGE_SIZE));
    }
}

// Get memory statistics
void pmm_get_stats(size_t* total_pages, size_t* free_pages, size_t* used_pages) {
    if (total_pages) *total_pages = pmm_total_pages;
    if (free_pages) *free_pages = pmm_free_page_count;
    if (used_pages) *used_pages = pmm_total_pages - pmm_free_page_count;
}
