#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "kernel/panic.h"

// Memory block header (doubly-linked for O(1) coalesce)
typedef struct block {
    size_t size;
    struct block* next;
    struct block* prev;
    bool free;
} block_t;

// Heap configuration - must match kernel.c and be within mapped memory (first 4MB)
#define HEAP_START 0x200000   // 2MB
#define HEAP_SIZE  0x180000   // 1.5MB
#define BLOCK_HEADER_SIZE sizeof(block_t)
#define MIN_BLOCK_SIZE 16

// Heap state
static uint8_t* heap_start = (uint8_t*)HEAP_START;
static uint8_t* heap_end = (uint8_t*)(HEAP_START + HEAP_SIZE);
static block_t* head = NULL;
static bool heap_initialized = false;

// Statistics
static size_t total_allocated = 0;
static size_t total_free = HEAP_SIZE;
static size_t allocation_count = 0;

static void init_heap(void) {
    head = (block_t*)heap_start;
    head->size = HEAP_SIZE - BLOCK_HEADER_SIZE;
    head->next = NULL;
    head->prev = NULL;
    head->free = true;
    heap_initialized = true;
}

// Find a free block of at least the requested size
static block_t* find_free_block(size_t size) {
    block_t* current = head;

    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// Split a block if it's significantly larger than needed
static void split_block(block_t* block, size_t size) {
    // Only split if remaining space can hold a header + minimum block
    if (block->size >= size + BLOCK_HEADER_SIZE + MIN_BLOCK_SIZE) {
        // Create new block after the allocated portion
        block_t* new_block = (block_t*)((uint8_t*)block + BLOCK_HEADER_SIZE + size);
        new_block->size = block->size - size - BLOCK_HEADER_SIZE;
        new_block->free = true;
        new_block->next = block->next;
        new_block->prev = block;

        // Update successor's prev pointer
        if (block->next) {
            block->next->prev = new_block;
        }

        // Update current block
        block->size = size;
        block->next = new_block;
    }
}

// O(1) coalesce: merge a free block with its immediate neighbors
static void coalesce_neighbors(block_t* block) {
    // Merge with next block if free
    if (block->next && block->next->free) {
        block_t* next = block->next;
        total_free += BLOCK_HEADER_SIZE; // Reclaim header space
        block->size += BLOCK_HEADER_SIZE + next->size;
        block->next = next->next;
        if (next->next) {
            next->next->prev = block;
        }
    }

    // Merge with previous block if free
    if (block->prev && block->prev->free) {
        block_t* prev = block->prev;
        total_free += BLOCK_HEADER_SIZE; // Reclaim header space
        prev->size += BLOCK_HEADER_SIZE + block->size;
        prev->next = block->next;
        if (block->next) {
            block->next->prev = prev;
        }
    }
}

// Allocate memory
void* kmalloc(size_t size) {
    // Initialize heap on first allocation
    if (!heap_initialized) {
        init_heap();
    }

    // Align size to 8 bytes
    size = (size + 7) & ~7;

    // Find a free block
    block_t* block = find_free_block(size);
    if (!block) {
        panic("kmalloc: Out of memory!");
        return NULL;
    }

    // Split block if needed
    split_block(block, size);

    // Mark block as used
    block->free = false;

    // Update statistics
    total_allocated += block->size;
    total_free -= block->size;
    allocation_count++;

    // Return pointer to data (after header)
    return (uint8_t*)block + BLOCK_HEADER_SIZE;
}

// Free memory
void kfree(void* ptr) {
    if (!ptr) return;

    // Get block header
    block_t* block = (block_t*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);

    // Validate block
    if ((uint8_t*)block < heap_start || (uint8_t*)block >= heap_end) {
        panic("kfree: Invalid pointer!");
        return;
    }

    if (block->free) {
        panic("kfree: Double free detected!");
        return;
    }

    // Mark block as free
    block->free = true;

    // Update statistics
    total_allocated -= block->size;
    total_free += block->size;
    allocation_count--;

    // O(1) coalesce with immediate neighbors only
    coalesce_neighbors(block);
}

// Get heap statistics
void kmalloc_stats(size_t* allocated, size_t* free, size_t* count) {
    if (allocated) *allocated = total_allocated;
    if (free) *free = total_free;
    if (count) *count = allocation_count;
}

// Allocate and zero memory
void* kzalloc(size_t size) {
    void* ptr = kmalloc(size);
    if (ptr) {
        // Zero the allocated memory
        uint8_t* byte_ptr = (uint8_t*)ptr;
        for (size_t i = 0; i < size; i++) {
            byte_ptr[i] = 0;
        }
    }
    return ptr;
}

// Reallocate memory
void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }

    // Get old block
    block_t* block = (block_t*)((uint8_t*)ptr - BLOCK_HEADER_SIZE);
    size_t old_size = block->size;

    // Align new_size
    new_size = (new_size + 7) & ~7;

    // If new size fits in current block, just return
    if (new_size <= old_size) {
        return ptr;
    }

    // Try in-place expansion: absorb next block if it's free and large enough
    if (block->next && block->next->free) {
        size_t combined = old_size + BLOCK_HEADER_SIZE + block->next->size;
        if (combined >= new_size) {
            // Absorb the next block
            block_t* next = block->next;
            total_free -= next->size;
            total_allocated += (combined - old_size);

            block->size = combined;
            block->next = next->next;
            if (next->next) {
                next->next->prev = block;
            }

            // Split off excess if worthwhile
            split_block(block, new_size);
            if (block->size < combined) {
                // split_block created a new free block; fix stats
                total_allocated -= (combined - block->size);
                total_free += (combined - block->size - BLOCK_HEADER_SIZE);
            }

            return ptr;
        }
    }

    // Fallback: allocate new block and copy
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    // Copy old data (uint32_t-width)
    uint32_t* src = (uint32_t*)ptr;
    uint32_t* dst = (uint32_t*)new_ptr;
    size_t words = old_size / 4;
    for (size_t i = 0; i < words; i++) {
        dst[i] = src[i];
    }

    // Free old block
    kfree(ptr);

    return new_ptr;
}
