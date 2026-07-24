#include "kernel.h"
#include "elf.h"

extern void* syscall_table[SYS_TABLE_SIZE];

void init_syscalls(void) {
    memset_asm(syscall_table, 0, sizeof(syscall_table));
}

static process_t* get_cur_proc(void) {
    // The running process is whatever the scheduler last switched to (syscalls run
    // with interrupts masked, so current_idx can't change under us mid-call).
    extern process_t* get_current_process(void);
    return get_current_process();
}

// Set up syscall MSRs for the syscall/sysret mechanism
void setup_syscall_msrs(void) {
    // Enable SYSCALL/SYSRET (EFER.SCE). Without this the `syscall` instruction
    // raises #UD in ring 3 — the reason user processes crashed on entry.
    write_msr(MSR_EFER, read_msr(MSR_EFER) | EFER_SCE);

    // STAR:  [63:48] = sysret CS for ring 3, [47:32] = syscall CS (ring 0)
    //        [31:0]  = not used (legacy SYSCALL EIP)
    uint64_t star = ((uint64_t)USER_CS << 48) | ((uint64_t)KERNEL_CS << 32);
    write_msr(MSR_STAR, star);

    // LSTAR: RIP of syscall entry point. Must be the higher-half alias — a
    // ring-3 `syscall` runs under the user CR3, which maps kernel code only via
    // the PML4[511] mirror (KERNEL_BASE), not at its low link address. (The IDT
    // gates use the same +KERNEL_BASE aliasing for interrupts from ring 3.)
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry + KERNEL_BASE);

    // SF_MASK: RFLAGS bits to CLEAR on entry to the kernel. Every one of these is
    // ring-3 state that would otherwise survive into kernel code:
    //   TF (8)  single-step. A syscall made with TF set raised a #DB on the very
    //           first kernel instruction — a debug exception in ring 0, from an
    //           unprivileged program, at an attacker-chosen moment.
    //   IF (9)  interrupts, so entry is atomic until the kernel stack is set up.
    //   DF (10) direction. The counterpart of the `cld` now in SAVE_REGS: with DF
    //           set, memcpy_asm/memset_asm (bare rep movsb/stosb) run BACKWARDS.
    //   NT (14) nested task, which would corrupt an IRET's behaviour.
    //   AC (18) alignment check — and, more importantly, the flag that SUSPENDS
    //           SMAP. The kernel enables SMAP (v5.8.86) precisely so a stray
    //           ring-0 access to a user page traps; letting ring 3 hand us AC=1
    //           would switch that protection off for the whole syscall.
    write_msr(MSR_SF_MASK, (1 << 8) | (1 << 9) | (1 << 10) | (1 << 14) | (1 << 18));
}

// Provide kernel stack to user processes via the syscall_entry global variable

void set_kernel_rsp(uint64_t rsp) {
    kernel_rsp = rsp;
}

/* ------------------------------------------------------------------ */
/*  Ring-3 syscall boundary guards                                    */
/* ------------------------------------------------------------------ */
/* Canonical user space is the lower half: [USER_SPACE_MIN, USER_SPACE_END).
 * Anything at or above USER_SPACE_END is non-canonical or the higher-half
 * kernel. Rejecting those stops a ring-3 process from handing the kernel a
 * kernel address as a syscall buffer (which would be an arbitrary kernel
 * read/write or info-leak primitive). */

static int user_ptr_ok(uint64_t ptr, uint64_t len) {
    if (ptr < USER_SPACE_MIN) return 0;
    if (len > USER_SPACE_END) return 0;
    if (ptr + len < ptr) return 0;              /* wrap-around */
    if (ptr + len > USER_SPACE_END) return 0;
    return 1;
}

static int user_str_ok(uint64_t ptr) {
    /* String length is unknown here; validating the base pointer keeps a
     * higher-half kernel address from ever being dereferenced as a string. */
    return ptr >= USER_SPACE_MIN && ptr < USER_SPACE_END;
}

/* Per-process-agnostic fd table: ring 3 gets small integer fds and never sees
 * (or can forge) a raw kernel VFS handle. */
/* The fd table lives in process_t (see kernel.h) — one per process, so fds are
 * isolated per process and reaped with it (reap closes any still open). All of
 * these run inside a syscall (interrupts masked), so current_idx is stable.
 * The table is indexed by the fd itself (slots 0..PROC_MAX_FDS-1). Slots 0-2 are
 * the standard streams: normally NOT in use — writes to 1/2 fall back to the
 * console — but dup2() can install a pipe end there, which is how a pipeline
 * redirects a child's stdin/stdout. open()/pipe() allocate from UFD_BASE up. */
#define UFD_BASE 3

// poll(2) event/result bits — must match user/syscall.h.
#define POLLIN   0x001
#define POLLOUT  0x004
#define POLLNVAL 0x020
struct kpollfd { int fd; short events; short revents; };   // 8 bytes, matches userspace

/* The fd table belongs to the THREAD GROUP (v5.8.89): every CLONE_VM thread resolves
 * through tg_leader(), so an fd opened by one thread is visible to all of them and a
 * close by any of them closes it for the group — POSIX thread semantics. Only these
 * fd helpers redirect; things like getpid() must still answer for the calling THREAD,
 * so get_cur_proc() is deliberately left alone elsewhere. A thread's own table stays
 * empty, which is what makes close_proc_fds() at its exit a safe no-op. */
static process_t* fd_owner(void) { return tg_leader(get_cur_proc()); }

static int ufd_alloc(int internal) {
    process_t* p = fd_owner();
    if (!p) return -1;
    for (int i = UFD_BASE; i < PROC_MAX_FDS; i++) {
        if (!p->ufd_inuse[i]) {
            p->ufd_inuse[i] = 1; p->ufd_handle[i] = internal; p->ufd_offset[i] = 0;
            return i;
        }
    }
    return -1;
}
static int ufd_lookup(int ufd, int* internal) {
    process_t* p = fd_owner();
    if (!p || ufd < 0 || ufd >= PROC_MAX_FDS || !p->ufd_inuse[ufd]) return -1;
    *internal = p->ufd_handle[ufd];
    return 0;
}
/* Pointer to a live fd's byte offset (advanced by read/write), or NULL. */
static uint32_t* ufd_offset_of(int ufd) {
    process_t* p = fd_owner();
    if (!p || ufd < 0 || ufd >= PROC_MAX_FDS || !p->ufd_inuse[ufd]) return 0;
    return &p->ufd_offset[ufd];
}
static void ufd_release(int ufd) {
    process_t* p = fd_owner();
    if (p && ufd >= 0 && ufd < PROC_MAX_FDS) p->ufd_inuse[ufd] = 0;
}

/* ------------------------------------------------------------------ */
/*  stdin: keyboard -> fd 0 (canonical line discipline)               */
/* ------------------------------------------------------------------ */
/* A read() on an EMPTY fd 0 slot reads the keyboard: it drains the IRQ-fed
 * ASCII ring via getchar_poll(), echoing each key (putchar mirrors to the
 * terminal capture hook and the serial console) and handling backspace, and
 * returns when Enter arrives (the line includes the '\n'). The assembled line
 * lives in the caller's per-invocation kbuf, so the line discipline itself holds
 * no shared state. On a single core the ring is contention-free too, because the
 * compositor — its usual consumer — is parked in kwait() while a foreground
 * process runs; but under `smpbalance on` the two can run on different cores at
 * once, so the ring is now guarded by kbd_lock inside getchar_poll (v5.9.23).
 *
 * Blocking while the ring is empty uses the v5.8.2 mid-syscall discipline: the
 * caller stays PROC_RUN and sleeps one timeslice per `sti;hlt` (the timer keeps
 * scheduling it — no wakeup hook in the keyboard IRQ needed yet), with
 * `blocked_in_kernel` set so the scheduler resumes it on the KERNEL CR3, and
 * the shared user_cr3/user_rsp globals saved/restored around the loop. */
