#include "../core/kernel.h"
#include "slab.h"
#include "../core/spinlock.h"

// The physical allocator's state — page_bitmap, page_refcount, free_pages,
// memory_used — is now reachable from any CPU, so it needs a real lock rather
// than preempt_disable(). Held for a bitmap scan at most, always with interrupts
// off (a #PF handler allocates, and re-entering on the same core would deadlock).
// init_memory/reserve_low_pages run before any AP exists and stay unlocked.
static spinlock_t page_lock = SPINLOCK_INIT;

// One lock for kmalloc/kfree, covering BOTH the slab caches and the heap free
// list. They are two allocators but one dependency graph — slab_new_page() calls
// alloc_page(), and kmalloc falls through from slab to heap — so a single lock
// keeps the ordering trivially deadlock-free (kmalloc_lock is always taken
// before page_lock, never the reverse).
static spinlock_t kmalloc_lock = SPINLOCK_INIT;

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

// Frames that must NEVER return to the allocator, whatever the refcount says: the
// shared-libc master pages (loaded once at boot, mapped read-only into every
// process). A per-process libc mapping that wasn't matched by an incref would
// otherwise drive the master reference to 0 in free_page and release the frame;
// alloc_page() then hands the still-mapped page back out (double-allocation) — the
// root of the pipeline memory corruption. Pinning the masters breaks that at the
// source. page_pin() is called from shared_libc.c as each master frame is loaded.
static uint8_t page_pinned[MAX_PAGES];
void page_pin(void* addr) {
    uint32_t page_idx = (uint32_t)(uintptr_t)addr / PAGE_SIZE;
    if (page_idx < MAX_PAGES) page_pinned[page_idx] = 1;
}

// Reserve pages [0, end_page): mark each used (clear its bitmap bit) and drop free_pages
// for every one that was free. Carves the kernel image + low 1MB out of the freed RAM.
static void reserve_low_pages(uint32_t end_page) {
    for (uint32_t i = 0; i < end_page && i < total_pages; i++) {
        if (page_bitmap[i / 32] & (1u << (i % 32))) {   // currently free -> reserve it
            page_bitmap[i / 32] &= ~(1u << (i % 32));
            free_pages--;
        }
    }
}

