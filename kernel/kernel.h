// ============================================================
// kernel.h - Cabecera principal del kernel NyxOS x86_64
// ============================================================
#ifndef KERNEL_H
#define KERNEL_H

// ============================================================
// Tipos básicos (sin incluir cabeceras estándar)
// ============================================================
typedef unsigned long size_t;
typedef long ssize_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef int wchar_t;
typedef unsigned int mode_t;
typedef int32_t pid_t;
#ifndef __bool_true_false_are_defined
typedef _Bool bool;
#define true 1
#define false 0
#endif
#define false 0
#define true 1

// ============================================================
// stdarg (usando built-ins de GCC)
// ============================================================
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)

// ============================================================
// Constantes
// ============================================================
#define NULL ((void*)0)
#define KERNEL_NAME    "NyxOS"
#define KERNEL_VERSION "5.8.58"
#define KERNEL_CODENAME "GUI Suite"
#define KERNEL_DATE    "2026"

#define ARCH_X86 1
#define BITS_64  1

#define PAGE_SIZE        4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define KERNEL_BASE      0xFFFFFF8000000000
#define KERNEL_HEAP_START 0xFFFFFFFF90000000
#define __pa(vaddr)      ((uint64_t)(vaddr) - KERNEL_BASE)
#define __va(paddr)      ((void*)((uint64_t)(paddr) + KERNEL_BASE))
#define KERNEL_HEAP_SIZE  (16 * 1024 * 1024)

// Segment selectors (same as 32-bit)
#define KERNEL_CS  0x08
#define KERNEL_DS  0x10
#define USER_CS   0x1B
#define USER_DS   0x23
#define TSS_SEL   0x28

#define MAX_PROCESSES    512
#define MAX_THREADS      1024
#define MAX_FILES        256
#define PROC_MAX_FDS     32     // per-process open file descriptors (UFD_BASE..)
#define MAX_MOUNTS       16
#define SYS_TABLE_SIZE   256

// Syscall numbers
#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_PRINT   2
#define SYS_OPEN    3
#define SYS_READ    4
#define SYS_CLOSE   5
#define SYS_GETPID  6
#define SYS_SBRK    7
#define SYS_FSIZE   8
#define SYS_EXEC    9
#define SYS_FORK    10
#define SYS_WAITPID 11
#define SYS_PIPE    12
#define SYS_EXECVE  13
#define SYS_DUP2    14
#define SYS_GETDENTS 15
#define SYS_KILL     16
#define SYS_SIGNAL   17
#define SYS_SIGRETURN 18
#define SYS_MMAP     19
#define SYS_MUNMAP   20
#define SYS_CHDIR    21
#define SYS_GETCWD   22
#define SYS_MKDIR    23
#define SYS_UNLINK   24
#define SYS_TTYMODE  25
#define SYS_MPROTECT 26
#define SYS_GETPROCS 27
#define SYS_READKEY  28
#define SYS_DLOPEN   29
#define SYS_DLSYM    30
#define SYS_TIME     31
#define SYS_SLEEP    32
#define SYS_SETFG    33
#define SYS_SOCKET   34
#define SYS_CONNECT  35
#define SYS_BIND     36
#define SYS_LISTEN   37
#define SYS_ACCEPT   38
#define SYS_SENDTO   39
#define SYS_RECVFROM 40
#define SYS_SIGPROCMASK 41
#define SYS_ALARM    42

/* SYS_TTYMODE modes. Canonical: read(0) returns a full line, echoed + backspace-
 * edited by the kernel. Raw: read(0) returns bytes as they arrive, NO echo, and
 * extended keys are delivered as ANSI escapes (ESC [ A/B/C/D = up/down/right/left,
 * H/F = home/end) — the shell's line editor runs on this. */
#define TTY_CANON   0
#define TTY_RAW     1

/* waitpid() options (a3). WNOHANG makes SYS_WAITPID return 0 immediately instead
 * of blocking when a matching child exists but has not exited yet — the shell
 * uses it to reap finished `&` background jobs without stalling at the prompt.
 * WUNTRACED additionally reports a child that has STOPPED (Ctrl-Z / SIGTSTP) — the
 * status is the sentinel WSTOPPED|sig, so the shell can build job control. */
#define WNOHANG     1
#define WUNTRACED   2
/* waitpid status sentinel: a stopped (not exited) child. Bit 16 can't appear in a
 * normal exit status (0-255) or a killed status (128+signo), so it's unambiguous;
 * the low byte carries the stop signal. */
#define WSTOPPED    0x10000

