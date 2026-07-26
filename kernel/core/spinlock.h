#ifndef SPINLOCK_H
#define SPINLOCK_H

/* Spinlocks (v5.8.91).
 *
 * Until now the kernel serialised shared state with preempt_disable(), which is
 * just a counter the scheduler consults. That works when one CPU runs everything
 * — it stops a context switch mid-update — and means exactly NOTHING to another
 * core, which never looks at it. Real locks are the prerequisite for letting an
 * application processor touch any shared structure.
 *
 * Deliberately a plain test-and-set lock, not a ticket lock: NyxOS has at most 8
 * cores and the critical sections here are a few dozen instructions, so the
 * fairness a ticket lock buys is not worth its extra state. Revisit if a lock
 * ever gets held long enough for starvation to be observable.
 *
 * DISCIPLINE — always take these with interrupts disabled on the local CPU
 * (spin_lock_irqsave). A spinlock protects against OTHER cores; it does nothing
 * about the same core re-entering through an interrupt handler, which would
 * simply deadlock against itself. Disabling interrupts is what closes that.
 */

typedef struct {
    volatile uint32_t locked;   /* 0 = free, 1 = held */
} spinlock_t;

#define SPINLOCK_INIT { 0 }

/* xchg on memory is atomic on x86 with no LOCK prefix — the bus lock is implied.
 * The inner spin re-reads with plain loads (and `pause`, which yields the core's
 * pipeline and, under QEMU/TCG, its execution slice) so the contended case does
 * not hammer the cache line with atomic RMWs. */
static inline void spin_lock(spinlock_t* l) {
    for (;;) {
        uint32_t prev;
        __asm__ volatile("xchgl %0, %1"
                         : "=r"(prev), "+m"(l->locked)
                         : "0"(1)
                         : "memory");
        if (prev == 0) return;
        while (l->locked) __asm__ volatile("pause" ::: "memory");
    }
}

static inline int spin_trylock(spinlock_t* l) {
    uint32_t prev;
    __asm__ volatile("xchgl %0, %1" : "=r"(prev), "+m"(l->locked) : "0"(1) : "memory");
    return prev == 0;
}

/* A plain store suffices to release: x86 stores have release semantics, so every
 * write inside the critical section is visible before the lock reads as free.
 * The compiler barrier keeps the compiler from hoisting them past it. */
static inline void spin_unlock(spinlock_t* l) {
    __asm__ volatile("" ::: "memory");
    l->locked = 0;
}

/* Returns the caller's RFLAGS so the matching unlock can restore IF exactly as
 * it was — nesting must not enable interrupts early in an outer critical section. */
static inline uint64_t spin_lock_irqsave(spinlock_t* l) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    spin_lock(l);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t* l, uint64_t flags) {
    spin_unlock(l);
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

#endif
