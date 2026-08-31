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

// Live accounting so the kernel heap isn't a black box: `mem` reports real usage instead of just
// the static size, and an exhausted heap is surfaced (fail count) rather than a silent NULL.
// used = bytes committed to live allocations (each block's payload + its header); hiwater = peak
// used; fails = heap_alloc calls that found no fit. Pinned by heap_selftest (the `heap` KAT).
static size_t   g_heap_used = 0;
static size_t   g_heap_hiwater = 0;
static uint32_t g_heap_fails = 0;

void init_heap(void) {
    free_list->size = sizeof(heap) - sizeof(heap_block_t);
    free_list->used = 0;
    free_list->next = NULL;
    g_heap_used = 0;
    g_heap_hiwater = 0;
    g_heap_fails = 0;
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
            g_heap_used += curr->size + sizeof(heap_block_t);   // account payload + header
            if (g_heap_used > g_heap_hiwater) g_heap_hiwater = g_heap_used;
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }
    g_heap_fails++;                                             // no block fit -> out of heap
    return NULL;
}

void heap_free(void* ptr) {
    if (!ptr) return;
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    // Un-account this block's cost BEFORE any coalescing changes block->size (guard underflow
    // in case of a double-free / bad pointer). Merged-in blocks were free, so already uncounted.
    size_t cost = block->size + sizeof(heap_block_t);
    g_heap_used = (g_heap_used >= cost) ? g_heap_used - cost : 0;
    block->used = 0;
    if (block->next && !block->next->used) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
    }
}

// Live kernel-heap statistics (bytes). Any out-arg may be NULL. total is the fixed heap size.
void heap_stats(size_t* used, size_t* total, size_t* hiwater, uint32_t* fails) {
    if (used)    *used    = g_heap_used;
    if (total)   *total   = HEAP_SIZE;
    if (hiwater) *hiwater = g_heap_hiwater;
    if (fails)   *fails   = g_heap_fails;
}

// KAT (`heap`): pins the usage accounting behind the `mem` report — a successful alloc raises
// `used` by at least its payload and free restores it exactly, an over-sized request bumps the
// failure counter and returns NULL, and `total` is the real heap size. Runs on the live heap
// (like slab/pagealloc self-tests) but frees everything it takes, so it nets to zero.
int heap_selftest(void) {
    size_t u0, total, hw0; uint32_t f0;
    heap_stats(&u0, &total, &hw0, &f0);
    if (total != HEAP_SIZE) return 1;
    void* p = heap_alloc(1000);
    if (!p) return 2;
    size_t u1; heap_stats(&u1, 0, 0, 0);
    if (u1 < u0 + 1000) return 3;                  // used grew by at least the payload
    heap_free(p);
    size_t u2; heap_stats(&u2, 0, 0, 0);
    if (u2 != u0) return 4;                         // free restores the prior usage exactly
    uint32_t f1; heap_stats(0, 0, 0, &f1);
    if (heap_alloc(HEAP_SIZE + 1) != 0) return 5;   // bigger than the whole heap -> no fit
    uint32_t f2; heap_stats(0, 0, 0, &f2);
    if (f2 != f1 + 1) return 6;                     // the failure was counted, not silent
    size_t u3; heap_stats(&u3, 0, 0, 0);
    if (u3 != u0) return 7;                         // a failed alloc commits nothing
    return 0;
}