/* ------------------------------------------------------------------ */
/*  Signals (v5.8.11) — a subset of POSIX                             */
/* ------------------------------------------------------------------ */
/* Signal numbers fit in a u32 bitmask (bit i = signal i), so NSIG <= 32. */
#define NSIG        32
#define SIGHUP      1
#define SIGINT      2    /* Ctrl-C */
#define SIGQUIT     3
#define SIGILL      4
#define SIGABRT     6
#define SIGFPE      8
#define SIGKILL     9    /* uncatchable — always the default (terminate) action */
#define SIGUSR1     10
#define SIGSEGV     11
#define SIGUSR2     12
#define SIGPIPE     13
#define SIGALRM     14
#define SIGTERM     15
#define SIGCHLD     17
#define SIGCONT     18
#define SIGSTOP     19   /* uncatchable */
#define SIGTSTP     20   /* terminal stop (Ctrl-Z) — catchable, default = stop */

/* Handler sentinels (a real handler is a ring-3 code address, always > 1). */
#define SIG_DFL     0UL  /* default action (terminate, or ignore for SIGCHLD) */
#define SIG_IGN     1UL  /* ignore the signal */

/* read()/etc. return this (negated) when a signal interrupts a blocking wait. */
#define EINTR       4

/* open() flags. O_CREAT is the long-standing `flags & 1`; O_TRUNC empties an
 * existing file (shell `>`), O_APPEND starts writes at EOF (shell `>>`). */
#define O_RDONLY    0
#define O_CREAT     1
#define O_TRUNC     2
#define O_APPEND    4

/* ------------------------------------------------------------------ */
/*  mmap (v5.8.12) — anonymous, demand-zero memory mappings           */
/* ------------------------------------------------------------------ */
#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
/* Mappings live in [MMAP_BASE, MMAP_MAX): above the 4 GiB heap cap and well below
 * the 128 TiB user stack (0x7FFFFFFFE000), so they never collide with either. */
#define MMAP_BASE  0x100000000ULL
#define MMAP_MAX   0x0000700000000000ULL
#define PROC_MAX_VMAS 16

/* Shared libc (v5.8.28). One copy of libc's code is loaded at boot and mapped
 * read-only into every user process at this fixed VA, so programs link against
 * it (ld --just-symbols) instead of each bundling ~14 KB of libc. It sits within
 * ±2 GiB of the program at 0x10000 (so direct rel32 calls reach it), and the heap
 * cap (SYS_SBRK) is kept just below it. libc's tiny writable .bss (the malloc
 * freelist head) is given a private per-process page. */
#define SHARED_LIBC_BASE 0x30000000ULL
void shared_libc_load(void);            // parse + load /libc.so into shared frames (boot)
void shared_libc_map(uint64_t* pml4);   // map it into a process's address space
int  shared_libc_is_ready(void);
void map_page_ro(uint64_t* pml4, void* phys, void* virt, int exec);  // paging.c: RO user page

// A mapped virtual-address region (contiguous present user pages, same perms).
typedef struct { uint64_t start, end; int writable, exec; } vm_region_t;
// Walk a process address space, coalescing runs of present user pages with the
// same permissions into regions (for /proc/<pid>/maps). Returns the count.
int vm_collect_regions(uint64_t* pml4, vm_region_t* out, int max);
long     do_dlopen(const char* path);              // SYS_DLOPEN: map a .so into this process
uint64_t do_dlsym(long handle, const char* name);  // SYS_DLSYM: resolve a symbol to its VA

typedef struct {
    uint64_t start;      // page-aligned base VA (inclusive)
    uint64_t end;        // page-aligned end VA (exclusive)
    uint32_t prot;       // PROT_* bits (honored per-page by vm_handle_fault; mprotect updates it)
    uint8_t  used;       // 1 = slot in use
    // File-backed mapping: a private kernel snapshot of the file (from offset 0).
    // A demand-faulted page copies its slice from here; NULL = anonymous (zero-fill).
    // Owned by the VMA: freed on munmap/execve, deep-copied on fork.
    uint8_t* file_buf;
    uint32_t file_size;
} vma_t;

// Pipe fds are stored in the per-process fd table (ufd_handle) with this flag set,
// so SYS_READ/WRITE/CLOSE route to the pipe layer instead of the VFS. The low bits
// carry (pipe_id << 1) | end (end: 0 = read, 1 = write). A real VFS handle is a
// small index (< MAX_FILES) so it never collides with the flag.
#define UFD_PIPE_FLAG        0x40000000
#define UFD_PIPE_MAKE(id, w) (UFD_PIPE_FLAG | ((id) << 1) | ((w) ? 1 : 0))
#define UFD_PIPE_ID(h)       (((h) & 0x3FFFFFFF) >> 1)
#define UFD_PIPE_IS_WRITE(h) ((h) & 1)

// Socket fds share the same fd table with this (distinct) flag set, so
// SYS_READ/WRITE/CLOSE route to the TCP socket layer (net.c) instead of the VFS.
// The low bits carry the net-socket id (< MAX_SOCKETS). 0x20000000 doesn't
// collide with the pipe flag (0x40000000) or a small VFS handle.
#define UFD_SOCK_FLAG        0x20000000
#define UFD_SOCK_MAKE(id)    (UFD_SOCK_FLAG | ((id) & 0xFFFF))
#define UFD_SOCK_ID(h)       ((h) & 0xFFFF)

