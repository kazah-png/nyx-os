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

    // SF_MASK: clear IF (bit 9) and DF (bit 10) during syscall
    write_msr(MSR_SF_MASK, (1 << 9) | (1 << 10));
}

// Provide kernel stack to user processes via the syscall_entry global variable
extern uint64_t user_rsp;
extern uint64_t kernel_rsp;

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
#define USER_SPACE_MIN 0x1000ULL
#define USER_SPACE_END 0x0000800000000000ULL

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

static int ufd_alloc(int internal) {
    process_t* p = get_cur_proc();
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
    process_t* p = get_cur_proc();
    if (!p || ufd < 0 || ufd >= PROC_MAX_FDS || !p->ufd_inuse[ufd]) return -1;
    *internal = p->ufd_handle[ufd];
    return 0;
}
/* Pointer to a live fd's byte offset (advanced by read/write), or NULL. */
static uint32_t* ufd_offset_of(int ufd) {
    process_t* p = get_cur_proc();
    if (!p || ufd < 0 || ufd >= PROC_MAX_FDS || !p->ufd_inuse[ufd]) return 0;
    return &p->ufd_offset[ufd];
}
static void ufd_release(int ufd) {
    process_t* p = get_cur_proc();
    if (p && ufd >= 0 && ufd < PROC_MAX_FDS) p->ufd_inuse[ufd] = 0;
}

/* ------------------------------------------------------------------ */
/*  stdin: keyboard -> fd 0 (canonical line discipline)               */
/* ------------------------------------------------------------------ */
/* A read() on an EMPTY fd 0 slot reads the keyboard: it drains the IRQ-fed
 * ASCII ring via getchar_poll(), echoing each key (putchar mirrors to the
 * terminal capture hook and the serial console) and handling backspace, and
 * returns when Enter arrives (the line includes the '\n'). This is safe from
 * contention because the compositor — the ring's usual consumer — is parked in
 * kwait() while a foreground process runs.
 *
 * Blocking while the ring is empty uses the v5.8.2 mid-syscall discipline: the
 * caller stays PROC_RUN and sleeps one timeslice per `sti;hlt` (the timer keeps
 * scheduling it — no wakeup hook in the keyboard IRQ needed yet), with
 * `blocked_in_kernel` set so the scheduler resumes it on the KERNEL CR3, and
 * the shared user_cr3/user_rsp globals saved/restored around the loop. */
static int stdin_read_line(char* kbuf, int max) {
    extern uint64_t user_cr3, user_rsp;
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    int len = 0;
    for (;;) {
        char c = getchar_poll();
        if (!c) {
            if (!self) break;                    /* no process context: don't block */
            if (signal_pending(self)) {          /* a signal (e.g. Ctrl-C) is waiting */
                len = -EINTR;                    /* bail so the syscall returns + delivers */
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
    extern uint64_t user_cr3, user_rsp;
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    int len = 0;
    for (;;) {
        int k = getkey_poll();               /* extended keycode, ASCII, or 0 */
        if (!k) {
            if (len > 0) break;              /* drained all that was pending */
            if (!self) break;
            if (signal_pending(self)) { len = -EINTR; break; }
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
    extern uint64_t user_cr3, user_rsp;
    process_t* self = get_cur_proc();
    uint64_t saved_cr3 = user_cr3, saved_ursp = user_rsp;
    uint32_t deadline = tick_count + timeout_ms;  /* wrap-safe via signed compare */
    int result = 0;
    for (;;) {
        int k = getkey_poll();
        if (k) { result = k; break; }
        if (self && signal_pending(self)) { result = -EINTR; break; }
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

/* ------------------------------------------------------------------ */
/*  User memory access                                                */
/* ------------------------------------------------------------------ */
/* The handler runs on the kernel CR3, where user pages are NOT mapped. To touch
 * a user buffer we translate its virtual address through the user page tables
 * (whose table pages are identity-mapped physical memory) and access the
 * resulting physical page via the kernel's identity map. `user_cr3` is saved by
 * syscall_entry for the process that trapped. */
extern uint64_t user_cr3;

/* Physical-frame masks (bits 51:12 / 51:21 / 51:30). Must exclude bit 63 (NX)
 * and the low flag bits — masking with ~0xFFF alone keeps NX, producing a
 * non-canonical address that faults (#GP) when dereferenced. */
#define PT_ADDR_4K 0x000FFFFFFFFFF000ULL
#define PT_ADDR_2M 0x000FFFFFFFE00000ULL
#define PT_ADDR_1G 0x000FFFFFC0000000ULL

static uint64_t user_v2p(uint64_t vaddr) {
    if (!user_cr3) return 0;
    uint64_t* pml4 = (uint64_t*)(user_cr3 & PT_ADDR_4K);
    uint64_t e = pml4[(vaddr >> 39) & 0x1FF];
    if (!(e & 1)) return 0;
    uint64_t* pdpt = (uint64_t*)(e & PT_ADDR_4K);
    e = pdpt[(vaddr >> 30) & 0x1FF];
    if (!(e & 1)) return 0;
    if (e & 0x80) return (e & PT_ADDR_1G) + (vaddr & 0x3FFFFFFFULL);
    uint64_t* pd = (uint64_t*)(e & PT_ADDR_4K);
    e = pd[(vaddr >> 21) & 0x1FF];
    if (!(e & 1)) return 0;
    if (e & 0x80) return (e & PT_ADDR_2M) + (vaddr & 0x1FFFFFULL);
    uint64_t* pt = (uint64_t*)(e & PT_ADDR_4K);
    e = pt[(vaddr >> 12) & 0x1FF];
    if (!(e & 1)) return 0;
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
            return vfs_close(internal);
        }
        case SYS_GETPID: {
            process_t* cur = get_cur_proc();
            return cur ? cur->pid : 0;
        }
        case SYS_SBRK: {
            process_t* cur = get_cur_proc();
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
            if (new_brk > 0x100000000ULL) return -1;    // keep the heap below 4 GiB
            cur->program_break = new_brk;
            return old_brk;
        }
        case SYS_FSIZE: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            return vfs_fsize(internal);
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
            // `path`, passing argv onto the new program's entry stack (SysV layout,
            // read by crt0). argv is a NULL-terminated user array of user string
            // pointers — copied into kernel buffers HERE, while the old address
            // space still exists (do_execve destroys it before building the new
            // stack). Limits: 8 args of 63 chars. envp (a3) still ignored. On
            // success the syscall returns into the new program; on failure -1.
            if (!user_str_ok(a1)) return -1;
            char path[128];
            if (copy_str_from_user(path, a1, sizeof(path)) != 0) return -1;
            static char kargv_store[8][64];    /* safe: syscalls serialized, no block */
            char* kargv[8];
            int argc = 0;
            if (a2) {
                for (; argc < 8; argc++) {
                    uint64_t uptr = 0;
                    if (copy_from_user(&uptr, a2 + (uint64_t)argc * 8, 8) != 0) return -1;
                    if (!uptr) break;                    /* NULL terminator */
                    if (!user_str_ok(uptr)) return -1;
                    if (copy_str_from_user(kargv_store[argc], uptr, 64) != 0) return -1;
                    kargv[argc] = kargv_store[argc];
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
            int r = do_execve(copy, sz, kargv, argc, path);   // success -> returns into new image
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
            process_t* p = get_cur_proc();
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
            extern uint64_t syscall_frame_ptr;
            uint64_t* f = (uint64_t*)syscall_frame_ptr;
            return f ? f[14] : 0;
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