static int stdin_read_line(char* kbuf, int max) {
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    int len = 0;
    for (;;) {
        char c = getchar_poll();
        if (!c) {
            if (!self) break;                    /* no process context: don't block */
            if (signal_pending(self)) {          /* a signal (e.g. Ctrl-C) is waiting */
                __asm__ volatile("cli");
                if (signal_check_stop(self)) { __asm__ volatile("sti"); continue; }  /* Ctrl-Z: resumed */
                __asm__ volatile("sti");
                len = -EINTR;                    /* real signal: bail so the syscall returns + delivers */
                break;
            }
            self->blocked_in_kernel = 1;         /* resume us on the kernel CR3 */
            __asm__ volatile("sti; hlt");        /* yield until the next tick/IRQ */
            __asm__ volatile("cli");
            self->blocked_in_kernel = 0;
            continue;
        }
        if (c == '\r') c = '\n';
        if (c == '\b' || c == 0x7F) {            /* rub out the last echoed char */
            if (len > 0) {
                len--;
                putchar('\b'); putchar(' '); putchar('\b');
            }
            continue;
        }
        putchar(c);                              /* echo */
        if (len < max) kbuf[len++] = c;
        if (c == '\n') break;                    /* canonical: return on Enter */
    }
    user_cr3 = saved_cr3;
    user_rsp = saved_ursp;
    return len;
}

/* Raw stdin (SYS_TTYMODE raw): block until at least one key, then return what is
 * immediately available — single bytes, NO echo (the caller renders its own line).
 * Extended keys become ANSI escapes: ESC [ A/B/C/D (up/down/right/left), H/F
 * (home/end) — 3 bytes, emitted only if they fit. Blocking follows the same
 * discipline as stdin_read_line, and a pending signal interrupts with -EINTR. */
static int stdin_read_raw(char* kbuf, int max) {
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    int len = 0;
    for (;;) {
        int k = getkey_poll();               /* extended keycode, ASCII, or 0 */
        if (!k) {
            if (len > 0) break;              /* drained all that was pending */
            if (!self) break;
            if (signal_pending(self)) {
                __asm__ volatile("cli");
                if (signal_check_stop(self)) { __asm__ volatile("sti"); continue; }  /* Ctrl-Z: resumed */
                __asm__ volatile("sti");
                len = -EINTR; break;
            }
            self->blocked_in_kernel = 1;
            __asm__ volatile("sti; hlt");
            __asm__ volatile("cli");
            self->blocked_in_kernel = 0;
            continue;
        }
        if (k >= 0x80) {                     /* extended key -> ESC [ x */
            char x = 0;
            switch (k) {
                case KEY_UP:    x = 'A'; break;
                case KEY_DOWN:  x = 'B'; break;
                case KEY_RIGHT: x = 'C'; break;
                case KEY_LEFT:  x = 'D'; break;
                case KEY_HOME:  x = 'H'; break;
                case KEY_END:   x = 'F'; break;
                default: continue;           /* PgUp/PgDn/Ins/Del: dropped for now */
            }
            if (len + 3 > max) break;        /* no room for the full sequence */
            kbuf[len++] = 0x1B; kbuf[len++] = '['; kbuf[len++] = x;
            continue;
        }
        char c = (char)k;
        if (c == '\r') c = '\n';
        if (len < max) kbuf[len++] = c;
        if (len >= max) break;
    }
    user_cr3 = saved_cr3;
    user_rsp = saved_ursp;
    return len;
}

/* Timed single-key read (SYS_READKEY). Block up to `timeout_ms` for one key and
 * return it (an ASCII byte, or an extended keycode >= 0x80 for arrows etc.), 0 on
 * timeout, or -EINTR if a signal arrives. This is the primitive behind `top`'s
 * refresh-or-quit loop: a foreground TUI can wait a fixed interval for input and
 * fall through to redraw when nothing was pressed. Independent of tty_raw — it
 * reads keys directly via getkey_poll, no echo, no line discipline. */
static int stdin_readkey(uint32_t timeout_ms) {
    extern volatile uint32_t tick_count;         /* 1000 Hz -> milliseconds */
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    uint32_t deadline = tick_count + timeout_ms;  /* wrap-safe via signed compare */
    int result = 0;
    for (;;) {
        int k = getkey_poll();
        if (k) { result = k; break; }
        if (self && signal_pending(self)) {
            __asm__ volatile("cli");
            if (signal_check_stop(self)) { __asm__ volatile("sti"); continue; }  /* Ctrl-Z: resumed */
            __asm__ volatile("sti");
            result = -EINTR; break;
        }
        if (timeout_ms != 0 && (int32_t)(tick_count - deadline) >= 0) break;  /* timed out -> 0 (0 = block forever) */
        if (self) self->blocked_in_kernel = 1;
        __asm__ volatile("sti; hlt");
        __asm__ volatile("cli");
        if (self) self->blocked_in_kernel = 0;
    }
    user_cr3 = saved_cr3;
    user_rsp = saved_ursp;
    return result;
}

/* Block the caller for `ms` milliseconds (SYS_SLEEP). Uses the timer wait queue:
 * mark ourselves PROC_BLOCKED with a wake_tick and yield, so the scheduler runs
 * other threads and irq_scheduler_tick wakes us once tick_count reaches the
 * deadline — no busy spin. This is the kernel sleep() body plus the mid-syscall
 * discipline every blocking syscall follows: `blocked_in_kernel` so the scheduler
 * resumes us on the KERNEL CR3 (we park in ring 0), and the shared user_cr3/user_rsp
 * globals saved on entry / restored before return so the asm path iretq's back into
 * THIS process. A pending signal (e.g. Ctrl-C) cuts the sleep short with -EINTR. */
static int do_sleep_ms(uint32_t ms) {
    extern volatile uint32_t tick_count;         /* 1000 Hz -> milliseconds */
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    uint32_t deadline = tick_count + ms;         /* wrap-safe via signed compare */
    int result = 0;
    for (;;) {
        /* cli makes the deadline check + block atomic vs. the waking tick. */
        __asm__ volatile("cli");
        if ((int32_t)(tick_count - deadline) >= 0) { __asm__ volatile("sti"); break; }
        if (self && signal_pending(self)) {
            if (signal_check_stop(self)) continue;   /* Ctrl-Z: parked+resumed, keep sleeping */
            __asm__ volatile("sti"); result = -EINTR; break;
        }
        if (self) {
            self->blocked_in_kernel = 1;         /* resume us on the kernel CR3 */
            self->wake_tick = deadline;          /* irq_scheduler_tick wakes us here */
            self->state = PROC_BLOCKED;
        }
        __asm__ volatile("sti; hlt");            /* parked until the scheduler wakes us */
        __asm__ volatile("cli");
        if (self) {
            self->blocked_in_kernel = 0;
            self->wake_tick = 0;
            if (self->state == PROC_BLOCKED) self->state = PROC_RUN;
        }
        __asm__ volatile("sti");
    }
    user_cr3 = saved_cr3;
    user_rsp = saved_ursp;
    return result;
}

/* ------------------------------------------------------------------ */
/*  User memory access                                                */
/* ------------------------------------------------------------------ */
/* The handler runs on the kernel CR3, where user pages are NOT mapped. To touch
 * a user buffer we translate its virtual address through the user page tables
 * (whose table pages are identity-mapped physical memory) and access the
 * resulting physical page via the kernel's identity map. `user_cr3` is saved by
 * syscall_entry for the process that trapped. */

/* Physical-frame masks (bits 51:12 / 51:21 / 51:30). Must exclude bit 63 (NX)
 * and the low flag bits — masking with ~0xFFF alone keeps NX, producing a
 * non-canonical address that faults (#GP) when dereferenced. */
#define PT_ADDR_4K 0x000FFFFFFFFFF000ULL
#define PT_ADDR_2M 0x000FFFFFFFE00000ULL
#define PT_ADDR_1G 0x000FFFFFC0000000ULL

/* Non-static: do_futex (process.c) keys its wait queue by the word's physical address,
 * so every task sharing the page agrees on the key. */
/* SECURITY: this is THE choke point for turning a userspace address into a
 * physical one, so it is where the user/kernel boundary has to be enforced —
 * not at each caller.
 *
 * The hole this closes: alloc_page_directory() mirrors kernel_pml4[511] into
 * EVERY user PML4, and that entry is the identity map of all physical RAM.
 * KERNEL_BASE is 0xFFFFFF8000000000 == -2^39, so (KERNEL_BASE + X) >> 39 & 0x1FF
 * is exactly 511 and the walk below resolved KERNEL_BASE+X to physical X. Any
 * copy_to_user() whose destination was not separately validated was therefore an
 * arbitrary write into kernel memory — reachable from ring 3 through
 * signal_dispatch's user RSP, sigprocmask's oldset pointer, and others.
 *
 * Two independent guards, because one of them being bypassed should not be
 * enough:
 *   1. the address must lie in the canonical LOWER half (user space);
 *   2. every level of the walk must be marked PAGE_USER, so a mapping the
 *      hardware would refuse to ring 3 is refused here too.
 * Guard 2 is what keeps this correct if a kernel-only mapping ever appears in
 * the lower half, where guard 1 alone would let it through.
 */