#define MAX_PATH         256
#define MAX_FILENAME     128

// Extended keycodes (> 0x7F to avoid ASCII overlap)
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_PGUP    0x86
#define KEY_PGDN    0x87
#define KEY_INSERT  0x88
#define KEY_DEL     0x89
// Synthetic keycodes the compositor injects from mouse-wheel notches, delivered to
// the focused window's on_key like any other key (scrollback nav in the terminal).
#define KEY_WHEEL_UP   0x8A
#define KEY_WHEEL_DOWN 0x8B

// CR4 flags
#define CR4_PAE     (1 << 5)
#define CR4_SMEP    (1 << 20)

// Page table entry flags
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_HUGE     (1ULL << 7)
#define PAGE_NX       (1ULL << 63)

// EFER MSR (0xC0000080)
#define MSR_EFER    0xC0000080
#define EFER_SCE    (1 << 0)     // System Call Extensions — enables syscall/sysret
#define EFER_LME    (1 << 8)
#define EFER_LMA    (1 << 10)
#define EFER_NXE    (1 << 11)

// STAR, LSTAR, SF_MASK MSRs for syscall
#define MSR_STAR     0xC0000081
#define MSR_LSTAR    0xC0000082
#define MSR_CSTAR    0xC0000083
#define MSR_SF_MASK  0xC0000084

// ============================================================
// ESTRUCTURAS
// ============================================================
typedef struct process {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t euid;
    uint32_t gid;
    uint32_t egid;
    char comm[32];
    char cmdline[256];
    void* page_directory;
    void* stack;
    void* kernel_stack;
    uint32_t state;         // PROC_PARKED / PROC_RUN / PROC_ZOMBIE
    uint32_t priority;
    uint32_t cpu_time;
    uint32_t start_time;
    void* files[MAX_FILES];
    uint64_t program_break;  // top of the heap (moved by SYS_SBRK)
    uint64_t heap_start;     // initial break; the lazy-sbrk fault-in window is
                             // [heap_start, program_break) — a not-present user
                             // fault there gets a fresh zeroed page on first touch
    uint32_t sched_managed;  // 1 = round-robined by the preemptive scheduler (spawn_user_path);
                             // blocking-exec and unstarted procs leave this 0 so they're skipped
    uint32_t waiting_for;    // pid this proc is blocked in kwait() on (0 = not waiting)
    uint32_t blocked_in_kernel; // 1 = parked mid-syscall (ring 0): the scheduler must
                             // resume it on the KERNEL CR3, since -mcmodel=large kernel
                             // code runs at low link addresses only mapped there
    uint32_t wake_tick;      // tick_count to wake a sleep()-blocked proc (0 = not sleeping)
    uint32_t stop_sig;       // job control: signal that stopped us (SIGTSTP/SIGSTOP), 0 = not stopped
    uint32_t stopped_reported; // 1 = this stop was already reported to the parent's waitpid(WUNTRACED)
    uint32_t sched_weight;   // ticks per turn in the weighted round-robin (0 => 1)
    uint32_t sched_quantum;  // ticks left in the current turn (scheduler-internal)
    int      exit_code;      // status passed to SYS_EXIT, collected by kwait()
    // Per-process file-descriptor table: opaque small ints (UFD_BASE + slot) each
    // mapping to an internal VFS handle + byte offset. Isolated per process and
    // closed when the process is reaped, so fds neither leak nor cross processes.
    int      ufd_handle[PROC_MAX_FDS];
    uint32_t ufd_offset[PROC_MAX_FDS];
    uint8_t  ufd_inuse[PROC_MAX_FDS];
    // Signals (v5.8.11). Delivered at return-to-ring-3 (signal_dispatch, from the
    // syscall path). sig_handlers: 0=SIG_DFL, 1=SIG_IGN, else a ring-3 handler VA.
    uint32_t sig_pending;             // bit i set = signal i is pending
    uint32_t sig_mask;                // bit i set = signal i is blocked (deferred)
    uint32_t sig_active;              // bit i set = handler for signal i is on the user stack now
    uint64_t sig_handlers[NSIG];      // per-signal disposition (SIG_DFL/SIG_IGN/handler VA)
    uint64_t sig_trampoline;          // ring-3 sigreturn trampoline VA (libc __sigreturn)
    // Ring-3 context saved when a handler is entered, restored by SYS_SIGRETURN:
    // [0..14]=GPRs r15..rax (frame order), [15]=RFLAGS, [16]=RIP, [17]=user RSP.
    uint64_t sig_saved[18];
    uint32_t alarm_tick;              // alarm(2) deadline in ticks (0 = none); irq_scheduler_tick
                                      // posts SIGALRM once tick_count passes it
    // Anonymous mmap regions (v5.8.12). A fault inside a vma gets a fresh zeroed
    // page with the vma's prot (vm_handle_fault); mmap_next bumps up per mapping.
    vma_t    mmap_vmas[PROC_MAX_VMAS];
    uint64_t mmap_next;
    // Per-process current working directory (absolute, normalized). Relative paths
    // in open()/getdents() resolve against it; inherited on fork, kept across execve.
    // An empty string is treated as "/".
    char     cwd[MAX_PATH];
    // stdin discipline (SYS_TTYMODE): 0 = canonical line reads, 1 = raw single-byte
    // reads with arrows as ANSI escapes. NOT inherited on fork, reset by execve —
    // only the process that asked for raw mode sees it.
    uint32_t tty_raw;
    struct process* next;
    struct process* parent;
    struct process* children;
} process_t;

