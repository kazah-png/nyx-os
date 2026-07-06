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

static int copy_to_user(uint64_t udst, const void* src, uint64_t len) {
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

uint64_t syscall_handler(uint64_t no, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
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
            char path[128];
            if (copy_str_from_user(path, a1, sizeof(path)) != 0) return -1;
            int flags = (int)a2;
            int mode = (int)a3;
            int internal = vfs_open(path, flags, mode);
            if (internal < 0) return -1;
            int ufd = ufd_alloc(internal);
            if (ufd < 0) { vfs_close(internal); return -1; }  /* table full */
            return ufd;
        }
        case SYS_READ: {
            int internal;
            if (ufd_lookup((int)a1, &internal) != 0) return -1;
            int count = (int)a3;
            if (count < 0 || !user_ptr_ok(a2, (uint64_t)count)) return -1;
            if (count > 4096) count = 4096;
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
            if (inc < 0) return -1;
            uint64_t new_brk = old_brk + inc;
            uint64_t start_page = (old_brk + 0xFFF) & ~0xFFFULL;
            uint64_t end_page = (new_brk + 0xFFF) & ~0xFFFULL;
            if (end_page > 0x100000000ULL) return -1;
            uint64_t* pml4 = (uint64_t*)cur->page_directory;
            for (uint64_t page = start_page; page < end_page; page += 4096) {
                void* phys = alloc_page();
                if (!phys) return -1;
                map_page_dir(pml4, phys, (void*)page, 0x7 | PAGE_NX);
            }
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
            int r = do_waitpid((int)a1, &code);
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
            // `path`. argv/envp (a2/a3) are accepted but not yet threaded onto the
            // new stack (crt0 hardcodes argc=0). On success the syscall returns into
            // the new program; on failure the caller is intact and gets -1.
            if (!user_str_ok(a1)) return -1;
            char path[128];
            if (copy_str_from_user(path, a1, sizeof(path)) != 0) return -1;
            int fd = vfs_open(path, 0, 0);
            if (fd < 0) return -1;
            uint32_t sz = vfs_fsize(fd);
            uint8_t* fdata = vfs_fdata(fd);
            if (!fdata || sz == 0) { vfs_close(fd); return -1; }
            uint8_t* copy = (uint8_t*)kmalloc(sz);
            if (!copy) { vfs_close(fd); return -1; }
            memcpy_asm(copy, fdata, sz);
            vfs_close(fd);
            int r = do_execve(copy, sz);   // success -> rewrites the frame, returns into new image
            kfree(copy);
            return (uint64_t)(int64_t)r;
        }
        case SYS_DUP2: {
            // dup2(oldfd, newfd): make newfd refer to the same pipe end as oldfd,
            // closing whatever newfd held. This is the redirection primitive — e.g.
            // dup2(pipefd[1], 1) sends a child's stdout into a pipe. PIPE fds only:
            // their ends are reference-counted, so two fds can share one safely; VFS
            // handles aren't refcounted (two fds would double-close on reap) -> -1.
            int oldfd = (int)a1, newfd = (int)a2;
            process_t* p = get_cur_proc();
            int internal;
            if (!p || ufd_lookup(oldfd, &internal) != 0) return -1;
            if (!(internal & UFD_PIPE_FLAG)) return -1;      /* pipes only (see above) */
            if (newfd < 0 || newfd >= PROC_MAX_FDS) return -1;
            if (newfd == oldfd) return newfd;
            if (p->ufd_inuse[newfd]) {                        /* implicitly close newfd */
                int old = p->ufd_handle[newfd];
                if (old & UFD_PIPE_FLAG) pipe_close_end(UFD_PIPE_ID(old), UFD_PIPE_IS_WRITE(old));
                else                     vfs_close(old);
                p->ufd_inuse[newfd] = 0;
            }
            pipe_incref(UFD_PIPE_ID(internal), UFD_PIPE_IS_WRITE(internal));
            p->ufd_inuse[newfd]  = 1;
            p->ufd_handle[newfd] = internal;
            p->ufd_offset[newfd] = 0;
            return newfd;
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
