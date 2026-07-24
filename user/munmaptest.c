#include "libc.h"

/* munmaptest — prove partial munmap SPLITS/TRIMS the VMA (v5.9.27), so a freed
 * sub-range stops resolving instead of faulting back in as a fresh zero page.
 *
 * Before the fix, do_munmap freed the pages of a partial unmap but left the VMA
 * record covering the whole original range. The pages were gone, but the VMA still
 * matched, so the next touch of the "unmapped" hole found the VMA and
 * vm_handle_fault handed back a brand-new zeroed page — munmap of part of a region
 * silently didn't take. This writes a marker to every page, punches a hole in the
 * middle, and checks (a) the surrounding pages keep their data (the split preserved
 * head + tail) and (b) the hole no longer resolves: reading it must FAULT, not
 * return 0. */

#define PGSZ 4096
static int fail = 0;
static void check(const char* what, int ok) {
    printf("munmaptest: [%s] %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fail = 1;
}

int main(void) {
    /* Reserve 5 demand-zero pages as ONE VMA, mark each page's first byte. */
    unsigned char* base = (unsigned char*)mmap(0, 5 * PGSZ, PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == (unsigned char*)MAP_FAILED) { printf("munmaptest: mmap FAILED\n"); return 1; }
    for (int i = 0; i < 5; i++) base[i * PGSZ] = (unsigned char)(0xA0 + i);

    int ok = 1;
    for (int i = 0; i < 5; i++) if (base[i * PGSZ] != (unsigned char)(0xA0 + i)) ok = 0;
    check("mmap: 5 pages faulted in and hold their markers", ok);

    /* Punch a hole: unmap the MIDDLE page (page 2) -> split into head [0,2), tail [3,5). */
    long r = munmap(base + 2 * PGSZ, PGSZ);
    check("munmap(middle) returned 0", r == 0);

    /* The four surrounding pages must still hold their markers (split touched neither). */
    ok = (base[0*PGSZ]==0xA0) && (base[1*PGSZ]==0xA1) && (base[3*PGSZ]==0xA3) && (base[4*PGSZ]==0xA4);
    check("split preserved head [0,2) and tail [3,5) markers", ok);

    /* Run the other two overlap cases: back-trim the tail, front-trim the head. */
    r = munmap(base + 4 * PGSZ, PGSZ);      /* back-trim tail [3,5) -> [3,4) */
    check("munmap(back trim) returned 0", r == 0);
    r = munmap(base + 0 * PGSZ, PGSZ);      /* front-trim head [0,2) -> [1,2) */
    check("munmap(front trim) returned 0", r == 0);
    ok = (base[1*PGSZ]==0xA1) && (base[3*PGSZ]==0xA3);
    check("trims preserved the still-mapped pages", ok);

    if (fail) { printf("munmaptest: FAIL (before hole probe)\n"); return 1; }

    /* Decisive check: the freed middle hole must NOT resolve. On the fixed kernel
     * this FAULTS and the process is killed with SIGSEGV (the harness sees the kill
     * and never the line below). On the buggy kernel the stale VMA refaults a zero
     * page and the read returns 0 — printed below as the BUG. */
    printf("munmaptest: probing freed hole (fixed => SIGSEGV here; bug => reads 0)\n");
    volatile unsigned char c = base[2 * PGSZ];
    printf("munmaptest: HOLE STILL MAPPED, read 0x%x = BUG (VMA was not split)\n", (int)c);
    return 1;   /* reaching this line at all is the bug */
}