// Process states (kept numeric-compatible with the existing 0/1 usage).
#define PROC_PARKED  0   // not runnable (retired kernel thread); scheduler skips
#define PROC_RUN     1   // runnable or running
#define PROC_ZOMBIE  2   // scheduled user proc that exited; awaiting reap_zombies()
#define PROC_BLOCKED 3   // blocked in kwait()/sleep(); scheduler skips
#define PROC_STOPPED 4   // job-control stopped (SIGTSTP/SIGSTOP); parked until SIGCONT

// Weighted round-robin: the compositor (GUI) runs several ticks per turn so it
// stays responsive while background jobs (weight 1) run.
#define SCHED_WEIGHT_GUI 4

// x86_64 TSS (102 bytes)
// Layout per Intel Vol 3, Figure 7-9
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint32_t reserved_ss0;    // offset 28 (was SS0 in 32-bit)
    uint32_t reserved_ss1;    // offset 32 (was SS1 in 32-bit)
    uint32_t reserved_ss2;    // offset 36 (was SS2 in 32-bit)
    uint64_t ist1;            // offset 40
    uint64_t ist2;            // offset 48
    uint64_t ist3;            // offset 56
    uint64_t ist4;            // offset 64
    uint64_t ist5;            // offset 72
    uint64_t ist6;            // offset 80
    uint64_t ist7;            // offset 88
    uint32_t reserved2;       // offset 96
    uint16_t iomap_base;      // offset 100
} __attribute__((packed)) tss_entry_t;

// RTC time structure (also in rtc.h)
#ifndef RTC_TIME_T_DEFINED
#define RTC_TIME_T_DEFINED
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;
#endif

typedef struct {
    uint32_t tid;
    uint32_t pid;
    void* stack;
    void* kernel_stack;
    uint32_t state;
    void* entry_point;
    void* arg;
    void* return_value;
} thread_t;

typedef struct {
    uint32_t fd;
    uint32_t flags;
    uint32_t mode;
    uint32_t offset;
    void* inode;
    void* ops;
    uint32_t ref_count;
} file_t;

typedef struct {
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t blocks;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    void* data;
} inode_t;

typedef struct {
    uint32_t ino;
    char name[MAX_FILENAME];
    uint32_t type;
} dirent_t;

typedef struct {
    char device[64];
    char mount_point[MAX_PATH];
    char fs_type[16];
    uint32_t flags;
    void* fs_data;
} mount_t;

typedef struct {
    uint8_t* data;
    uint32_t length;
    uint32_t protocol;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
} net_packet_t;

typedef struct {
    char name[16];
    uint8_t mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;
    uint32_t flags;
    uint32_t tx_packets;
    uint32_t rx_packets;
    void* driver_data;
} net_iface_t;

// ============================================================
// Network byte order (host x86 is little-endian; the wire is big-endian)
// ============================================================
// Centralizes the byte swaps every protocol used to open-code inline (the
// ((x<<8)&0xFF00)|((x>>8)&0xFF) idiom, and uglier 32-bit versions). The
// __builtin_bswap* forms compile to a single bswap/xchg. NOTE: IPv4 addresses
// in this stack are stored in *network* order already (low byte = first octet,
// e.g. 127.0.0.1 == 0x0100007F), so htonl/ntohl are only for fields that are
// genuine host integers on the wire (TCP seq/ack) — IP addresses never pass
// through them.
static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }

// Expand a network-order IPv4 address into four %d printf arguments, first octet
// first:  printf("%d.%d.%d.%d", IP4_OCTETS(ip)).  Replaces the
// ip&0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF quad that was copy-pasted
// across every network printer.
#define IP4_OCTETS(ip) (int)((ip) & 0xFF), (int)(((ip) >> 8) & 0xFF), \
                       (int)(((ip) >> 16) & 0xFF), (int)(((ip) >> 24) & 0xFF)

// ============================================================
// COLORES VGA
// ============================================================
enum vga_color {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_LIGHT_BROWN   = 14,
    VGA_WHITE         = 15,
};

