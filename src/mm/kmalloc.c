#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../include/panic.h"

// Memory block header
typedef struct block {
    size_t size;
    struct block* next;
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

// Initialize the heap
static void init_heap(void) {
    head = (block_t*)heap_start;
    head->size = HEAP_SIZE - BLOCK_HEADER_SIZE;
    head->next = NULL;
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
        
        // Update current block
        block->size = size;
        block->next = new_block;
    }
}

// Coalesce adjacent free blocks
static void coalesce_free_blocks(void) {
    block_t* current = head;
    
    while (current && current->next) {
        if (current->free && current->next->free) {
            // Merge with next block
            current->size += BLOCK_HEADER_SIZE + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
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
    
    // Coalesce adjacent free blocks
    coalesce_free_blocks();
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
    
    // If new size fits in current block, just return
    if (new_size <= old_size) {
        return ptr;
    }
    
    // Allocate new block
    void* new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    // Copy old data
    uint8_t* src = (uint8_t*)ptr;
    uint8_t* dst = (uint8_t*)new_ptr;
    for (size_t i = 0; i < old_size; i++) {
        dst[i] = src[i];
    }
    
    // Free old block
    kfree(ptr);
    
    return new_ptr;
}