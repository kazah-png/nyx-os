// ============================================================
// kernel.h - Cabecera principal del kernel NyxOS v1.0.1
// ============================================================
#ifndef KERNEL_H
#define KERNEL_H

// ============================================================
// Tipos básicos (sin incluir cabeceras estándar)
// ============================================================
typedef unsigned int size_t;
typedef int ssize_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef int wchar_t;
typedef unsigned int mode_t;
typedef int32_t pid_t;
typedef int bool;
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
#define KERNEL_VERSION "1.0.1"
#define KERNEL_CODENAME "Nightfall"
#define KERNEL_DATE    "2024"

#define ARCH_X86 1
#define BITS_32  1

#define PAGE_SIZE        4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define KERNEL_BASE      0xC0000000
#define KERNEL_HEAP_START 0xD0000000
#define KERNEL_HEAP_SIZE  0x10000000

#define MAX_MODULES      128
#define MAX_PROCESSES    512
#define MAX_THREADS      1024
#define MAX_FILES        256
#define MAX_MOUNTS       16
#define SYS_TABLE_SIZE   256
#define MAX_PATH         256
#define MAX_FILENAME     128

// ============================================================
// ESTRUCTURAS
// ============================================================
typedef struct {
    uint32_t id;
    char name[32];
    void* (*init)(void);
    void* (*handler)(void*);
    uint32_t flags;
    bool loaded;
    bool hidden;
    void* module_base;
    size_t module_size;
} kernel_module_t;

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
    uint32_t state;
    uint32_t priority;
    uint32_t capabilities;
    uint32_t stealth_level;
    uint32_t cpu_time;
    uint32_t start_time;
    void* files[MAX_FILES];
    struct process* next;
    struct process* parent;
    struct process* children;
} process_t;

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
extern uint32_t memory_total;
extern uint32_t memory_used;
extern uint32_t tick_count;
extern int process_count;
extern net_iface_t net_interfaces[8];

// ============================================================
// DECLARACIONES DE FUNCIONES ENSAMBLADOR
// ============================================================
extern void memcpy_asm(void* dest, const void* src, size_t n);
extern void memset_asm(void* dest, uint8_t val, size_t n);
extern void idt_load(uint32_t idt_ptr);
extern void gdt_flush(uint32_t gdt_ptr);

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
static inline uint32_t read_cr0(void) {
    uint32_t val;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(val));
    return val;
}
static inline void write_cr0(uint32_t val) {
    __asm__ volatile ("mov %0, %%cr0" : : "r"(val));
}
static inline uint32_t read_cr2(void) {
    uint32_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}
static inline uint32_t read_cr3(void) {
    uint32_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}
static inline void write_cr3(uint32_t val) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(val));
}
static inline void flush_tlb(void) {
    __asm__ volatile ("mov %%cr3, %%eax; mov %%eax, %%cr3" : : : "eax");
}
static inline void invlpg(void *addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr));
}

// ============================================================
// FUNCIONES UTILITARIAS (protegidas con DOOMGENERIC)
// ============================================================
#ifndef DOOMGENERIC
static inline void *memcpy(void *dest, const void *src, size_t n) {
    memcpy_asm(dest, src, n);
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
        s += 2;
        base = 16;
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
#endif // DOOMGENERIC

// ============================================================
// PROTOTIPOS DE FUNCIONES GLOBALES
// ============================================================
void kernel_main(uint32_t magic, void* mboot_ptr);
void kernel_panic(const char* msg, ...);
void load_offensive_module(char* name, void* (*init_func)(void), uint32_t flags);
void lock_module_pages(kernel_module_t* mod);
void launch_shell(void);
void nyxfetch(void);

void init_gdt(void);
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void init_isr(void);
void isr_handler(uint32_t int_no, uint32_t err_code);
void init_irq(void);
void irq_handler(uint32_t irq_no);
void irq_install_handler(int irq, void (*handler)(void*));

void init_memory(uint32_t mem_size);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, uint32_t align);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t size);
void* alloc_page(void);
void free_page(void* addr);

void init_paging(void);
void* get_phys_addr(void* virtual_addr);
void map_page(void* phys_addr, void* virt_addr, uint32_t flags);
void unmap_page(void* virt_addr);
void* clone_page_directory(void);

void init_heap(void);
void* heap_alloc(size_t size);
void heap_free(void* ptr);