// ============================================================
// VARIABLES GLOBALES EXTERNAS
// ============================================================
extern process_t* process_table[MAX_PROCESSES];
extern uint64_t memory_total;
extern uint64_t memory_used;
extern volatile uint32_t tick_count;
extern int process_count;
extern int current_idx;
extern net_iface_t net_interfaces[8];
extern uint64_t saved_rsp;
extern uint64_t next_rsp;
extern uint64_t next_cr3;
extern uint64_t kernel_pml4_phys;

// ============================================================
// DECLARACIONES DE FUNCIONES ENSAMBLADOR
// ============================================================
extern void memcpy_asm(void* dest, const void* src, size_t n);
extern void memset_asm(void* dest, uint8_t val, size_t n);
extern void _gdt_flush(uint64_t gdt_ptr);
extern void _idt_flush(uint64_t idt_ptr);

// ============================================================
// FUNCIONES DE E/S INLINE
// ============================================================
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }
static inline void enable_interrupts(void) { __asm__ volatile ("sti"); }
static inline void disable_interrupts(void) { __asm__ volatile ("cli"); }
static inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}
static inline void write_cr0(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(val));
}
static inline uint64_t read_cr2(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}
static inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}
static inline void write_cr3(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val));
}
static inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(val));
    return val;
}
static inline void write_cr4(uint64_t val) {
    __asm__ volatile ("mov %0, %%cr4" : : "r"(val));
}
static inline void flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) :: "memory");
}
static inline void invlpg(void *addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void write_msr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

// ============================================================
// FUNCIONES UTILITARIAS
// ============================================================
static inline void *memcpy(void *dest, const void *src, size_t n) {
    memcpy_asm(dest, src, n);
    return dest;
}
static inline void *memmove(void *dest, const void *src, size_t n) {
    if (dest == src) return dest;
    if ((uintptr_t)dest < (uintptr_t)src) {
        for (size_t i = 0; i < n; i++) ((uint8_t*)dest)[i] = ((const uint8_t*)src)[i];
    } else {
        for (size_t i = n; i > 0; i--) ((uint8_t*)dest)[i-1] = ((const uint8_t*)src)[i-1];
    }
    return dest;
}
static inline char *strcpy(char *dest, const char *src) {
    char *orig = dest;
    while ((*dest++ = *src++) != '\0');
    return orig;
}
static inline char *strcat(char *dest, const char *src) {
    char *orig = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++) != '\0');
    return orig;
}
static inline char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}
static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}
static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
static inline int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
static inline int atoi(const char *s) {
    int sign = 1, result = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { result = result * 10 + (*s - '0'); s++; }
    return sign * result;
}
static inline char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return NULL;
}
char *strstr(const char *haystack, const char *needle);
static inline char *strtok(char *str, const char *delim) {
    static char *next = NULL;
    if (str) next = str;
    if (!next) return NULL;
    while (*next && strchr(delim, *next)) next++;
    if (*next == '\0') return NULL;
    char *start = next;
    while (*next && !strchr(delim, *next)) next++;
    if (*next) *next++ = '\0';
    return start;
}
static inline long strtol(const char *s, char **end, int base) {
    long result = 0;
    int sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    }
    if (base == 0) base = 10;
    while (*s) {
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    if (end) *end = (char*)s;
    return sign * result;
}

// ============================================================
// PROTOTIPOS DE FUNCIONES GLOBALES
// ============================================================
void kernel_main(uint64_t magic, void* mboot_ptr);
void kernel_panic(const char* msg, ...);
void launch_shell(void);
void nyxfetch(void);
void execute_command(const char* cmd_line);
void command_complete(const char* partial, char* out, int out_size, int* match_count);
void command_list_matches(const char* partial, char* out, int out_size);

void init_gdt(void);
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
void tss_set_stack(uint64_t rsp0);
void tss_set_ist(uint8_t ist_idx, uint64_t stack_top);
void load_tss(void);
void init_idt(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_set_gate_ist(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags, uint8_t ist);

// User ring-3 stack: pages mapped growing DOWN from 0x00007FFFFFFFF000. One 4 KB
// page overflows on modest call chains, so give every user process 64 KB.
#define USER_STACK_PAGES 16

// IST stack sizes
#define IST_STACK_SIZE 8192
#define IST_DOUBLE_FAULT 1
void init_isr(void);
void isr_handler(uint64_t int_no, uint64_t rip, uint64_t error, uint64_t cs, uint64_t* frame);
void init_irq(void);
void irq_handler(uint64_t irq_no);
void irq_install_handler(int irq, void (*handler)(void*));
void irq_unmask(int irq);
void irq_mask(int irq);
void irq_eoi(uint64_t int_no);

void init_memory(uint64_t mem_size);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, uint32_t align);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t size);
void* alloc_page(void);
void free_page(void* addr);
void page_incref(void* addr);           // COW: share an allocated page (fork)
uint32_t page_get_refcount(void* addr); // COW: how many PTEs point at this page
void page_pin(void* addr);              // mark a frame un-freeable (shared-libc masters)
void slab_init_all(void);