// Initialize the physical page allocator. When the bootloader provides a firmware
// memory map (multiboot2 type-6 tag, parsed in kernel_main), we mark EVERY page used
// and then free ONLY the ranges the firmware reports as available RAM (type 1) — so
// every hole (the low VGA/BIOS window 0xA0000-0xFFFFF, the block SeaBIOS relocates
// itself into near the top of RAM, ACPI tables, PCI MMIO) is reserved automatically,
// regardless of RAM size or where the holes sit. alloc_page can then never hand out a
// non-RAM frame — the root fix for the "pipeline corruption" Heisenbug, generalized:
// v5.8.83 reserved a hardcoded [0,_kernel_end), which covered the LOW firmware hole
// but not reserved regions ABOVE the kernel (safe only because pstorm never allocated
// that high). With no memory map we fall back to that v5.8.83 behavior.
void init_memory(uint64_t mem_size, const mb_mmap_entry_t* mmap, int mmap_count) {
    memory_total = mem_size;
    memory_used = 0;

    extern uint8_t _kernel_end[];
    uint32_t kernel_end_page = (uint32_t)(((uintptr_t)_kernel_end + PAGE_SIZE - 1) / PAGE_SIZE);

    if (mmap && mmap_count > 0) {
        memset_asm(page_bitmap, 0x00, sizeof(page_bitmap));   // all pages USED
        free_pages = 0;

        // Size the pool to the top of available RAM.
        uint64_t top = 0;
        for (int r = 0; r < mmap_count; r++)
            if (mmap[r].type == 1 && mmap[r].base + mmap[r].len > top)
                top = mmap[r].base + mmap[r].len;
        total_pages = (uint32_t)(top / PAGE_SIZE);
        if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;

        // Free every whole page inside an AVAILABLE (type 1) region.
        for (int r = 0; r < mmap_count; r++) {
            if (mmap[r].type != 1) continue;
            uint64_t start = (mmap[r].base + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);  // round up
            uint64_t end   = (mmap[r].base + mmap[r].len)   & ~(uint64_t)(PAGE_SIZE - 1);  // round down
            for (uint64_t a = start; a < end; a += PAGE_SIZE) {
                uint32_t p = (uint32_t)(a / PAGE_SIZE);
                if (p >= total_pages) break;
                if (!(page_bitmap[p / 32] & (1u << (p % 32)))) {  // used -> free
                    page_bitmap[p / 32] |= (1u << (p % 32));
                    free_pages++;
                }
            }
        }
        // The firmware map lists RAM the kernel itself occupies as available (GRUB loaded
        // us into it), so carve it back out: page 0 (NULL guard), the whole low 1MB
        // (real-mode IVT/BDA + the SMP trampoline at 0x8000, copied in later by smp_init),
        // and the kernel image/BSS through _kernel_end. Reserve [0, max(1MB, _kernel_end)).
        uint32_t low_1mb = 0x100000 / PAGE_SIZE;
        reserve_low_pages(kernel_end_page > low_1mb ? kernel_end_page : low_1mb);

        printf("[MEM] mmap %d regions: %u pages free (%u MB usable), RAM top %u MB\n",
               mmap_count, free_pages, free_pages / 256, (unsigned)(top / (1024 * 1024)));
        return;
    }

    // Fallback: no firmware memory map. Reserve [0, _kernel_end) — including the whole
    // sub-1MB firmware hole (VGA MMIO + BIOS ROM) — and treat the rest up to mem_size as
    // usable. This is the v5.8.83 behavior; it can't see reserved regions above the
    // kernel, so it is only the safety net for a boot without a type-6 tag.
    memset_asm(page_bitmap, 0xFF, sizeof(page_bitmap));
    total_pages = mem_size / PAGE_SIZE;
    if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;
    free_pages = total_pages;
    reserve_low_pages(kernel_end_page);
    printf("[MEM] no mmap: %d pages free (%d KB)\n", free_pages, free_pages * 4);
}

void* alloc_page(void) {
    uint64_t fl = spin_lock_irqsave(&page_lock);
    for (uint32_t i = 0; i < BITMAP_WORDS && i * 32 < total_pages; i++) {
        if (page_bitmap[i]) {
            uint32_t bit = __builtin_ctz(page_bitmap[i]);
            uint32_t page_idx = i * 32 + bit;
            if (page_idx >= total_pages) break;
            page_bitmap[i] &= ~(1 << bit);
            free_pages--;
            memory_used += PAGE_SIZE;
            page_refcount[page_idx] = 1;       // one owner until COW-shared
            spin_unlock_irqrestore(&page_lock, fl);
            return (void*)(uintptr_t)(page_idx * PAGE_SIZE);
        }
    }
    spin_unlock_irqrestore(&page_lock, fl);
    return NULL;
}

// Add a reference to an already-allocated physical page (used by the COW clone
// in fork: the child maps the parent's page instead of copying it).
void page_incref(void* addr) {
    uintptr_t a = (uintptr_t)addr;
    if (a & (PAGE_SIZE - 1)) return;                 // must be a page-aligned frame
    uint32_t page_idx = (uint32_t)(a / PAGE_SIZE);
    if (page_idx >= total_pages) return;
    uint64_t fl = spin_lock_irqsave(&page_lock);
    // Only an already-allocated frame can gain a reference. Incref'ing a free frame
    // (refcount 0) would leave it "referenced" yet still marked free in the bitmap,
    // so alloc_page could hand the same frame out a second time (issue #53). Saturate
    // at 0xFF rather than wrapping.
    if (page_refcount[page_idx] >= 1 && page_refcount[page_idx] < 0xFF)
        page_refcount[page_idx]++;
    spin_unlock_irqrestore(&page_lock, fl);
}

