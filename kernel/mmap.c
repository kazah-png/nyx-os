#include "kernel.h"

/* ============================================================
 * mmap.c — anonymous, demand-zero memory mappings (v5.8.12)
 * ============================================================
 * A process reserves a virtual range with mmap(); the pages materialise on first
 * touch. do_mmap only records a VMA (start/end/prot) and bumps a per-process
 * pointer — no pages are allocated. A fault inside a VMA is serviced by
 * vm_handle_fault (paging.c), which allocs a zeroed page and maps it with the
 * VMA's prot. munmap frees the present pages (refcount-aware) and drops the VMA.
 *
 * Mappings sit in [MMAP_BASE, MMAP_MAX) = [4 GiB, 112 TiB): above the 4 GiB heap
 * cap (SYS_SBRK) and far below the 128 TiB user stack, so they never collide. This
 * is anonymous MAP_PRIVATE only — file-backed mmap and addr hints are future work. */

/* Release a process's file-backed VMA snapshot buffers. Called when an address
 * space is torn down (reap), since those buffers are kernel-heap allocations that
 * live outside the page directory free_page_directory() releases. */
void mmap_free_bufs(process_t* p) {
    if (!p) return;
    for (int i = 0; i < PROC_MAX_VMAS; i++)
        if (p->mmap_vmas[i].file_buf) {
            kfree(p->mmap_vmas[i].file_buf);
            p->mmap_vmas[i].file_buf = 0;
            p->mmap_vmas[i].file_size = 0;
        }
}

/* Find the VMA containing `addr`, or NULL. Called from the #PF handler. */
vma_t* vma_find(process_t* p, uint64_t addr) {
    p = tg_leader(p);          /* CLONE_VM threads share the leader's mmap table */
    if (!p) return 0;
    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (v->used && addr >= v->start && addr < v->end) return v;
    }
    return 0;
}

/* SYS_MMAP: reserve `length` bytes of demand-paged memory. addr is an ignored
 * hint. Anonymous (file_handle==0) faults in as zero; file-backed snapshots the
 * file (from offset 0) into a private kernel buffer here, and each faulted page
 * copies its slice from that buffer. Returns the base VA, or MAP_FAILED. */
uint64_t do_mmap(uint64_t addr, uint64_t length, int prot, int flags,
                 int file_handle, uint32_t file_size, uint32_t file_off) {
    (void)addr; (void)flags;
    /* Allocate out of the thread group's SHARED mmap table, so two threads can never
     * be handed the same region and either can munmap what the other mapped. */
    process_t* p = tg_leader(get_current_process());
    if (!p || length == 0) return (uint64_t)-1;
    length = (length + 0xFFF) & ~0xFFFULL;              /* round up to whole pages */
    if (p->mmap_next < MMAP_BASE) p->mmap_next = MMAP_BASE;   /* lazy first-use init */

    int slot = -1;
    for (int i = 0; i < PROC_MAX_VMAS; i++)
        if (!p->mmap_vmas[i].used) { slot = i; break; }
    if (slot < 0) return (uint64_t)-1;                  /* too many regions */

    uint64_t base = p->mmap_next;
    if (base + length > MMAP_MAX || base + length < base) return (uint64_t)-1;

    // File-backed: snapshot the file from `file_off` to EOF into a private buffer, so
    // a demand-faulted page copies file_buf[page_offset] (mapping byte 0 == file[off]).
    uint8_t* fb = 0; uint32_t snap = 0;
    if (file_handle && file_off < file_size) {
        snap = file_size - file_off;
        fb = (uint8_t*)kmalloc(snap);
        if (!fb) return (uint64_t)-1;
        if (vfs_pread(file_handle, fb, snap, file_off) < 0) { kfree(fb); return (uint64_t)-1; }
    }

    p->mmap_next = base + length;
    p->mmap_vmas[slot].start     = base;
    p->mmap_vmas[slot].end       = base + length;
    p->mmap_vmas[slot].prot      = (uint32_t)prot;
    p->mmap_vmas[slot].used      = 1;
    p->mmap_vmas[slot].file_buf  = fb;
    p->mmap_vmas[slot].file_size = snap;
    return base;                                        /* pages fault in on first touch */
}

/* SYS_MPROTECT: change the protection of [addr, addr+length). Rewrites the flags of
 * present pages (writable per PROT_WRITE, NX unless PROT_EXEC) and updates the prot
 * stored in overlapping VMAs so pages faulted in later get the new protection.
 * v1 note: a partial mprotect updates the whole overlapping VMA's prot. */
int do_mprotect(uint64_t addr, uint64_t length, int prot) {
    /* VMA table is the thread group's (page_directory is shared either way) */
    process_t* p = tg_leader(get_current_process());
    if (!p || !p->page_directory || length == 0) return -1;
    addr &= ~0xFFFULL;
    length = (length + 0xFFF) & ~0xFFFULL;
    uint64_t end = addr + length;
    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (v->used && v->start < end && v->end > addr) v->prot = (uint32_t)prot;
    }
    vm_protect_range((uint64_t*)p->page_directory, addr, end, prot);
    return 0;
}

/* SYS_MUNMAP: release [addr, addr+length). Frees every present page in the range
 * (free_page is refcount-aware, so a COW-shared page just drops a reference) and
 * drops any VMA fully covered by the range. Partial unmaps (splitting a VMA) are
 * not supported in v1 — the pages are freed but the VMA record is left intact. */
int do_munmap(uint64_t addr, uint64_t length) {
    extern void vm_free_range(uint64_t* pml4, uint64_t start, uint64_t end);
    /* unmap from the group's shared table: a thread may release what a sibling mapped */
    process_t* p = tg_leader(get_current_process());
    if (!p || !p->page_directory || length == 0) return -1;
    addr &= ~0xFFFULL;
    length = (length + 0xFFF) & ~0xFFFULL;
    uint64_t end = addr + length;

    vm_free_range((uint64_t*)p->page_directory, addr, end);

    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (v->used && v->start >= addr && v->end <= end) {
            if (v->file_buf) { kfree(v->file_buf); v->file_buf = 0; v->file_size = 0; }
            v->used = 0;
        }
    }
    return 0;
}
