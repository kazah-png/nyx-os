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

/* Fill a free VMA slot with an anonymous [start,end) mapping at prot. The caller's
 * feasibility check guarantees a slot exists. */
static void vma_add_anon(process_t* p, uint64_t start, uint64_t end, uint32_t prot) {
    for (int j = 0; j < PROC_MAX_VMAS; j++) {
        if (!p->mmap_vmas[j].used) {
            vma_t* w = &p->mmap_vmas[j];
            w->start = start; w->end = end; w->prot = prot;
            w->file_buf = 0; w->file_size = 0; w->used = 1;
            return;
        }
    }
}

/* SYS_MPROTECT: change the protection of [addr, addr+length). Rewrites the flags of
 * present pages (writable per PROT_WRITE, NX unless PROT_EXEC) and updates the VMAs
 * so pages faulted in later get the right protection. A PARTIAL mprotect now SPLITS
 * the overlapping VMA (v5.9.28) so only the affected sub-range carries the new prot
 * and the rest keeps the old — before, it set the whole overlapping VMA's prot, so a
 * later fault OUTSIDE the mprotected range materialised with the wrong protection. */
int do_mprotect(uint64_t addr, uint64_t length, int prot) {
    /* VMA table is the thread group's (page_directory is shared either way) */
    process_t* p = tg_leader(get_current_process());
    if (!p || !p->page_directory || length == 0) return -1;
    addr &= ~0xFFFULL;
    length = (length + 0xFFF) & ~0xFFFULL;
    uint64_t end = addr + length;

    // The range must be USER memory, and must not wrap. Both callers of this
    // prologue (mprotect and munmap) accepted ANY address and ANY length:
    //  - aimed at the shared-libc mapping, mprotect made those pages writable
    //    for one process — and since that is one physical copy mapped read-only
    //    into everybody, it is a shared-library patch visible system-wide;
    //  - aimed at a higher-half address, both walked the kernel's own tables;
    //  - with a huge length, the per-4 KB loop below ran for billions of
    //    iterations with interrupts masked — an unprivileged hard hang.
    // The bound is the one user_v2p and the ELF loader use; one definition, in
    // kernel.h, so it cannot drift between the places that enforce it.
    if (end <= addr || addr < USER_SPACE_MIN || end > USER_SPACE_END) return -1;

    // Feasibility pass — reject before touching state. A partial mprotect splits the
    // overlapping VMA (a back/front carve: +1 record, a middle carve: +2). A
    // file-backed VMA can't be split — its pages copy from file_buf[page - v->start]
    // and a new piece with a shifted start would misalign that shared snapshot — so
    // a partial mprotect of one is refused (a full-cover mprotect, which only rewrites
    // prot without moving anything, is fine). Also confirm enough free slots exist.
    int slots_needed = 0, free_slots = 0;
    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (!v->used) { free_slots++; continue; }
        if (!(v->start < end && v->end > addr)) continue;
        int full = (v->start >= addr && v->end <= end);
        if (full) continue;
        if (v->file_buf) return -1;                     // partial mprotect of file-backed: unsupported
        int middle = (v->start < addr && v->end > end);
        slots_needed += middle ? 2 : 1;
    }
    if (slots_needed > free_slots) return -1;

    // Page tables first (precise to [addr, end)); then the VMA bookkeeping. Snapshot
    // the original overlapping VMAs before splitting — the new-prot pieces added below
    // DO overlap [addr, end), so a live re-scan would reprocess them.
    vm_protect_range((uint64_t*)p->page_directory, addr, end, prot);

    int todo[PROC_MAX_VMAS], ntodo = 0;
    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (v->used && v->start < end && v->end > addr) todo[ntodo++] = i;
    }
    for (int t = 0; t < ntodo; t++) {
        vma_t* v = &p->mmap_vmas[todo[t]];
        uint32_t oldp = v->prot;
        int full   = (v->start >= addr && v->end <= end);
        int middle = (v->start <  addr && v->end >  end);
        int back   = (v->start <  addr && v->end <= end);
        if (full) {
            v->prot = (uint32_t)prot;
        } else if (back) {                              // mprotect covers the back
            uint64_t oe = v->end;
            v->end = addr;                              // head [start, addr) keeps old prot
            vma_add_anon(p, addr, oe, (uint32_t)prot);  // tail [addr, oe) gets new prot
        } else if (middle) {                            // a band of new prot in the middle
            uint64_t oe = v->end;
            v->end = addr;                              // head [start, addr) old
            vma_add_anon(p, addr, end, (uint32_t)prot); // mid  [addr, end) new
            vma_add_anon(p, end, oe, oldp);             // tail [end, oe)  old
        } else {                                        // front: mprotect covers the front
            uint64_t os = v->start;
            v->start = end;                             // tail [end, v->end) keeps old prot
            vma_add_anon(p, os, end, (uint32_t)prot);   // front [os, end) gets new prot
        }
    }
    return 0;
}