// Free-frame count. The strongest whole-machine invariant the SMP stress test
// has: every core's allocations are paired with a free, so this number must be
// identical before and after the run. A broken lock shows up as drift here even
// when no individual operation looked wrong.
uint32_t get_free_pages(void) { return free_pages; }

uint32_t page_get_refcount(void* addr) {
    uint32_t page_idx = (uint32_t)(uintptr_t)addr / PAGE_SIZE;
    if (page_idx >= total_pages) return 0;
    return page_refcount[page_idx];
}

void free_page(void* addr) {
    uintptr_t a = (uintptr_t)addr;
    // Reject a non-page-aligned address: alloc_page only ever hands out page-aligned
    // frames, so an interior pointer here is a caller bug. Silently flooring it (the
    // old behaviour) could release a DIFFERENT frame than intended (issue #54).
    if (a & (PAGE_SIZE - 1)) return;
    uint32_t page_idx = (uint32_t)(a / PAGE_SIZE);
    if (page_idx >= total_pages) return;
    uint64_t fl = spin_lock_irqsave(&page_lock);
    // Only an ALLOCATED frame (refcount >= 1) may be freed. A refcount of 0 means the
    // frame is already free (a double-free) or was never allocated (a reserved frame);
    // proceeding would push a bad frame into the pool and inflate free_pages /
    // memory_used, corrupting the allocator's accounting (issue #50).
    if (page_refcount[page_idx] == 0) { spin_unlock_irqrestore(&page_lock, fl); return; }
    // Shared page (COW): drop one reference, keep the frame for the others. The
    // read-decide-write on the refcount is exactly why this needs the lock: two
    // cores dropping the last two references could otherwise both see >1.
    if (page_refcount[page_idx] > 1) {
        page_refcount[page_idx]--;
    } else if (page_pinned[page_idx]) {
        // Pinned (shared-libc master) frames live for the whole OS lifetime: never
        // free one, and floor the refcount at 1 so a stray over-decrement can't
        // underflow it.
        page_refcount[page_idx] = 1;
    } else {
        page_refcount[page_idx] = 0;
        page_bitmap[page_idx / 32] |= 1 << (page_idx % 32);
        free_pages++;
        memory_used -= PAGE_SIZE;
    }
    spin_unlock_irqrestore(&page_lock, fl);
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
    // The slab/heap freelists are not reentrant. preempt_disable() used to hold
    // them together by stopping a context switch mid-update — which was enough
    // while one CPU ran everything, and is worth nothing against another core
    // that never consults it. The spinlock replaces it, and taking it with
    // interrupts off subsumes what preempt_disable was doing locally.
    uint64_t fl = spin_lock_irqsave(&kmalloc_lock);
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
    spin_unlock_irqrestore(&kmalloc_lock, fl);
    return result;
}

void* kmalloc_aligned(size_t size, uint32_t align) {
    (void)align;
    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;
    uint64_t fl = spin_lock_irqsave(&kmalloc_lock);
    alloc_hdr_t* hdr = ((alloc_hdr_t*)ptr) - 1;
    extern void heap_free(void*);
    if (hdr->magic == ALLOC_MAGIC_SLAB) {
        slab_free(hdr, hdr->size + sizeof(alloc_hdr_t));
    } else {
        // ALLOC_MAGIC_HEAP, or a raw heap block allocated without our header.
        heap_free(hdr);
    }
    spin_unlock_irqrestore(&kmalloc_lock, fl);
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

// Usable size of a kmalloc'd block: the request size stored in its header. Lets a
// grow-in-place buffer (vfs_pwrite) know how many bytes its current allocation holds
// without tracking a separate capacity field. NULL -> 0. Only valid for pointers
// returned by kmalloc (which always writes the header).
uint32_t ksize(void* ptr) {
    if (!ptr) return 0;
    return (((alloc_hdr_t*)ptr) - 1)->size;
}