uint64_t user_v2p(uint64_t vaddr) {
    if (!user_cr3) return 0;
    if (vaddr < USER_SPACE_MIN || vaddr >= USER_SPACE_END) return 0;

    uint64_t* pml4 = (uint64_t*)(user_cr3 & PT_ADDR_4K);
    uint64_t e = pml4[(vaddr >> 39) & 0x1FF];
    if ((e & 5) != 5) return 0;                 /* PRESENT | USER */
    uint64_t* pdpt = (uint64_t*)(e & PT_ADDR_4K);
    e = pdpt[(vaddr >> 30) & 0x1FF];
    if ((e & 5) != 5) return 0;
    if (e & 0x80) return (e & PT_ADDR_1G) + (vaddr & 0x3FFFFFFFULL);
    uint64_t* pd = (uint64_t*)(e & PT_ADDR_4K);
    e = pd[(vaddr >> 21) & 0x1FF];
    if ((e & 5) != 5) return 0;
    if (e & 0x80) return (e & PT_ADDR_2M) + (vaddr & 0x1FFFFFULL);
    uint64_t* pt = (uint64_t*)(e & PT_ADDR_4K);
    e = pt[(vaddr >> 12) & 0x1FF];
    if ((e & 5) != 5) return 0;
    return (e & PT_ADDR_4K) + (vaddr & 0xFFFULL);
}

static int copy_from_user(void* dst, uint64_t usrc, uint64_t len) {
    uint8_t* d = (uint8_t*)dst;
    for (uint64_t i = 0; i < len; i++) {
        uint64_t p = user_v2p(usrc + i);
        if (!p) return -1;
        d[i] = *(volatile uint8_t*)p;
    }
    return 0;
}

/* Exported (kernel.h): do_execve builds the new program's argv frame with this —
 * it points user_cr3 at the NEW page directory first, so the writes land in the
 * fresh address space's stack page via the identity map. */
int copy_to_user(uint64_t udst, const void* src, uint64_t len) {
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < len; i++) {
        uint64_t p = user_v2p(udst + i);
        if (!p) return -1;
        *(volatile uint8_t*)p = s[i];
    }
    return 0;
}

/* Copy a NUL-terminated user string into a kernel buffer (always terminated). */
static int copy_str_from_user(char* dst, uint64_t usrc, uint64_t maxlen) {
    for (uint64_t i = 0; i < maxlen; i++) {
        uint64_t p = user_v2p(usrc + i);
        if (!p) { dst[i ? i - 1 : 0] = '\0'; return -1; }
        char c = *(volatile char*)p;
        dst[i] = c;
        if (c == '\0') return 0;
    }
    dst[maxlen - 1] = '\0';
    return 0;
}

/* Resolve `path` (absolute, or relative to `cwd`) into a normalized absolute path
 * in `out`: collapses `.`, `..`, and redundant `/`. cwd must be absolute; an empty
 * cwd is treated as "/". This is what makes a process's CWD affect open()/getdents()
 * — a relative path is joined onto the caller's cwd before hitting the VFS. */
static void path_resolve(const char* cwd, const char* path, char* out, int outsz) {
    char buf[MAX_PATH];
    if (path[0] == '/') {
        strncpy(buf, path, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    } else {
        const char* c = (cwd && cwd[0]) ? cwd : "/";
        snprintf(buf, sizeof(buf), "%s/%s", c, path);
    }
    char* comps[32]; int nc = 0;                 // component stack (into buf)
    char* s = buf;
    while (*s) {
        while (*s == '/') s++;                    // skip runs of '/'
        if (!*s) break;
        char* start = s;
        while (*s && *s != '/') s++;
        if (*s) *s++ = '\0';
        if (start[0] == '.' && start[1] == '\0') continue;              // "."
        if (start[0] == '.' && start[1] == '.' && start[2] == '\0') {   // ".."
            if (nc > 0) nc--;
            continue;
        }
        if (nc < 32) comps[nc++] = start;
    }
    int o = 0;
    if (nc == 0) { if (outsz > 1) out[o++] = '/'; }
    else for (int i = 0; i < nc; i++) {
        if (o < outsz - 1) out[o++] = '/';
        for (char* p = comps[i]; *p && o < outsz - 1; p++) out[o++] = *p;
    }
    out[o] = '\0';
}

/* Copy the caller's cwd-resolved absolute path for a user path pointer into `out`. */
static int copy_path_from_user(char* out, int outsz, uint64_t uptr) {
    char rel[128];
    if (copy_str_from_user(rel, uptr, sizeof(rel)) != 0) return -1;
    process_t* cur = get_cur_proc();
    path_resolve(cur ? cur->cwd : "/", rel, out, outsz);
    return 0;
}

// Broken-down RTC time -> Unix epoch seconds. Civil-days algorithm (valid for any
// Gregorian date); the RTC carries no timezone, so it is treated as UTC. Backs
// SYS_GETTIMEOFDAY.
static int64_t rtc_to_epoch(const rtc_time_t* t) {
    int y = (int)t->year, m = (int)t->month, d = (int)t->day;
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);                      // [0, 399]
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;   // [0, 365]
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;     // [0, 146096]
    int64_t days = era * 146097 + (int64_t)doe - 719468; // days since 1970-01-01
    return days * 86400 + (int64_t)t->hour * 3600 + (int64_t)t->minute * 60 + (int64_t)t->second;
}