void init_paging(void);
void* get_phys_addr(void* virtual_addr);
void map_page(void* phys_addr, void* virt_addr, uint64_t flags);
void unmap_page(void* virt_addr);
void* clone_page_directory(void);
uint64_t* clone_page_directory_cow(uint64_t* parent_pml4); // fork: COW-share the user half
uint64_t* alloc_page_directory(void);
uint64_t* get_kernel_page_directory(void);
void switch_page_directory(uint64_t* pd);
void map_page_dir(uint64_t* pd, void* phys, void* virt, uint64_t flags);

// Demand paging + copy-on-write (serviced from the #PF handler).
int vm_handle_fault(uint64_t cr2, uint64_t err);
int vm_map_demand(uint64_t virt);            // mark virt as allocate-on-first-touch
int vm_map_cow(uint64_t virt, uint64_t phys); // map virt -> phys read-only, copy on write
void vm_unmap(uint64_t virt);
uint64_t vm_stat_demand(void);
uint64_t vm_stat_cow(void);

void init_heap(void);
void* heap_alloc(size_t size);
void heap_free(void* ptr);

void init_process(void);
process_t* create_process(const char* name, void* entry, uint64_t flags);
process_t* create_user_process(const char* name, void* entry, void* user_stack, uint64_t* page_dir);
int do_fork(void);   // SYS_FORK: COW-clone the caller; returns child pid to parent, 0 in child
int do_waitpid(int pid, int* out_code, int options); // SYS_WAITPID: reap a child; -1 none, 0 WNOHANG-none-ready, >0 pid
// Signals (signal.c). signal_dispatch is called from syscall_entry (isr_stubs.asm)
// on the return path, with the saved user frame; it delivers one pending signal.
void signal_dispatch(uint64_t* frame);
int  signal_deliver_fault(uint64_t* frame, int sig); // catchable CPU-exception signal (isr.c) -> handler; 1 if delivered
int  do_kill(int pid, int sig);                      // SYS_KILL: post `sig` to `pid`
long do_signal(int sig, uint64_t handler, uint64_t trampoline); // SYS_SIGNAL: install disposition
void do_sigreturn(void);                             // SYS_SIGRETURN: restore pre-handler context
long do_sigprocmask(int how, uint64_t set, uint64_t oldset_ptr); // SYS_SIGPROCMASK: read/change block mask
unsigned int do_alarm(unsigned int seconds);                     // SYS_ALARM: schedule SIGALRM after N s
void signal_raise(process_t* p, int sig);            // post a signal to a process (+ wake if blocked)
int  signal_pending(process_t* p);                   // 1 if a deliverable (unblocked) signal waits
int  signal_check_stop(process_t* p);                // park on a pending stop signal; 1 if it did (restart)
void signal_send_foreground(int sig);                // keyboard Ctrl-C -> foreground process
// Anonymous mmap (mmap.c). Pages within a returned region demand-fault to zero.
uint64_t do_mmap(uint64_t addr, uint64_t length, int prot, int flags,
                 int file_handle, uint32_t file_size, uint32_t file_off); // SYS_MMAP; MAP_FAILED=(uint64_t)-1
int      do_munmap(uint64_t addr, uint64_t length);  // SYS_MUNMAP
int      do_mprotect(uint64_t addr, uint64_t length, int prot); // SYS_MPROTECT
vma_t*   vma_find(process_t* p, uint64_t addr);      // vm_handle_fault lookup
void     vm_protect_range(uint64_t* pml4, uint64_t start, uint64_t end, int prot); // paging.c
void     mmap_free_bufs(process_t* p);               // release file-backed snapshots at reap
int do_execve(const uint8_t* data, uint32_t size,
              char* const* kargv, int argc,
              char* const* kenvp, int envc, const char* path); // SYS_EXECVE: replace caller's image; -1 on failure
void proc_set_comm(process_t* p, const char* path); // set comm to basename(path) minus ".elf"
int copy_to_user(uint64_t udst, const void* src, uint64_t len); // via user_cr3 page walk (syscall.c)
void free_page_directory(uint64_t* pml4);           // free a user address space (COW-refcount aware)
int elf_load_image(const uint8_t* data, uint32_t size, uint64_t** out_pd,
                   uint64_t* out_entry, uint64_t* out_stack_top, uint64_t* out_brk);

