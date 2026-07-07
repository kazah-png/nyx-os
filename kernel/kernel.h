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
#define KERNEL_VERSION "5.8.10"
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

/* waitpid() options (a3). WNOHANG makes SYS_WAITPID return 0 immediately instead
 * of blocking when a matching child exists but has not exited yet — the shell
 * uses it to reap finished `&` background jobs without stalling at the prompt. */
#define WNOHANG     1

// Pipe fds are stored in the per-process fd table (ufd_handle) with this flag set,
// so SYS_READ/WRITE/CLOSE route to the pipe layer instead of the VFS. The low bits
// carry (pipe_id << 1) | end (end: 0 = read, 1 = write). A real VFS handle is a
// small index (< MAX_FILES) so it never collides with the flag.
#define UFD_PIPE_FLAG        0x40000000
#define UFD_PIPE_MAKE(id, w) (UFD_PIPE_FLAG | ((id) << 1) | ((w) ? 1 : 0))
#define UFD_PIPE_ID(h)       (((h) & 0x3FFFFFFF) >> 1)
#define UFD_PIPE_IS_WRITE(h) ((h) & 1)

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
    uint32_t sched_weight;   // ticks per turn in the weighted round-robin (0 => 1)
    uint32_t sched_quantum;  // ticks left in the current turn (scheduler-internal)
    int      exit_code;      // status passed to SYS_EXIT, collected by kwait()
    // Per-process file-descriptor table: opaque small ints (UFD_BASE + slot) each
    // mapping to an internal VFS handle + byte offset. Isolated per process and
    // closed when the process is reaped, so fds neither leak nor cross processes.
    int      ufd_handle[PROC_MAX_FDS];
    uint32_t ufd_offset[PROC_MAX_FDS];
    uint8_t  ufd_inuse[PROC_MAX_FDS];
    struct process* next;
    struct process* parent;
    struct process* children;
} process_t;

// Process states (kept numeric-compatible with the existing 0/1 usage).
#define PROC_PARKED  0   // not runnable (retired kernel thread); scheduler skips
#define PROC_RUN     1   // runnable or running
#define PROC_ZOMBIE  2   // scheduled user proc that exited; awaiting reap_zombies()
#define PROC_BLOCKED 3   // blocked in kwait()/sleep(); scheduler skips

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

// IST stack sizes
#define IST_STACK_SIZE 8192
#define IST_DOUBLE_FAULT 1
void init_isr(void);
void isr_handler(uint64_t int_no, uint64_t rip, uint64_t error, uint64_t cs);
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
int do_execve(const uint8_t* data, uint32_t size,
              char* const* kargv, int argc); // SYS_EXECVE: replace caller's image; -1 on failure
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
uint64_t syscall_handler(uint64_t syscall_no, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);
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
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
void* fb_get_addr(void);
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

int mouse_init(void);
void mouse_irq_handler(void* unused);
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_buttons(void);
void mouse_set_pos(int x, int y);

void font_draw_char(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg);
void font_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
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

#endif // KERNEL_H