uint64_t syscall_handler(uint64_t no, uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    switch (no) {
        case SYS_EXIT: {
            printf("[USER] exit(%lu)\n", a1);
            // Every user process now runs under the scheduler, so exit is uniform:
            // become a zombie and yield forever. The scheduler skips non-RUN states,
            // so the next tick switches to another thread and never returns here;
            // reap_zombies() (a compositor background task) frees the address space +
            // stacks — we can't, we're still on this proc's kernel stack in its CR3.
            // Record the status and wake a parent blocked in kwait() (all with
            // interrupts masked, so it's atomic w.r.t. the parent).
            process_t* cur = get_cur_proc();
            if (cur) {
                cur->exit_code = (int)a1;
                // Close fds now (not just at reap) so pipe write ends drop and any
                // parent reading this process's output gets EOF — e.g. the shell's
                // `$(cmd)` capture, which reads the pipe before it reaps the subshell.
                close_proc_fds(cur);
                cur->state = PROC_ZOMBIE;
                wake_waiters(cur);
            }
            __asm__ volatile("sti");            // let the timer preempt us away
            for (;;) __asm__ volatile("hlt");
            return 0;   // unreachable
        }
        case SYS_WRITE: {
            int fd = (int)a1;
            int len = (int)a3;
            if (len < 0 || !user_ptr_ok(a2, (uint64_t)len)) return -1;
            /* The fd table wins over the console: dup2() can install a pipe end
             * at fd 1/2 (redirected stdout/stderr), which must route to the pipe.
             * Only an EMPTY 1/2 slot falls back to the console below. */
            int internal;
            if (ufd_lookup(fd, &internal) == 0) {
                if (internal & UFD_PIPE_FLAG) {                 /* pipe write */
                    if (!UFD_PIPE_IS_WRITE(internal)) return -1; /* read end isn't writable */
                    if (len > 4096) len = 4096;
                    char* pbuf = (char*)kmalloc(len);
                    if (!pbuf) return -1;
                    int n = (copy_from_user(pbuf, a2, len) == 0)
                                ? pipe_write(UFD_PIPE_ID(internal), pbuf, len) : -1;
                    kfree(pbuf);
                    return n;
                }
                if (internal & UFD_SOCK_FLAG) {                 /* socket write -> tcp_send */
                    if (len > 4096) len = 4096;
                    char* sbuf = (char*)kmalloc(len);
                    if (!sbuf) return -1;
                    int n = (copy_from_user(sbuf, a2, len) == 0)
                                ? nsock_send(UFD_SOCK_ID(internal), sbuf, len) : -1;
                    kfree(sbuf);
                    return n;
                }
                uint32_t* off = ufd_offset_of(fd);              /* VFS file write */
                if (len > 4096) len = 4096;
                char* kbuf = (char*)kmalloc(len);
                if (!kbuf) return -1;
                int n = (copy_from_user(kbuf, a2, len) == 0)
                            ? vfs_pwrite(internal, kbuf, len, off ? *off : 0) : -1;
                kfree(kbuf);
                if (n > 0 && off) *off += n;
                return n;
            }
            if (fd == 1 || fd == 2) {           /* stdout / stderr -> console */
                char kbuf[128];
                int done = 0;
                while (done < len) {
                    int chunk = len - done;
                    if (chunk > (int)sizeof(kbuf)) chunk = sizeof(kbuf);
                    if (copy_from_user(kbuf, a2 + done, chunk) != 0) return done;
                    for (int i = 0; i < chunk; i++) putchar(kbuf[i]);
                    done += chunk;
                }
                return len;
            }
            return -1;
        }
        case SYS_PRINT: {
            if (!user_str_ok(a1)) return -1;
            char kbuf[512];
            if (copy_str_from_user(kbuf, a1, sizeof(kbuf)) != 0) return -1;
            printf("%s", kbuf);
            return 0;
        }
        case SYS_OPEN: {
            if (!user_str_ok(a1)) return -1;
            char path[MAX_PATH];
            if (copy_path_from_user(path, sizeof(path), a1) != 0) return -1;  // cwd-relative
            int flags = (int)a2;
            int mode = (int)a3;
            int internal = vfs_open(path, flags, mode);
            if (internal < 0) return -1;
            int ufd = ufd_alloc(internal);
            if (ufd < 0) { vfs_close(internal); return -1; }  /* table full */
            if (flags & O_APPEND) {                           /* start writes at EOF */
                uint32_t* off = ufd_offset_of(ufd);
                int sz = vfs_fsize(internal);
                if (off) *off = (sz > 0) ? (uint32_t)sz : 0;
            }
            return ufd;
        }
        case SYS_READ: {
            int count = (int)a3;
            if (count < 0 || !user_ptr_ok(a2, (uint64_t)count)) return -1;
            if (count > 4096) count = 4096;
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) {
                /* An EMPTY fd 0 slot is stdin: a blocking, echoing line read from
                 * the keyboard. (A dup2'd fd 0 is in the table and routes below.) */
                if ((int)a1 == 0) {
                    if (count == 0) return 0;
                    char* lbuf = (char*)kmalloc(count);
                    if (!lbuf) return -1;
                    process_t* rp = get_cur_proc();
                    int n = (rp && rp->tty_raw) ? stdin_read_raw(lbuf, count)
                                                : stdin_read_line(lbuf, count);
                    if (n > 0 && copy_to_user(a2, lbuf, n) != 0) n = -1;
                    kfree(lbuf);
                    return n;
                }
                return -1;
            }
            if (internal & UFD_PIPE_FLAG) {                 /* pipe read — blocks if empty */
                if (UFD_PIPE_IS_WRITE(internal)) return -1;  /* the write end isn't readable */
                char* pbuf = (char*)kmalloc(count);
                if (!pbuf) return -1;
                int n = pipe_read(UFD_PIPE_ID(internal), pbuf, count);
                if (n > 0 && copy_to_user(a2, pbuf, n) != 0) n = -1;
                kfree(pbuf);
                return n;                                    /* 0 = EOF (all writers closed) */
            }
            if (internal & UFD_SOCK_FLAG) {                 /* socket read -> tcp_recv (blocks) */
                char* sbuf = (char*)kmalloc(count);
                if (!sbuf) return -1;
                int n = nsock_recv(UFD_SOCK_ID(internal), sbuf, count);
                if (n > 0 && copy_to_user(a2, sbuf, n) != 0) n = -1;
                kfree(sbuf);
                return n;                                    /* 0 = peer closed the connection */
            }
            uint32_t* off = ufd_offset_of((int)a1);
            char* kbuf = (char*)kmalloc(count);
            if (!kbuf) return -1;
            int n = vfs_pread(internal, kbuf, count, off ? *off : 0);
            if (n > 0 && copy_to_user(a2, kbuf, n) != 0) n = -1;
            if (n > 0 && off) *off += n;      /* advance for streaming reads */
            kfree(kbuf);
            return n;
        }
        case SYS_CLOSE: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            ufd_release((int)a1);
            if (internal & UFD_PIPE_FLAG) {                 /* pipe end -> drop a ref */
                pipe_close_end(UFD_PIPE_ID(internal), UFD_PIPE_IS_WRITE(internal));
                return 0;
            }
            if (internal & UFD_SOCK_FLAG) {                 /* socket -> tcp_close */
                nsock_close(UFD_SOCK_ID(internal));
                return 0;
            }
            return vfs_close(internal);
        }
        case SYS_SOCKET: {
            int s = nsock_create((int)a1, (int)a2, (int)a3);
            if (s < 0) return -1;
            int ufd = ufd_alloc(UFD_SOCK_MAKE(s));
            if (ufd < 0) { nsock_close(s); return -1; }     /* fd table full */
            return ufd;
        }
        case SYS_CONNECT: {
            /* connect(fd, ip, port): ip is a network-order IPv4 (low byte = first
             * octet, like net_interfaces[].ip), port is host order. Blocks until
             * the TCP handshake completes. */
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            if (!(internal & UFD_SOCK_FLAG)) return -1;
            return nsock_connect(UFD_SOCK_ID(internal), (uint32_t)a2, (uint16_t)a3);
        }
        case SYS_BIND: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            if (!(internal & UFD_SOCK_FLAG)) return -1;
            return nsock_bind(UFD_SOCK_ID(internal), (uint32_t)a2, (uint16_t)a3);
        }
        case SYS_LISTEN: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            if (!(internal & UFD_SOCK_FLAG)) return -1;
            return nsock_listen(UFD_SOCK_ID(internal), (int)a2);
        }
        case SYS_ACCEPT: {
            /* accept(fd): block until a client connects, returning a NEW fd for
             * that connection (usable with read/write/close); the listener fd
             * stays open for further clients. */
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            if (!(internal & UFD_SOCK_FLAG)) return -1;
            int cs = nsock_accept(UFD_SOCK_ID(internal));
            if (cs < 0) return -1;
            int ufd = ufd_alloc(UFD_SOCK_MAKE(cs));
            if (ufd < 0) { nsock_close(cs); return -1; }    /* fd table full */
            return ufd;
        }
        case SYS_SENDTO: {
            /* sendto(fd, buf, len, flags, ip, port): ip is network-order, port host. */
            int fd = (int)a1, len = (int)a3;
            if (len < 0 || !user_ptr_ok(a2, (uint64_t)len)) return -1;
            int internal;
            if (ufd_lookup(fd, &internal) != 0 || !(internal & UFD_SOCK_FLAG)) return -1;
            if (len > 4096) len = 4096;
            char* sbuf = (char*)kmalloc(len);
            if (!sbuf) return -1;
            int n = (copy_from_user(sbuf, a2, len) == 0)
                        ? nsock_sendto(UFD_SOCK_ID(internal), sbuf, len,
                                       (uint32_t)a5, (uint16_t)a6) : -1;
            kfree(sbuf);
            return n;
        }
        case SYS_RECVFROM: {
            /* recvfrom(fd, buf, len, flags, uint*ip, int*port): blocks for a datagram;
             * writes the sender's IP (4 bytes @a5) and port (4-byte int @a6) if given. */
            int fd = (int)a1, len = (int)a3;
            if (len < 0 || !user_ptr_ok(a2, (uint64_t)len)) return -1;
            int internal;
            if (ufd_lookup(fd, &internal) != 0 || !(internal & UFD_SOCK_FLAG)) return -1;
            if (len > 4096) len = 4096;
            char* rbuf = (char*)kmalloc(len);
            if (!rbuf) return -1;
            uint32_t src_ip = 0; uint16_t src_port = 0;
            int n = nsock_recvfrom(UFD_SOCK_ID(internal), rbuf, len, &src_ip, &src_port);
            if (n > 0 && copy_to_user(a2, rbuf, n) != 0) n = -1;
            kfree(rbuf);
            if (n > 0) {
                if (a5 && user_ptr_ok(a5, 4)) copy_to_user(a5, &src_ip, 4);
                if (a6 && user_ptr_ok(a6, 4)) { int p = (int)src_port; copy_to_user(a6, &p, 4); }
            }
            return n;
        }
        case SYS_GETPID: {
            process_t* cur = get_cur_proc();
            return cur ? cur->pid : 0;
        }
        case SYS_SBRK: {
            // Resolve to the thread-group leader: every CLONE_VM thread shares ONE heap,
            // so a sbrk from any of them must move — and be seen through — the same break.
            // Without this each thread bumped its own copy and they handed out overlapping
            // regions (multi-threaded malloc corruption).
            process_t* cur = tg_leader(get_cur_proc());
            if (!cur) return -1;
            int64_t inc = (int64_t)a1;
            uint64_t old_brk = cur->program_break;
            if (inc == 0) return old_brk;
            // Lazy heap: just move the break — no eager alloc/map. Pages inside the
            // resulting [heap_start, program_break) window fault in on first touch
            // (vm_handle_fault), so a big malloc that's only partly written costs
            // only the pages actually used.
            if (inc < 0) {                              // shrink, clamped at heap_start
                uint64_t dec = (uint64_t)(-inc);
                cur->program_break = (dec > old_brk - cur->heap_start)
                                         ? cur->heap_start : old_brk - dec;
                return old_brk;
            }
            uint64_t new_brk = old_brk + (uint64_t)inc;
            if (new_brk > SHARED_LIBC_BASE) return -1;  // keep the heap below the shared libc
            cur->program_break = new_brk;
            return old_brk;
        }
        case SYS_FSIZE: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            return vfs_fsize(internal);
        }
        case SYS_STAT: {
            /* stat(path, struct stat*) — statbuf = {u32 st_size, st_mode, st_ino}. */
            if (!user_str_ok(a1) || !user_ptr_ok(a2, 12)) return -1;
            char path[MAX_PATH];
            if (copy_path_from_user(path, sizeof(path), a1) != 0) return -1;
            uint32_t size = 0; int isdir = 0;
            if (vfs_stat(path, &size, &isdir) != 0) return -1;
            uint32_t sb[3] = { size, isdir ? (0x4000u | 0755u) : (0x8000u | 0644u), 0 };
            return copy_to_user(a2, sb, sizeof(sb)) == 0 ? 0 : -1;
        }
        case SYS_FSTAT: {
            if (!user_ptr_ok(a2, 12)) return -1;
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            uint32_t sb[3];
            if (internal & (UFD_PIPE_FLAG | UFD_SOCK_FLAG)) {   // pipe/socket: size 0, regular-ish
                sb[0] = 0; sb[1] = 0x8000u | 0644u; sb[2] = 0;
            } else {
                uint32_t size = 0; int isdir = 0;
                if (vfs_fstat(internal, &size, &isdir) != 0) return -1;
                sb[0] = size; sb[1] = isdir ? (0x4000u | 0755u) : (0x8000u | 0644u); sb[2] = 0;
            }
            return copy_to_user(a2, sb, sizeof(sb)) == 0 ? 0 : -1;
        }
        case SYS_LSEEK: {
            /* lseek(fd, offset, whence): 0=SET, 1=CUR, 2=END. Returns the new offset. */
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            if (internal & (UFD_PIPE_FLAG | UFD_SOCK_FLAG)) return -1;   // not seekable
            uint32_t* off = ufd_offset_of((int)a1);
            if (!off) return -1;
            int64_t offset = (int64_t)a2, noff;
            int whence = (int)a3;
            if (whence == 0)      noff = offset;
            else if (whence == 1) noff = (int64_t)*off + offset;
            else if (whence == 2) { int sz = vfs_fsize(internal); noff = (int64_t)(sz > 0 ? sz : 0) + offset; }
            else return -1;
            if (noff < 0) return -1;
            *off = (uint32_t)noff;
            return (int64_t)*off;
        }
        case SYS_GETPPID: {
            process_t* cur = get_cur_proc();
            return cur ? cur->ppid : 0;
        }
        case SYS_EXEC: {
            if (!user_str_ok(a1)) return -1;
            char path[128];
            if (copy_str_from_user(path, a1, sizeof(path)) != 0) return -1;
            int fd = vfs_open(path, 0, 0);      /* kernel-side handle, never exposed */
            if (fd < 0) return -1;
            uint32_t size = vfs_fsize(fd);
            uint8_t* data = vfs_fdata(fd);
            if (!data || size == 0) { vfs_close(fd); return -1; }
            uint8_t* copy = (uint8_t*)kmalloc(size);
            if (!copy) { vfs_close(fd); return -1; }
            memcpy_asm(copy, data, size);
            vfs_close(fd);
            if (!elf_validate(copy, size)) { kfree(copy); return -2; }
            process_t* new_proc = NULL;
            int ret = elf_load(copy, size, &new_proc);
            kfree(copy);
            if (ret != 0 || !new_proc) return -3;
            printf("[EXEC] Loaded %s as PID %lu\n", path, new_proc->pid);
            return (uint64_t)new_proc->pid;
        }
        case SYS_CLONE:
            // clone(fn, stack, arg, flags): with CLONE_VM this starts a THREAD — a
            // scheduled ring-3 task SHARING our address space (same PML4, no COW copy)
            // that runs fn(arg) on the caller-supplied stack with its own kernel stack.
            // Returns the new tid, or -1 (including when CLONE_VM is absent: use fork).
            return (uint64_t)(int64_t)do_clone(a1, a2, a3, a4);
        case SYS_FUTEX:
            // futex(uaddr, op, val): FUTEX_WAIT blocks while *uaddr == val (returning at
            // once if it already differs — that compare-and-sleep is what makes a lock
            // race-free); FUTEX_WAKE wakes up to `val` waiters and returns how many.
            return (uint64_t)(int64_t)do_futex(a1, (int)a2, (uint32_t)a3);
        case SYS_FORK:
            // COW-clone the caller. Returns the child's pid to the parent and 0 in
            // the child (baked into the child's resume frame by do_fork), -1 on error.
            return (uint64_t)(int64_t)do_fork();
        case SYS_WAITPID: {
            // waitpid(pid, status). do_waitpid BLOCKS until a matching child exits
            // (per-process syscall stacks make blocking mid-syscall safe), then
            // returns the reaped child's pid, or -1 if there is no such child. It
            // hands back on the kernel CR3 with our syscall globals restored, so the
            // copy_to_user of the exit status below is safe.
            int code = 0;
            int r = do_waitpid((int)a1, &code, (int)a3);   // a3 = options (WNOHANG)
            if (r > 0 && a2) {
                if (!user_ptr_ok(a2, sizeof(int))) return -1;
                copy_to_user(a2, &code, sizeof(int));
            }
            return (uint64_t)(int64_t)r;
        }
        case SYS_PIPE: {
            // pipe(int fds[2]): fds[0] = read end, fds[1] = write end. Returns 0, or -1.
            if (!user_ptr_ok(a1, 2 * sizeof(int))) return -1;
            int id = pipe_new();
            if (id < 0) return -1;
            int rfd = ufd_alloc(UFD_PIPE_MAKE(id, 0));
            int wfd = ufd_alloc(UFD_PIPE_MAKE(id, 1));
            int fds[2] = { rfd, wfd };
            if (rfd < 0 || wfd < 0 || copy_to_user(a1, fds, sizeof(fds)) != 0) {
                if (rfd >= 0) ufd_release(rfd);
                if (wfd >= 0) ufd_release(wfd);
                pipe_close_end(id, 0);                       // drop both refs -> free the pipe
                pipe_close_end(id, 1);
                return -1;
            }
            return 0;
        }
        case SYS_EXECVE: {
            // execve(path, argv, envp): replace the caller's image with the ELF at
            // `path`, passing argv AND envp onto the new program's entry stack (SysV
            // layout, read by crt0). Both are NULL-terminated user arrays of user
            // string pointers — copied into kernel buffers HERE, while the old address
            // space still exists (do_execve destroys it before building the new
            // stack). Limits: 32 args of 63 chars, 16 env entries of 159 chars. On
            // success the syscall returns into the new program; on failure -1.
            if (!user_str_ok(a1)) return -1;
            char path[128];
            if (copy_str_from_user(path, a1, sizeof(path)) != 0) return -1;
            static char kargv_store[32][64];   /* safe: syscalls serialized, no block */
            char* kargv[32];
            int argc = 0;
            if (a2) {
                for (; argc < 32; argc++) {
                    uint64_t uptr = 0;
                    if (copy_from_user(&uptr, a2 + (uint64_t)argc * 8, 8) != 0) return -1;
                    if (!uptr) break;                    /* NULL terminator */
                    if (!user_str_ok(uptr)) return -1;
                    if (copy_str_from_user(kargv_store[argc], uptr, 64) != 0) return -1;
                    kargv[argc] = kargv_store[argc];
                }
            }
            static char kenvp_store[16][160];  /* NAME=VALUE strings, copied in like argv */
            char* kenvp[16];
            int envc = 0;
            if (a3) {
                for (; envc < 16; envc++) {
                    uint64_t uptr = 0;
                    if (copy_from_user(&uptr, a3 + (uint64_t)envc * 8, 8) != 0) return -1;
                    if (!uptr) break;                    /* NULL terminator */
                    if (!user_str_ok(uptr)) return -1;
                    if (copy_str_from_user(kenvp_store[envc], uptr, 160) != 0) return -1;
                    kenvp[envc] = kenvp_store[envc];
                }
            }
            int fd = vfs_open(path, 0, 0);
            if (fd < 0) return -1;
            uint32_t sz = vfs_fsize(fd);
            uint8_t* fdata = vfs_fdata(fd);
            if (!fdata || sz == 0) { vfs_close(fd); return -1; }
            uint8_t* copy = (uint8_t*)kmalloc(sz);
            if (!copy) { vfs_close(fd); return -1; }
            memcpy_asm(copy, fdata, sz);
            vfs_close(fd);
            int r = do_execve(copy, sz, kargv, argc, kenvp, envc, path);   // success -> returns into new image
            kfree(copy);
            return (uint64_t)(int64_t)r;
        }
        case SYS_DUP2: {
            // dup2(oldfd, newfd): make newfd refer to oldfd's stream, closing whatever
            // newfd held. The redirection primitive — dup2(pipefd[1], 1) wires a pipe,
            // dup2(file_fd, 1) redirects stdout to a file. PIPE ends are reference-
            // counted, so they DUPLICATE (oldfd stays valid). VFS file handles aren't
            // refcounted, so they MOVE (oldfd is cleared) — that way the shell's
            // `dup2(fd,1); close(fd)` leaves exactly one owner and never double-closes.
            int oldfd = (int)a1, newfd = (int)a2;
            process_t* p = fd_owner();          /* the thread group's shared fd table */
            int internal;
            if (!p || ufd_lookup(oldfd, &internal) != 0) return -1;
            if (newfd < 0 || newfd >= PROC_MAX_FDS) return -1;
            if (newfd == oldfd) return newfd;
            if (p->ufd_inuse[newfd]) {                        /* implicitly close newfd */
                int old = p->ufd_handle[newfd];
                if (old & UFD_PIPE_FLAG) pipe_close_end(UFD_PIPE_ID(old), UFD_PIPE_IS_WRITE(old));
                else                     vfs_close(old);
                p->ufd_inuse[newfd] = 0;
            }
            if (internal & UFD_PIPE_FLAG) {                   /* pipe end -> duplicate (incref) */
                pipe_incref(UFD_PIPE_ID(internal), UFD_PIPE_IS_WRITE(internal));
                p->ufd_inuse[newfd]  = 1;
                p->ufd_handle[newfd] = internal;
                p->ufd_offset[newfd] = 0;
            } else {                                          /* VFS file -> move to newfd */
                p->ufd_inuse[newfd]  = 1;
                p->ufd_handle[newfd] = internal;
                p->ufd_offset[newfd] = p->ufd_offset[oldfd]; /* carry offset (append) */
                p->ufd_inuse[oldfd]  = 0;                     /* clear oldfd: it's moved */
            }
            return newfd;
        }
        case SYS_DUP: {
            // dup(oldfd): return the lowest-available fd that refers to the same
            // stream as oldfd — both stay open (unlike dup2's move for VFS files).
            // Pipe ends are refcounted so we incref; VFS/socket handles aren't, so
            // the new fd just aliases the same handle (vfs_close is a no-op for the
            // ramdisk nodes ring 3 opens, so closing either fd later is safe).
            int oldfd = (int)a1;
            process_t* p = fd_owner();          /* the thread group's shared fd table */
            int internal;
            if (!p || ufd_lookup(oldfd, &internal) != 0) return -1;
            int newfd = ufd_alloc(internal);          /* lowest free slot (>= UFD_BASE) */
            if (newfd < 0) return -1;                  /* fd table full */
            if (internal & UFD_PIPE_FLAG)
                pipe_incref(UFD_PIPE_ID(internal), UFD_PIPE_IS_WRITE(internal));
            else if (!(internal & UFD_SOCK_FLAG))
                vfs_dup(internal);   /* mount-backed mirrors need the alias counted */
            p->ufd_offset[newfd] = p->ufd_offset[oldfd];   /* start at the same offset */
            return newfd;
        }
        case SYS_RENAME: {
            // rename(oldpath, newpath): move/rename within the VFS (both cwd-relative).
            // Wraps the hardened vfs_rename; refuses a missing source and confirms the
            // destination exists afterward.
            if (!user_str_ok(a1) || !user_str_ok(a2)) return -1;
            char oldp[MAX_PATH], newp[MAX_PATH];
            if (copy_path_from_user(oldp, sizeof(oldp), a1) != 0) return -1;
            if (copy_path_from_user(newp, sizeof(newp), a2) != 0) return -1;
            if (vfs_stat(oldp, 0, 0) != 0) return -1;         /* source must exist */
            vfs_rename(oldp, newp);
            return (vfs_stat(newp, 0, 0) == 0) ? 0 : -1;      /* did the move land? */
        }
        case SYS_GETDENTS: {
            // getdents(path, buf, max): enumerate directory `path`, writing up to
            // `max` fixed 68-byte records { char name[64]; u32 type } into the user
            // buffer. Returns the number written, or -1. The whole vfs_open/readdir/
            // close cycle runs here so ring 3 never holds a raw VFS handle. This is
            // the enumeration primitive behind `ls`.
            //
            // NOTE (lazy sbrk): copy_to_user walks the *user* page tables and cannot
            // fault a not-yet-present heap page in — a malloc'd buffer whose later
            // pages have never been touched would fail the copy. `ls` memset()s its
            // buffer first (touching every page); if a copy still fails here we stop
            // and return the records written so far rather than -1, so a caller that
            // forgets gets a truncated-but-valid listing.
            if (!user_str_ok(a1)) return -1;
            char path[MAX_PATH];
            if (copy_path_from_user(path, sizeof(path), a1) != 0) return -1;  // cwd-relative
            int max = (int)a3;
            if (max <= 0) return -1;
            if (max > 256) max = 256;                    // sanity clamp
            struct { char name[64]; uint32_t type; } rec; // must match user nyx_dirent_t
            const uint64_t recsz = sizeof(rec);           // 68 (u32 tail, no padding)
            if (!user_ptr_ok(a2, (uint64_t)max * recsz)) return -1;
            int fd = vfs_open(path, 0, 0);
            if (fd < 0) return -1;
            int count = 0;
            for (dirent_t* de = vfs_readdir(fd); de && count < max; de = vfs_readdir(fd)) {
                int i = 0;
                for (; i < 63 && de->name[i]; i++) rec.name[i] = de->name[i];
                rec.name[i] = '\0';
                rec.type = de->type;
                if (copy_to_user(a2 + (uint64_t)count * recsz, &rec, recsz) != 0)
                    break;                                // untouched lazy page: stop, keep what we have
                count++;
            }
            vfs_close(fd);
            return count;
        }
        case SYS_KILL:
            // kill(pid, sig): post signal `sig` to process `pid` (sig 0 = existence
            // probe). Delivery happens when the target next returns to ring 3.
            return (uint64_t)(int64_t)do_kill((int)a1, (int)a2);
        case SYS_SIGNAL:
            // signal(sig, handler, trampoline): set the disposition of `sig`
            // (SIG_DFL/SIG_IGN or a ring-3 handler VA) and record the sigreturn
            // trampoline. Returns the previous disposition, or -1.
            return (uint64_t)do_signal((int)a1, a2, a3);
        case SYS_SIGRETURN: {
            // Invoked by the ring-3 trampoline after a handler returns. do_sigreturn
            // restores the pre-handler context into this syscall's frame; we return
            // the restored RAX so the entry stub's post-write leaves it intact.
            do_sigreturn();
            uint64_t* f = (uint64_t*)syscall_frame_ptr;
            return f ? f[14] : 0;
        }
        case SYS_SIGPROCMASK:
            // sigprocmask(how, set, oldset): read/change the blocked-signal mask.
            // The mechanism behind sigsetjmp/siglongjmp restoring the mask on a
            // non-local jump out of a handler (so repeated faults stay catchable).
            return (uint64_t)do_sigprocmask((int)a1, a2, a3);
        case SYS_ALARM:
            // alarm(seconds): schedule SIGALRM after N seconds (0 cancels); returns
            // the seconds left on any previous alarm. Fires from irq_scheduler_tick.
            return (uint64_t)do_alarm((unsigned int)a1);
        case SYS_POLL: {
            // poll(struct pollfd* fds, int nfds, int timeout_ms). Blocks until a
            // listed fd is ready (POLLIN = readable, POLLOUT = writable — always
            // ready here), the timeout elapses (0 = non-blocking, <0 = forever), or
            // a signal arrives (-EINTR). Returns the count of ready fds, or 0.
            int nfds = (int)a2, timeout = (int)a3;
            if (nfds < 0 || nfds > 64) return -1;
            if (nfds > 0 && !user_ptr_ok(a1, (uint64_t)nfds * 8)) return -1;
            struct kpollfd kfds[64];
            if (nfds > 0 && copy_from_user(kfds, a1, (uint64_t)nfds * 8) != 0) return -1;
            extern volatile uint32_t tick_count;
            process_t* self = get_cur_proc();
            // poll re-enables interrupts to spin (the `sti` below), so the timer can
            // preempt us and run ANOTHER process, whose syscall overwrites the shared
            // user_cr3/user_rsp globals (isr_stubs.asm). Save them now and restore
            // before any copy_to_user / return — exactly the discipline every other
            // interrupt-enabling syscall follows (stdin_read_line, do_sleep_ms,
            // stdin_readkey, pipe/socket reads, do_waitpid). Without it, copy_to_user
            // and the asm return path translate through the FOREIGN address space,
            // wild-writing a live page of an unrelated process (issue #20).
            uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
            uint32_t start = tick_count;
            for (;;) {
                __asm__ volatile("cli");        // atomic readiness snapshot vs. the timer/NIC IRQ
                int ready = 0;
                for (int i = 0; i < nfds; i++) {
                    kfds[i].revents = 0;
                    int fd = kfds[i].fd;
                    short ev = kfds[i].events, rv = 0;
                    if (fd < 0) continue;                          // negative fd: ignored (POSIX)
                    int internal;
                    if (ufd_lookup(fd, &internal) != 0) {          // not in the fd table
                        if (fd == 0) {                             // stdin
                            if ((ev & POLLIN) && keyboard_has_input()) rv |= POLLIN;
                        } else if (fd == 1 || fd == 2) {           // stdout/stderr: always writable
                            if (ev & POLLOUT) rv |= POLLOUT;
                        } else {
                            rv = POLLNVAL;                          // bad fd
                        }
                    } else if (internal & UFD_PIPE_FLAG) {
                        if ((ev & POLLIN) && !UFD_PIPE_IS_WRITE(internal)
                            && pipe_readable(UFD_PIPE_ID(internal))) rv |= POLLIN;
                        if ((ev & POLLOUT) && UFD_PIPE_IS_WRITE(internal)) rv |= POLLOUT;
                    } else if (internal & UFD_SOCK_FLAG) {
                        if ((ev & POLLIN) && nsock_readable(UFD_SOCK_ID(internal))) rv |= POLLIN;
                        if (ev & POLLOUT) rv |= POLLOUT;           // sockets treated always-writable
                    } else {                                       // VFS file: never blocks
                        if (ev & POLLIN)  rv |= POLLIN;
                        if (ev & POLLOUT) rv |= POLLOUT;
                    }
                    kfds[i].revents = rv;
                    if (rv) ready++;
                }
                int timed_out = (timeout == 0) ||
                    (timeout > 0 && (int32_t)(tick_count - (start + (uint32_t)timeout)) >= 0);
                if (ready > 0 || timed_out) {
                    // Still masked from the loop-top cli: restore OUR globals, copy,
                    // and return without re-enabling interrupts, so the copy_to_user
                    // and the asm return path can't be preempted into a foreign CR3.
                    user_cr3 = saved_cr3; user_rsp = saved_ursp;
                    if (nfds > 0) copy_to_user(a1, kfds, (uint64_t)nfds * 8);
                    return (uint64_t)ready;                        // ready count, or 0 on timeout
                }
                if (self && signal_pending(self)) { user_cr3 = saved_cr3; user_rsp = saved_ursp; return (uint64_t)(-EINTR); }
                // Enable IRQs so the 1000 Hz timer advances tick_count (the timeout);
                // syscalls run with interrupts masked, so without this the deadline
                // would never pass. Then drive the sockets and delay before re-checking.
                __asm__ volatile("sti");
                kernel_poll_net();
                for (volatile int d = 0; d < 1500; d++) inb(0x80);
            }
        }
        case SYS_MMAP: {
            // mmap(addr, length, prot, flags, fd, offset). Anonymous (MAP_ANONYMOUS)
            // demand-faults to zero; otherwise file-backed — resolve fd (a5) to a VFS
            // handle here and snapshot the file (from `offset`, a6) in do_mmap.
            int flags = (int)a4;
            int fh = 0; uint32_t fsz = 0;
            if (!(flags & MAP_ANONYMOUS)) {
                int internal;
                if (ufd_lookup((int)a5, &internal) != 0) return (uint64_t)-1;  // bad fd
                if (internal & UFD_PIPE_FLAG) return (uint64_t)-1;             // no pipe mmap
                fh = internal;
                fsz = (uint32_t)vfs_fsize(internal);
            }
            return do_mmap(a1, a2, (int)a3, flags, fh, fsz, (uint32_t)a6);
        }
        case SYS_MUNMAP:
            return (uint64_t)(int64_t)do_munmap(a1, a2);
        case SYS_MPROTECT:
            // mprotect(addr, length, prot): change protection of a mapped range.
            return (uint64_t)(int64_t)do_mprotect(a1, a2, (int)a3);
        case SYS_CHDIR: {
            // chdir(path): set the process CWD (relative paths resolve against the
            // current one). Rejects a path that isn't a directory. Returns 0 or -1.
            if (!user_str_ok(a1)) return -1;
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            char abspath[MAX_PATH];
            if (copy_path_from_user(abspath, sizeof(abspath), a1) != 0) return -1;
            if (!vfs_isdir(abspath)) return -1;
            strncpy(cur->cwd, abspath, sizeof(cur->cwd) - 1);
            cur->cwd[sizeof(cur->cwd) - 1] = '\0';
            return 0;
        }
        case SYS_GETCWD: {
            // getcwd(buf, size): copy the process CWD out. Returns the length (excl.
            // NUL), or -1 if the buffer is too small or invalid.
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            const char* cwd = cur->cwd[0] ? cur->cwd : "/";
            int len = (int)strlen(cwd) + 1;
            int sz = (int)a2;
            if (sz < len || !user_ptr_ok(a1, (uint64_t)len)) return -1;
            if (copy_to_user(a1, cwd, len) != 0) return -1;
            return (uint64_t)(len - 1);
        }
        case SYS_MKDIR: {
            // mkdir(path, mode): create a directory (cwd-relative path). Fails if
            // the parent doesn't exist or the name is taken. Returns 0 or -1.
            if (!user_str_ok(a1)) return -1;
            char path[MAX_PATH];
            if (copy_path_from_user(path, sizeof(path), a1) != 0) return -1;
            // The parent must already exist as a directory: vfs_mkdir would otherwise
            // create the FIRST missing component of a deep path (resolve_parent hands
            // it the deepest existing dir), which ring 3 must not rely on.
            char parent[MAX_PATH];
            strncpy(parent, path, sizeof(parent) - 1);
            parent[sizeof(parent) - 1] = '\0';
            char* slash = parent;
            for (char* q = parent; *q; q++) if (*q == '/') slash = q;
            if (slash == parent) parent[1] = '\0';       // parent is "/"
            else *slash = '\0';
            if (!vfs_isdir(parent)) return -1;
            return (uint64_t)(int64_t)vfs_mkdir(path, (int)a2);
        }
        case SYS_TTYMODE: {
            // ttymode(mode): TTY_CANON = kernel line discipline (echo + backspace),
            // TTY_RAW = byte-at-a-time, no echo, arrows as ANSI escapes. Per-process;
            // execve resets to canonical. Returns the previous mode.
            process_t* cur = get_cur_proc();
            if (!cur) return -1;
            uint32_t prev = cur->tty_raw;
            if (a1 == TTY_CANON || a1 == TTY_RAW) cur->tty_raw = (uint32_t)a1;
            else return -1;
            return prev;
        }
        case SYS_UNLINK: {
            // unlink(path): remove a file (or empty-dir semantics of vfs_unlink —
            // the same primitive the kernel shell's `rm` uses). cwd-relative.
            if (!user_str_ok(a1)) return -1;
            char path[MAX_PATH];
            if (copy_path_from_user(path, sizeof(path), a1) != 0) return -1;
            return (uint64_t)(int64_t)vfs_unlink(path);
        }
        case SYS_GETPROCS: {
            // getprocs(buf, max): snapshot the process table into up to `max`
            // fixed 48-byte records { u32 pid, ppid, state, cpu_time; char
            // comm[32] }. Returns the number written. This is the enumeration
            // primitive behind `ps` — the process analogue of SYS_GETDENTS.
            // Empty slots and any pid-0 (uninitialised) entry are skipped, so
            // ring 3 sees only live processes. Interrupts are masked for the
            // whole syscall, so the table can't be reshaped under the walk.
            int max = (int)a2;
            if (max <= 0) return -1;
            if (max > MAX_PROCESSES) max = MAX_PROCESSES;      // sanity clamp
            struct { uint32_t pid, ppid, state, cpu_time; char comm[32]; } rec;
            const uint64_t recsz = sizeof(rec);                // 48, must match user nyx_procinfo_t
            if (!user_ptr_ok(a1, (uint64_t)max * recsz)) return -1;
            extern process_t* process_table[MAX_PROCESSES];
            extern int process_count;
            int count = 0;
            for (int i = 0; i < process_count && count < max; i++) {
                process_t* p = process_table[i];
                if (!p || p->pid == 0) continue;
                rec.pid = p->pid;
                rec.ppid = p->ppid;
                rec.state = p->state;
                rec.cpu_time = p->cpu_time;
                int j = 0;
                for (; j < 31 && p->comm[j]; j++) rec.comm[j] = p->comm[j];
                rec.comm[j] = '\0';
                if (copy_to_user(a1 + (uint64_t)count * recsz, &rec, recsz) != 0)
                    break;                                     // untouched lazy page: stop, keep what we have
                count++;
            }
            return count;
        }
        case SYS_READKEY:
            // readkey(timeout_ms): block up to timeout_ms for one key; return it,
            // 0 on timeout, or -EINTR on a signal. The timed-input primitive `top`
            // uses to auto-refresh while staying responsive to 'q'.
            return (uint64_t)(int64_t)stdin_readkey((uint32_t)a1);
        case SYS_DLOPEN: {
            // dlopen(path): load the shared object `path` and map it into this
            // process on demand; returns a handle (>=1), or -1. cwd-relative path.
            if (!user_str_ok(a1)) return (uint64_t)-1;
            char path[MAX_PATH];
            if (copy_path_from_user(path, sizeof(path), a1) != 0) return (uint64_t)-1;
            return (uint64_t)(int64_t)do_dlopen(path);
        }
        case SYS_DLSYM: {
            // dlsym(handle, name): resolve `name` in the dlopen'd library to its
            // address, or 0. The caller casts it to a function/data pointer.
            if (!user_str_ok(a2)) return 0;
            char name[128];
            if (copy_str_from_user(name, a2, sizeof(name)) != 0) return 0;
            return do_dlsym((long)a1, name);
        }
        case SYS_TIME: {
            // time(buf): read the real-time clock and write the broken-down local
            // time into the user's 6-int buffer {sec,min,hour,mday,mon,year}. Ints
            // (not the kernel's packed rtc_time_t) give ring 3 a stable, padding-free
            // ABI — the primitive behind `date`. Returns 0, or -1 on a bad pointer.
            int t[6];
            if (!user_ptr_ok(a1, sizeof(t))) return -1;
            rtc_time_t rt;
            rtc_read_time(&rt);
            t[0] = rt.second; t[1] = rt.minute; t[2] = rt.hour;
            t[3] = rt.day;    t[4] = rt.month;  t[5] = rt.year;
            if (copy_to_user(a1, t, sizeof(t)) != 0) return -1;
            return 0;
        }
        case SYS_SLEEP:
            // sleep_ms(ms): block the caller for `ms` milliseconds on the timer wait
            // queue (the scheduler runs others meanwhile). Returns 0, or -EINTR if a
            // signal interrupted the wait. The primitive behind `sleep`.
            return (uint64_t)(int64_t)do_sleep_ms((uint32_t)a1);
        case SYS_SETFG: {
            // setfg(pid): make `pid` the terminal foreground process, so keyboard
            // signals (Ctrl-C -> SIGINT, Ctrl-Z -> SIGTSTP) target it. The shell
            // points this at its current foreground child while it runs, then back at
            // itself — the mechanism behind job control. Only a live pid (or 0 to
            // clear) is accepted. Returns 0, or -1.
            extern uint32_t g_foreground_pid;
            if (a1 != 0 && !find_process((uint32_t)a1)) return -1;
            g_foreground_pid = (uint32_t)a1;
            return 0;
        }
        case SYS_GETTIMEOFDAY: {
            // gettimeofday(struct timeval* tv, void* tz): wall-clock time since the
            // Unix epoch as two longs {tv_sec, tv_usec}. Seconds come from the RTC
            // (treated as UTC); microseconds from the 1000 Hz tick within the current
            // second — the finest the timer resolves. tz (a2) is obsolete and ignored.
            // Returns 0, or -1 on a bad pointer.
            if (a1) {
                extern volatile uint32_t tick_count;
                long tv[2];
                if (!user_ptr_ok(a1, sizeof(tv))) return -1;
                rtc_time_t rt;
                rtc_read_time(&rt);
                tv[0] = (long)rtc_to_epoch(&rt);
                tv[1] = (long)((tick_count % 1000) * 1000);
                if (copy_to_user(a1, tv, sizeof(tv)) != 0) return -1;
            }
            return 0;
        }
        case SYS_NANOSLEEP: {
            // nanosleep(const struct timespec* req, struct timespec* rem): sub-second
            // sleep. req is two longs {tv_sec, tv_nsec}; converted to milliseconds (the
            // timer's resolution — any sub-ms remainder rounds up to 1 ms) and blocked
            // on the timer wait queue via do_sleep_ms. rem (a2, may be NULL) receives
            // the time left if a signal cuts the sleep short; we don't track a precise
            // remainder, so it is zeroed. Returns 0, -EINTR if interrupted, or -1 on a
            // bad pointer / invalid timespec.
            long req[2];
            if (!user_ptr_ok(a1, sizeof(req))) return -1;
            if (copy_from_user(req, a1, sizeof(req)) != 0) return -1;
            if (req[0] < 0 || req[1] < 0 || req[1] >= 1000000000L) return -1;   // EINVAL
            uint64_t ms = (uint64_t)req[0] * 1000 + ((uint64_t)req[1] + 999999) / 1000000;
            if (ms > 0xFFFFFFFFULL) ms = 0xFFFFFFFFULL;
            int r = do_sleep_ms((uint32_t)ms);
            if (a2 && user_ptr_ok(a2, sizeof(req))) {
                long rem[2] = { 0, 0 };
                copy_to_user(a2, rem, sizeof(rem));
            }
            return (uint64_t)(int64_t)r;
        }
        default:
            printf("[SYSCALL] Unknown syscall %lu\n", no);
            return -1;
    }
}

void* get_syscall_table(void) {
    return syscall_table;
}

void register_syscall(uint32_t num, void* handler) {
    if (num < SYS_TABLE_SIZE) syscall_table[num] = handler;
}