// Anonymous pipes (pipe.c)
int  pipe_new(void);                    // alloc a pipe (1 read ref + 1 write ref) -> id, or -1
int  pipe_read(int id, char* kbuf, int n);   // blocking read into kbuf; 0 = EOF
int  pipe_write(int id, const char* src, int n); // non-blocking write; -1 = broken pipe
void pipe_close_end(int id, int is_write);   // drop one ref; frees the pipe at zero
void pipe_incref(int id, int is_write);      // fork(): inherit a pipe fd
void reap_user_process(process_t* proc);
void close_proc_fds(process_t* proc);   // close a process's fds (called at exit + reap)
void destroy_process(uint64_t pid);
process_t* find_process(uint64_t pid);
process_t* get_current_process(void);
void schedule(void);
void switch_context(uint64_t* old_rsp_ptr, uint64_t new_rsp);
uint64_t create_task_stack(uint64_t stack_top, uint64_t entry_point);
void ensure_idle_process(void);

void init_syscalls(void);
void setup_syscall_msrs(void);
void set_kernel_rsp(uint64_t rsp);
uint64_t syscall_handler(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);
extern void syscall_entry(void);

void init_vfs(void);
int vfs_open(const char* path, int flags, mode_t mode);
int vfs_read(int fd, void* buf, size_t count);
int vfs_write(int fd, const void* buf, size_t count);
int vfs_pread(int fd, void* buf, uint32_t count, uint32_t offset);
int vfs_pwrite(int fd, const void* buf, uint32_t count, uint32_t offset);
int vfs_close(int fd);
int vfs_mkdir(const char* path, mode_t mode);
int vfs_unlink(const char* path);
dirent_t* vfs_readdir(int fd);
int vfs_isdir(const char* path);   // 1 if `path` resolves to a directory, else 0
uint32_t vfs_fsize(int fd);
uint8_t* vfs_fdata(int fd);
int vfs_create_from_mem(const char* path, uint8_t* data, uint32_t size);
void init_load_modules(void);

void init_net(void);
void kernel_poll_net(void);
void init_background_tasks(void);
void run_background_tasks(void);
void irq_scheduler_tick(void);

// Preemptive scheduler (process.c): dormant until sched_enable(); mtdemo_start()
// spins up two demo kernel threads and turns preemption on for the self-test.
void sched_enable(void);
void sched_disable(void);
int  sched_is_enabled(void);
int  mtdemo_start(void);
extern volatile uint64_t mtdemo_a_count, mtdemo_b_count;

// Preemption critical sections: while the disable-count is nonzero the scheduler
// keeps the current thread (protects non-reentrant shared state — the heap).
void preempt_disable(void);
void preempt_enable(void);
// Preemptive user (ring-3) processes: spawn one from an ELF path into the
// scheduler (non-blocking), and reap the ones that have exited.
int  spawn_user_path(const char* path);
void reap_zombies(void);
// Block the current thread until child `pid` exits; returns its exit code (or -1
// if there is no such process). Used by `exec` to run a foreground job.
int  kwait(uint32_t pid);
// Block the current thread until all of its children have exited (the `wait` cmd).
void kwait_all(void);
// Called from SYS_EXIT: wake a parent blocked in kwait() on this child.
void wake_waiters(process_t* child);

void init_timer(uint32_t frequency);
uint32_t get_ticks(void);
void sleep(uint32_t milliseconds);

void init_serial(void);
void serial_putchar(char c);
void serial_puts(const char* str);
char serial_getchar(void);
char serial_getchar_nonblock(void);

void init_keyboard(void);
char getchar(void);
char getchar_poll(void);
int  getkey_poll(void);
void keyboard_irq_handler(void* unused);
int is_ctrl_pressed(void);

void init_screen(void);
int putchar(int c);
int puts(const char* str);
void set_putchar_hook(int (*hook)(int c));
int printf(const char* fmt, ...);
int vprintf(const char* fmt, va_list args);
void clear_screen(void);
char* itoa(int value, char* str, int base);
char* lltoa(long long value, char* str, int base);
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg);
void set_terminal_color(uint8_t color);
uint8_t get_terminal_color(void);
void pipe_start(void);
int pipe_stop(void);
const char* pipe_get_data(void);
int pipe_get_len(void);

int elf_load(const uint8_t* data, uint32_t size, process_t** out_proc);
int elf_validate(const uint8_t* data, uint32_t size);

int initramfs_load(void);
void initramfs_boot(void);

void init_ext2(void);
int  ata_init(void);
int  ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buf);
int  ext2_mount(uint8_t drive, uint32_t part_lba);
uint32_t ext2_resolve(const char* path);
uint32_t ext2_get_size(const char* path);
int  ext2_read_file(const char* path, void* buf, uint32_t maxlen);
int  ext2_write_file(const char* path, const void* buf, uint32_t len);
int  ext2_create_file(const char* path);
int  ext2_readdir(const char* path, dirent_t* entries, uint32_t max_entries);

int rtl8139_init(void);
void eth_poll(int iface_idx);
void arp_init(void);
int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const uint8_t* data, uint32_t len, int iface_idx);

