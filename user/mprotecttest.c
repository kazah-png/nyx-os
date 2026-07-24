#include "libc.h"

/* mprotecttest — prove partial mprotect SPLITS the VMA (v5.9.28), so pages OUTSIDE
 * the mprotected range keep their original protection.
 *
 * Before the fix, do_mprotect set the protection field of the WHOLE overlapping VMA,
 * even when the range covered only part of it. The page tables were updated only for
 * the range, but a page elsewhere in the same VMA that had not faulted in yet would
 * later materialise with the mprotected (wrong) protection. Concretely: mprotect the
 * MIDDLE of an RW mapping to read-only, and the still-RW pages on either side turned
 * read-only too the moment they were first touched.
 *
 * This maps four RW pages (unfaulted), mprotects the middle two to read-only, then
 * WRITES the two outside pages. On the fixed kernel the split kept them RW, so the
 * writes succeed. On the buggy kernel the whole VMA is read-only, so the first
 * outside write faults and the process dies before the milestone below. Finally it
 * writes an inside (read-only) page, which must fault — proving the range IS RO. */

#define PGSZ 4096
static int fail = 0;
static void check(const char* what, int ok) {
    printf("mprotecttest: [%s] %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok) fail = 1;
}

int main(void) {
    unsigned char* base = (unsigned char*)mmap(0, 4 * PGSZ, PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == (unsigned char*)MAP_FAILED) { printf("mprotecttest: mmap FAILED\n"); return 1; }

    /* mprotect the middle two pages (1,2) to read-only. Partial -> the one VMA must
     * split into RW [0,1), RO [1,3), RW [3,4). No page is faulted in yet. */
    long r = mprotect(base + 1 * PGSZ, 2 * PGSZ, PROT_READ);
    check("mprotect(middle 2 pages -> RO) returned 0", r == 0);
    if (r != 0) { printf("mprotecttest: FAIL (mprotect rejected)\n"); return 1; }

    /* Write the OUTSIDE pages (0 and 3). On the BUGGY kernel the whole VMA is now RO,
     * so this write faults and the process is killed HERE — before the milestone. */
    base[0 * PGSZ] = 0xC0;
    base[3 * PGSZ] = 0xC3;
    int ok = (base[0 * PGSZ] == 0xC0) && (base[3 * PGSZ] == 0xC3);
    check("outside pages [0) and [3) still writable after partial mprotect", ok);

    /* Reads of the inside RO band are fine. */
    volatile unsigned char rr = base[1 * PGSZ]; (void)rr;
    check("inside RO page readable", 1);

    if (fail) { printf("mprotecttest: FAIL\n"); return 1; }
    printf("mprotecttest: OUTSIDE-OK milestone reached (split kept the flanks RW)\n");

    /* Decisive: a write into the mprotected (read-only) band must FAULT. Fixed kernel
     * => SIGSEGV here and the line after never prints; a kernel that wrongly left the
     * band writable would print the BUG line. */
    printf("mprotecttest: probing RO-band write (fixed => SIGSEGV here)\n");
    base[1 * PGSZ] = 0xFF;
    printf("mprotecttest: RO BAND WAS WRITABLE = BUG (mprotect page-prot didn't hold)\n");
    return 1;
}
