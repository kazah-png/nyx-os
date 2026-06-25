#include "kernel.h"

// Bitmap-based physical page allocator
// Each bit represents one 4KB page (1 = free, 0 = used)
// Supports up to 512MB of physical RAM with 16KB bitmap

#define MAX_PAGES (512 * 1024 * 1024 / 4096)
#define BITMAP_WORDS (MAX_PAGES / 32)
static uint32_t page_bitmap[BITMAP_WORDS];
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;

void init_memory(uint64_t mem_size) {
    memory_total = mem_size;
    memory_used = 0;
    memset_asm(page_bitmap, 0xFF, sizeof(page_bitmap));

    total_pages = mem_size / PAGE_SIZE;
    if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;
    free_pages = total_pages;

    // Mark page 0 as used (NULL page)
    page_bitmap[0] &= ~1;
    free_pages--;

    // Mark kernel pages as used (from 0x100000 up to end of BSS)
    extern uint8_t _kernel_end[];
    uintptr_t kernel_end = (uintptr_t)_kernel_end;
    uint32_t kernel_start_page = 0x100000 / PAGE_SIZE;
    uint32_t kernel_end_page = (uint32_t)((kernel_end + PAGE_SIZE - 1) / PAGE_SIZE);
    for (uint32_t i = kernel_start_page; i < kernel_end_page && i < total_pages; i++) {
        page_bitmap[i / 32] &= ~(1 << (i % 32));
        free_pages--;
    }

    printf("[MEM] Bitmap at %p, %d pages free (%d KB)\n",
        (void*)page_bitmap, free_pages, free_pages * 4);
}

void* alloc_page(void) {
    for (uint32_t i = 0; i < BITMAP_WORDS && i * 32 < total_pages; i++) {
        if (page_bitmap[i]) {
            uint32_t bit = __builtin_ctz(page_bitmap[i]);
            uint32_t page_idx = i * 32 + bit;
            if (page_idx >= total_pages) return NULL;
            page_bitmap[i] &= ~(1 << bit);
            free_pages--;
            memory_used += PAGE_SIZE;
            return (void*)(uintptr_t)(page_idx * PAGE_SIZE);
        }
    }
    return NULL;
}

void free_page(void* addr) {
    uint32_t page_idx = (uint32_t)(uintptr_t)addr / PAGE_SIZE;
    if (page_idx >= total_pages) return;
    page_bitmap[page_idx / 32] |= 1 << (page_idx % 32);
    free_pages++;
    memory_used -= PAGE_SIZE;
}

// heap.c uses these for kmalloc
void* kmalloc(size_t size) {
    extern void* heap_alloc(size_t);
    return heap_alloc(size);
}

void* kmalloc_aligned(size_t size, uint32_t align) {
    (void)align;
    return kmalloc(size);
}

void kfree(void* ptr) {
    extern void heap_free(void*);
    heap_free(ptr);
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }
    void* newp = kmalloc(size);
    if (newp) memcpy_asm(newp, ptr, size);
    kfree(ptr);
    return newp;
}
