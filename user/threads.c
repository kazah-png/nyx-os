#include "libc.h"

/* threads — exercise v5.8.87 clone(CLONE_VM) + futex.
 *
 * Spawns NTHREADS real threads that SHARE this address space (so `counter`, `lock`
 * and `stacks` below are literally the same memory in every one) and have them each
 * bump a shared counter ITERS times under a futex-backed mutex. If the sharing or the
 * mutex were broken the total would come out short; landing exactly on
 * NTHREADS*ITERS is the proof that both work.
 *
 * The lock is the textbook futex mutex: grab it with an atomic xchg, and only enter
 * the kernel (futex_wait) when it was already held — so the uncontended path is a
 * single instruction and contention costs one syscall.
 */
#define NTHREADS 4
#define ITERS    2000
#define STACKSZ  8192

static volatile int lock    = 0;   /* 0 = free, 1 = held */
static volatile int counter = 0;   /* what the threads fight over (deliberately non-atomic) */
static volatile int done    = 0;   /* how many workers have finished */
static char stacks[NTHREADS][STACKSZ];

static void lock_acquire(void) {
    for (;;) {
        int prev;
        __asm__ volatile("xchgl %0, %1" : "=r"(prev), "+m"(lock) : "0"(1) : "memory");
        if (prev == 0) return;            /* it was free — we own it now */
        futex_wait(&lock, 1);             /* still held: sleep until the owner releases */
    }
}

static void lock_release(void) {
    __asm__ volatile("" ::: "memory");
    lock = 0;
    futex_wake(&lock, 1);
}

/* Thread entry. clone() delivers `arg` in RDI, so this matches the SysV ABI directly.
 * A thread must never RETURN (there is nowhere to return to) — it ends with exit(). */
static void worker(void* arg) {
    (void)arg;
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
        /* the stack grows DOWN from the high end of each slice; keep it 16-aligned */
        unsigned long top = (unsigned long)&stacks[i][STACKSZ];
        top &= ~15UL;
        int tid = clone((void*)worker, (void*)top, (void*)i, CLONE_VM);
        if (tid < 0) { printf("threads: clone #%ld FAILED\n", i); return 1; }
        printf("threads: started tid %d\n", tid);
    }

    /* Wait for every worker. Re-reading `done` each pass makes the compare-and-sleep
     * race-free: if it changed since we read it, futex_wait returns at once. */
    while (done < NTHREADS) futex_wait(&done, done);

    int expected = NTHREADS * ITERS;
    printf("threads: counter = %d (expected %d) -> %s\n",
           counter, expected, counter == expected ? "OK" : "MISMATCH");
    return counter == expected ? 0 : 1;
}
