#include "libc.h"

/* threads — exercise clone(CLONE_VM) + futex (v5.8.87) and the SHARED thread-group
 * heap (v5.8.88).
 *
 * Two things are proven here:
 *  1) MUTEX + SHARED MEMORY: NTHREADS threads each bump a shared counter ITERS times
 *     behind a futex mutex. `counter` is deliberately non-atomic, so landing exactly on
 *     NTHREADS*ITERS means both the address-space sharing and the lock work.
 *  2) SHARED HEAP: each thread malloc()s its own block and signs it. Before the thread
 *     group shared one program_break, every thread bumped its OWN copy of the break and
 *     they were handed OVERLAPPING regions — so distinct, non-overlapping, still-intact
 *     blocks are the proof that multi-threaded malloc is now coherent.
 *
 * The lock is the textbook futex mutex: an atomic xchg fast path, entering the kernel
 * only when the lock was already held.
 */
#define NTHREADS 4
#define ITERS    2000
#define STACKSZ  8192
#define BLKSZ    256

static volatile int lock    = 0;   /* 0 = free, 1 = held */
static volatile int counter = 0;   /* what the threads fight over (non-atomic on purpose) */
static volatile int done    = 0;   /* how many workers have finished */
static char  stacks[NTHREADS][STACKSZ];
static void* blocks[NTHREADS];     /* each worker's malloc'd block */

static void lock_acquire(void) {
    for (;;) {
        int prev;
        __asm__ volatile("xchgl %0, %1" : "=r"(prev), "+m"(lock) : "0"(1) : "memory");
        if (prev == 0) return;            /* it was free — ours now */
        futex_wait(&lock, 1);             /* held: sleep until the owner releases */
    }
}

static void lock_release(void) {
    __asm__ volatile("" ::: "memory");
    lock = 0;
    futex_wake(&lock, 1);
}

/* clone() delivers `arg` in RDI, matching the SysV ABI. A thread must never RETURN
 * (there is nowhere to return to) — it ends with exit(). */
static void worker(void* arg) {
    long id = (long)arg;

    /* Heap test: this malloc must come out of the thread GROUP's heap. */
    char* b = (char*)malloc(BLKSZ);
    blocks[id] = b;
    if (b) for (int i = 0; i < BLKSZ; i++) b[i] = (char)('A' + id);

    for (int i = 0; i < ITERS; i++) {
        lock_acquire();
        counter++;                        /* the lock is what makes this safe */
        lock_release();
    }
    lock_acquire();
    done++;
    lock_release();
    futex_wake(&done, 1);                 /* nudge main, which waits on `done` */
    exit(0);
}

int main(void) {
    printf("threads: spawning %d threads x %d increments each (shared address space)\n",
           NTHREADS, ITERS);
    for (long i = 0; i < NTHREADS; i++) {
        unsigned long top = (unsigned long)&stacks[i][STACKSZ];
        top &= ~15UL;                     /* stack grows down; keep it 16-aligned */
        int tid = clone((void*)worker, (void*)top, (void*)i, CLONE_VM);
        if (tid < 0) { printf("threads: clone #%ld FAILED\n", i); return 1; }
        printf("threads: started tid %d\n", tid);
    }

    /* Re-reading `done` each pass keeps the compare-and-sleep race-free. */
    while (done < NTHREADS) futex_wait(&done, done);

    int expected = NTHREADS * ITERS;
    printf("threads: counter = %d (expected %d) -> %s\n",
           counter, expected, counter == expected ? "OK" : "MISMATCH");

    /* Every block must exist, still hold its own signature, and not overlap another. */
    int heap_ok = 1;
    for (int i = 0; i < NTHREADS; i++) {
        char* bi = (char*)blocks[i];
        if (!bi) { heap_ok = 0; continue; }
        if (bi[0] != (char)('A' + i) || bi[BLKSZ - 1] != (char)('A' + i)) heap_ok = 0;
        for (int j = i + 1; j < NTHREADS; j++) {
            char* bj = (char*)blocks[j];
            if (bj && bi < bj + BLKSZ && bj < bi + BLKSZ) heap_ok = 0;   /* overlap! */
        }
    }
    printf("threads: heap    = %s (%d per-thread malloc blocks distinct + intact)\n",
           heap_ok ? "OK" : "OVERLAP/CORRUPT", NTHREADS);

    return (counter == expected && heap_ok) ? 0 : 1;
}
