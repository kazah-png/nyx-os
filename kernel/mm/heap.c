#include "../core/kernel.h"

// Heap within the identity-mapped 64MB region
#define HEAP_SIZE (16 * 1024 * 1024)  // 16MB heap

typedef struct heap_block {
    size_t size;
    uint8_t used;
    struct heap_block* next;
} heap_block_t;

uint8_t heap[HEAP_SIZE] __attribute__((section(".bss.heap")));
static heap_block_t* free_list = (heap_block_t*)heap;

void init_heap(void) {
    free_list->size = sizeof(heap) - sizeof(heap_block_t);
    free_list->used = 0;
    free_list->next = NULL;
}

void* heap_alloc(size_t size) {
    heap_block_t* curr = free_list;
    while (curr) {
        // Coalesce this free block with any following free blocks before checking fit.
        // heap_free() only merges forward ONCE, so two blocks freed while a block between
        // them was still in use stay split — over time that fragments the heap and a large
        // request fails despite enough total free space. The list is address-ordered and each
        // block is physically contiguous with its next (see the split below), so absorbing
        // curr->next into curr is always valid; doing it here reclaims space freed in any order.
        if (!curr->used)
            while (curr->next && !curr->next->used) {
                curr->size += sizeof(heap_block_t) + curr->next->size;
                curr->next = curr->next->next;
            }
        if (!curr->used && curr->size >= size) {
            if (curr->size > size + sizeof(heap_block_t) + 16) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->used = 0;
                new_block->next = curr->next;
                curr->next = new_block;
                curr->size = size;
            }
            curr->used = 1;
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void heap_free(void* ptr) {
    if (!ptr) return;
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->used = 0;
    if (block->next && !block->next->used) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
    }
}