void init_process(void);
process_t* create_process(const char* name, void* entry, uint32_t flags);
void destroy_process(uint32_t pid);
void hide_process(uint32_t pid);
void unhide_process(uint32_t pid);
void fake_process_info(uint32_t pid, const char* fake_name, uint32_t fake_ppid);
process_t* find_process(uint32_t pid);
process_t* get_current_process(void);
void schedule(void);
void switch_context(uint32_t* old_esp_ptr, uint32_t new_esp);
uint32_t create_task_stack(uint32_t stack_top, uint32_t entry_point);

void init_syscalls(void);
void syscall_handler(uint32_t syscall_no, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
void* get_syscall_table(void);
void register_syscall(uint32_t num, void* handler);

void init_vfs(void);
int vfs_open(const char* path, int flags, mode_t mode);
int vfs_read(int fd, void* buf, size_t count);
int vfs_write(int fd, const void* buf, size_t count);
int vfs_close(int fd);
int vfs_mkdir(const char* path, mode_t mode);
int vfs_unlink(const char* path);
dirent_t* vfs_readdir(int fd);

void init_net(void);
int net_create_socket(int domain, int type, int protocol);
int net_bind(int sock, uint32_t ip, uint16_t port);
int net_listen(int sock, int backlog);
int net_accept(int sock);
int net_connect(int sock, uint32_t ip, uint16_t port);
int net_send(int sock, const void* buf, size_t len);
int net_recv(int sock, void* buf, size_t len);
int net_close(int sock);

void init_crypto(void);
void aes_encrypt(const uint8_t* key, const uint8_t* input, uint8_t* output);
void aes_decrypt(const uint8_t* key, const uint8_t* input, uint8_t* output);
void sha256(const uint8_t* data, size_t len, uint8_t* hash);
void md5(const uint8_t* data, size_t len, uint8_t* hash);

void init_anon(void);
void tor_enable_system_wide(uint16_t socks_port, uint16_t control_port);
void mac_spoof_schedule(uint32_t interval_seconds);
void hostname_randomize(void);
void disable_ipv6_leaks(void);
void disable_webrtc_leaks(void);
void disable_dns_leaks(void);

void init_timer(uint32_t frequency);
void update_timer(void);
uint32_t get_ticks(void);
void sleep(uint32_t milliseconds);

void init_keyboard(void);
char getchar(void);
char getchar_poll(void);
void keylog_init(void);
void keylog_dump(void);

void init_screen(void);
void putchar(char c);
void puts(const char* str);
int printf(const char* fmt, ...);
int vprintf(const char* fmt, va_list args);
void clear_screen(void);
char* itoa(int value, char* str, int base);
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg);
void set_terminal_color(uint8_t color);
uint8_t get_terminal_color(void);

void init_ext2(void);

// Network driver and polling
void kernel_poll_net(void);
int rtl8139_init(void);
void eth_poll(int iface_idx);
void arp_init(void);
void udp_register_listener(uint16_t port, void (*handler)(uint8_t*, uint32_t, uint32_t, uint16_t));
int icmp_ping(uint32_t dst_ip, int count, int iface_idx);
void init_background_tasks(void);
void run_background_tasks(void);

// New VFS helpers
const char* vfs_getcwd(void);
int vfs_chdir(const char* path);
void vfs_list_dir(const char* path);
void vfs_cat_file(const char* path);
int vfs_touch(const char* path);
int vfs_cp(const char* src, const char* dst);
void vfs_rename(const char* old, const char* new);

// ===== STUBS PARA MÓDULOS (declaraciones) =====
int net_create_raw_socket(void);
void kernel_thread_create(const char *name, void (*func)(void), void *arg);
void* hook_irq(int irq, void (*handler)(void));
void* allocate_remote_memory(process_t *proc, size_t size);
void write_remote_memory(process_t *proc, void *dest, const void *src, size_t len);
uint32_t get_kernel_symbol(const char *name);
void remote_syscall(process_t *proc, uint32_t func, void *arg1, void *arg2);
void init_raw_socket(void);
void load_wifi_firmware(void);
char scancode_to_ascii(uint8_t sc);
int snprintf(char *buf, size_t size, const char *fmt, ...);
char* strcasestr(const char *haystack, const char *needle);

// ============================================================
// DECLARACIONES PARA CAMBIO DE DISTRIBUCIÓN DE TECLADO
// ============================================================
extern int keyboard_layout;                 // 0 = US, 1 = ES
void set_keyboard_layout(int layout);

// ===== SOPORTE PARA MODO GRÁFICO VGA =====
void vga_set_mode_13h(void);
void vga_copy_buffer(uint8_t* buffer);

// ===== FUNCIÓN PARA EJECUTAR DOOM =====
void run_doom(void);

#endif // KERNEL_H