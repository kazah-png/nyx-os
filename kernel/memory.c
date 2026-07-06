#include "kernel.h"
#include "slab.h"

// Bitmap-based physical page allocator
// Each bit represents one 4KB page (1 = free, 0 = used)
// Supports up to 512MB of physical RAM with 16KB bitmap

#define MAX_PAGES (512 * 1024 * 1024 / 4096)
#define BITMAP_WORDS (MAX_PAGES / 32)
static uint32_t page_bitmap[BITMAP_WORDS];
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;

// Per-page reference count, indexed by physical page number. Copy-on-write
// (fork) shares one physical page between several address spaces; the refcount
// is how many PTEs point at it. alloc_page() sets it to 1; page_incref() bumps
// it when a page is shared (COW clone); free_page() decrements and only returns
// the frame to the bitmap when the last reference drops. A refcount of 0 for a
// page that was never tracked (reserved at init, or pre-refcount allocations)
// simply means "unconditional free", so legacy callers keep working.
static uint8_t page_refcount[MAX_PAGES];

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
            page_refcount[page_idx] = 1;       // one owner until COW-shared
            return (void*)(uintptr_t)(page_idx * PAGE_SIZE);
        }
    }
    return NULL;
}

// Add a reference to an already-allocated physical page (used by the COW clone
// in fork: the child maps the parent's page instead of copying it).
void page_incref(void* addr) {
    uint32_t page_idx = (uint32_t)(uintptr_t)addr / PAGE_SIZE;
    if (page_idx >= total_pages) return;
    if (page_refcount[page_idx] < 0xFF) page_refcount[page_idx]++;
}

uint32_t page_get_refcount(void* addr) {
    uint32_t page_idx = (uint32_t)(uintptr_t)addr / PAGE_SIZE;
    if (page_idx >= total_pages) return 0;
    return page_refcount[page_idx];
}

void free_page(void* addr) {
    uint32_t page_idx = (uint32_t)(uintptr_t)addr / PAGE_SIZE;
    if (page_idx >= total_pages) return;
    // Shared page (COW): drop one reference, keep the frame for the others.
    if (page_refcount[page_idx] > 1) { page_refcount[page_idx]--; return; }
    page_refcount[page_idx] = 0;
    page_bitmap[page_idx / 32] |= 1 << (page_idx % 32);
    free_pages++;
    memory_used -= PAGE_SIZE;
}

// Small allocation header for slab/heap routing
typedef struct alloc_hdr {
    uint32_t magic;
    uint32_t size;
} alloc_hdr_t;

// Two magics so kfree knows the true origin. The slab can't serve every size
// <= SLAB_MAX_OBJ (its cache classes may not cover a size, and the header pushes
// some requests over), so kmalloc falls back to the heap — kfree must route to
// the matching allocator regardless of size.
#define ALLOC_MAGIC_SLAB 0x4E79584F // "NyXO"
#define ALLOC_MAGIC_HEAP 0x4E795848 // "NyXH"

void slab_init_all(void) {
    slab_init();
}

// kmalloc: use slab for small objects (<=512 bytes), heap for larger
void* kmalloc(size_t size) {
    // preempt_disable: the slab/heap freelists are not reentrant, so a preemptive
    // context switch mid-update (to another thread that also allocates) would
    // corrupt them. Keep the whole allocation atomic w.r.t. the scheduler.
    preempt_disable();
    void* result = NULL;
    // Try the slab for small objects. slab_alloc returns NULL when no cache
    // class covers (size + header); in that case fall through to the heap
    // rather than failing the allocation (the old code returned NULL here,
    // so every 505..1016-byte kmalloc — e.g. VFS file writes — failed).
    if (size + sizeof(alloc_hdr_t) <= SLAB_MAX_OBJ) {
        void* ptr = slab_alloc((uint32_t)size + sizeof(alloc_hdr_t));
        if (ptr) {
            alloc_hdr_t* hdr = (alloc_hdr_t*)ptr;
            hdr->magic = ALLOC_MAGIC_SLAB;
            hdr->size = (uint32_t)size;
            result = (void*)(hdr + 1);
        }
    }
    if (!result) {
        extern void* heap_alloc(size_t);
        void* ptr = heap_alloc(size + sizeof(alloc_hdr_t));
        if (ptr) {
            alloc_hdr_t* hdr = (alloc_hdr_t*)ptr;
            hdr->magic = ALLOC_MAGIC_HEAP;
            hdr->size = (uint32_t)size;
            result = (void*)(hdr + 1);
        }
    }
    preempt_enable();
    return result;
}

void* kmalloc_aligned(size_t size, uint32_t align) {
    (void)align;
    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;
    preempt_disable();
    alloc_hdr_t* hdr = ((alloc_hdr_t*)ptr) - 1;
    extern void heap_free(void*);
    if (hdr->magic == ALLOC_MAGIC_SLAB) {
        slab_free(hdr, hdr->size + sizeof(alloc_hdr_t));
    } else {
        // ALLOC_MAGIC_HEAP, or a raw heap block allocated without our header.
        heap_free(hdr);
    }
    preempt_enable();
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (!size) { kfree(ptr); return NULL; }
    void* newp = kmalloc(size);
    if (newp) {
        alloc_hdr_t* hdr = ((alloc_hdr_t*)ptr) - 1;
        size_t old_size = hdr->size;
        memcpy_asm(newp, ptr, old_size < size ? old_size : size);
    }
    kfree(ptr);
    return newp;
}