// Userspace TCP socket layer (net.c) — backs SYS_SOCKET/SYS_CONNECT and the
// socket branches of SYS_READ/WRITE/CLOSE. Returns a small net-socket id.
int nsock_create(int domain, int type, int protocol);
int nsock_connect(int s, uint32_t ip, uint16_t port);   // blocks until ESTABLISHED
int nsock_bind(int s, uint32_t ip, uint16_t port);      // record the local port
int nsock_listen(int s, int backlog);                   // passive open on the bound port
int nsock_accept(int s);                                // blocks; new socket id for the peer
int nsock_send(int s, const void* buf, int len);
int nsock_recv(int s, void* buf, int len);              // blocks until data or EOF
int nsock_sendto(int s, const void* buf, int len, uint32_t ip, uint16_t port);     // UDP
int nsock_recvfrom(int s, void* buf, int len, uint32_t* src_ip, uint16_t* src_port); // UDP, blocks
int nsock_udp_deliver(uint16_t dst_port, uint8_t* data, uint32_t len, uint32_t src_ip, uint16_t src_port);
int nsock_close(int s);
void tcp_echo_init(void);   // start the loopback TCP echo service (port 7)
void tcp_echo_poll(void);   // drive it from the net poll loop
void udp_register_listener(uint16_t port, void (*handler)(uint8_t*, uint32_t, uint32_t, uint16_t));
int dhcp_request(int iface_idx);
void cmd_dhcp(int argc, char** argv);
uint32_t dns_resolve(const char* hostname, int iface_idx);
void dns_set_server(uint32_t ip);
uint32_t dns_get_server(void);
int icmp_ping(uint32_t dst_ip, int count, int iface_idx);
int tcp_init(void);
int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_send(int conn_id, const uint8_t* data, uint32_t len);
int tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len);
int tcp_close(int conn_id);

// SMP
extern uint32_t cpu_count;
void smp_init(void);

// VFS mount layer
#define FS_TYPE_EXT2  1
#define MAX_MOUNT_POINTS 8
typedef struct {
    int type;
    char mount_point[MAX_PATH];
    void* fs_data;
    uint32_t (*resolve)(const char* path);
    uint32_t (*get_size)(const char* path);
    int (*read_file)(const char* path, void* buf, uint32_t maxlen);
    int (*write_file)(const char* path, const void* buf, uint32_t len);
    int (*readdir)(const char* path, dirent_t* entries, uint32_t max_entries);
    int (*mkdir)(const char* path);
    int (*unlink)(const char* path);
} mount_entry_t;

int vfs_mount(const char* mount_point, int fs_type, void* fs_data);
mount_entry_t* vfs_find_mount(const char* path);
const char* vfs_getcwd(void);
int vfs_chdir(const char* path);
void* vfs_getcwd_node(void);      // opaque current-directory node (per-shell CWD)
void  vfs_setcwd_node(void* n);
void* vfs_root_node(void);
void vfs_list_dir(const char* path);
void vfs_cat_file(const char* path);
int vfs_touch(const char* path);
int vfs_write_file(const char* path, const void* buf, uint32_t len);
int vfs_cp(const char* src, const char* dst);
void vfs_rename(const char* old, const char* new);

extern int keyboard_layout;
void set_keyboard_layout(int layout);

int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
char* strcasestr(const char *haystack, const char *needle);

int vbe_init(void);
int vbe_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
uint32_t vbe_get_width(void);
uint32_t vbe_get_height(void);
uint32_t vbe_get_bpp(void);
void* vbe_get_lfb(void);

void fb_init(uint32_t width, uint32_t height, uint32_t bpp, void* addr);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_blit(const void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);
void fb_clear(uint32_t color);
int  fb_enable_backbuffer(void);   // opt-in double buffering (compositor)
void fb_present(void);             // blit the back buffer to the LFB (one frame)
void fb_use_lfb_direct(void);      // repoint drawing at the hardware LFB (panic screen)
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
void* fb_get_addr(void);
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

int mouse_init(void);
void mouse_irq_handler(void* unused);
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_buttons(void);
int mouse_get_z(void);          // accumulated wheel notches (IntelliMouse); +up / -down
void mouse_set_pos(int x, int y);

void font_draw_char(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg);
void font_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
void font_draw_char_scaled(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg, uint32_t scale);
void font_draw_string_scaled(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg, uint32_t scale);
uint32_t font_get_width(void);
uint32_t font_get_height(void);

void gui_demo(void);
void speaker_init(void);
void speaker_on(uint32_t frequency);
void speaker_off(void);
void speaker_beep(uint32_t frequency, uint32_t duration_ms);

int sb16_init(void);
int sb16_play_pcm(const uint8_t* data, uint32_t len, uint32_t sample_rate, int bits, int channels);
void sb16_stop(void);
int sb16_is_playing(void);

void rtc_init(void);
void rtc_read_time(rtc_time_t* time);

// Compositor
void compositor_init(void);
void compositor_run(void);
void compositor_redraw_now(void);   // one-shot recomposite (foreground-exec wait loop)

#endif // KERNEL_H