/* SYS_MUNMAP: release [addr, addr+length). Frees every present page in the range
 * (free_page is refcount-aware, so a COW-shared page just drops a reference) and
 * updates the VMA table so the unmapped range no longer resolves. A partial unmap
 * now SPLITS or TRIMS the affected VMA (v5.9.27); before, only a fully-covered VMA
 * was dropped and a partial unmap left the record intact — so the "unmapped" range
 * still had a covering VMA and vm_handle_fault would re-materialise a zero page
 * there on the next touch. The four overlap cases (a 2x2 on which ends are inside
 * the unmap range) are: full cover (drop), back trim (v->end = addr), front trim
 * (v->start = end), and a hole in the middle (shrink to the head, add a new VMA
 * for the tail). File-backed VMAs copy each page from file_buf[page - v->start], so
 * moving v->start (front trim / middle split) would misalign that shared snapshot —
 * those two cases are refused for file-backed maps; full-cover and back-trim, which
 * leave v->start put, are fine. */
int do_munmap(uint64_t addr, uint64_t length) {
    extern void vm_free_range(uint64_t* pml4, uint64_t start, uint64_t end);
    /* unmap from the group's shared table: a thread may release what a sibling mapped */
    process_t* p = tg_leader(get_current_process());
    if (!p || !p->page_directory || length == 0) return -1;
    addr &= ~0xFFFULL;
    length = (length + 0xFFF) & ~0xFFFULL;
    uint64_t end = addr + length;

    // The range must be USER memory, and must not wrap. Both callers of this
    // prologue (mprotect and munmap) accepted ANY address and ANY length:
    //  - aimed at the shared-libc mapping, mprotect made those pages writable
    //    for one process — and since that is one physical copy mapped read-only
    //    into everybody, it is a shared-library patch visible system-wide;
    //  - aimed at a higher-half address, both walked the kernel's own tables;
    //  - with a huge length, the per-4 KB loop below ran for billions of
    //    iterations with interrupts masked — an unprivileged hard hang.
    // The bound is the one user_v2p and the ELF loader use; one definition, in
    // kernel.h, so it cannot drift between the places that enforce it.
    if (end <= addr || addr < USER_SPACE_MIN || end > USER_SPACE_END) return -1;

    // Feasibility pass — reject cleanly BEFORE freeing anything: a file-backed VMA
    // needing a front trim or a middle split can't be re-based, and a middle split
    // needs a free slot for the new tail VMA.
    int splits_needed = 0, free_slots = 0;
    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (!v->used) { free_slots++; continue; }
        if (!(v->start < end && v->end > addr)) continue;          // no overlap
        int full   = (v->start >= addr && v->end <= end);
        int middle = (v->start <  addr && v->end >  end);
        int front  = (v->start >= addr && v->end >  end);
        if (full) continue;
        if (v->file_buf && (front || middle)) return -1;           // unsupported for file-backed
        if (middle) splits_needed++;
    }
    if (splits_needed > free_slots) return -1;

    vm_free_range((uint64_t*)p->page_directory, addr, end);

    for (int i = 0; i < PROC_MAX_VMAS; i++) {
        vma_t* v = &p->mmap_vmas[i];
        if (!v->used) continue;
        if (!(v->start < end && v->end > addr)) continue;          // new tail starts at `end`, so excluded
        int full   = (v->start >= addr && v->end <= end);
        int middle = (v->start <  addr && v->end >  end);
        int front  = (v->start >= addr && v->end >  end);
        if (full) {
            if (v->file_buf) { kfree(v->file_buf); v->file_buf = 0; v->file_size = 0; }
            v->used = 0;
        } else if (middle) {                                       // punch a hole: head + new tail
            uint64_t old_end = v->end;
            v->end = addr;                                         // head keeps [start, addr)
            for (int j = 0; j < PROC_MAX_VMAS; j++) {
                if (!p->mmap_vmas[j].used) {                       // feasibility guaranteed a slot
                    vma_t* w = &p->mmap_vmas[j];
                    w->start = end; w->end = old_end; w->prot = v->prot;
                    w->file_buf = 0; w->file_size = 0; w->used = 1; // anonymous only (checked above)
                    break;
                }
            }
        } else if (front) {
            v->start = end;                                        // tail [end, v->end) survives
        } else {                                                  // back trim
            v->end = addr;                                         // head [v->start, addr) survives
        }
    }
    return 0;
}
