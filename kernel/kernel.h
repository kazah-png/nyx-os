// ============================================================
// kernel.h - Cabecera principal del kernel NyxOS v2.1.1
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
#define KERNEL_VERSION "3.1.0"
#define KERNEL_CODENAME "Nightfall"
#define KERNEL_DATE    "2026"

#define ARCH_X86 1
#define BITS_32  1

#define PAGE_SIZE        4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define KERNEL_BASE      0xC0000000
#define KERNEL_HEAP_START 0xD0000000
#define KERNEL_HEAP_SIZE  (16 * 1024 * 1024)  // 16 MB (matches heap.c)

// Segment selectors
#define KERNEL_CS  0x08
#define KERNEL_DS  0x10
#define USER_CS   0x1B
#define USER_DS   0x23
#define TSS_SEL   0x28

#define SYSCALL_INT 0x80

#define MAX_PROCESSES    512
#define MAX_THREADS      1024
#define MAX_FILES        256
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
    uint32_t state;
    uint32_t priority;
    uint32_t capabilities;
    uint32_t stealth_level;
    uint32_t cpu_time;
    uint32_t start_time;
    void* files[MAX_FILES];
    uint32_t program_break;
    struct process* next;
    struct process* parent;
    struct process* children;
} process_t;

typedef struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

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
extern uint32_t saved_esp;
extern uint32_t next_esp;

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
static inline void *memmove(void *dest, const void *src, size_t n) {
    if (dest == src) return dest;
    // Simple byte-by-byte copy (forward/backward as needed)
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
void launch_shell(void);
void nyxfetch(void);
void execute_command(const char* cmd_line);
void command_complete(const char* partial, char* out, int out_size, int* match_count);
void command_list_matches(const char* partial, char* out, int out_size);

void init_gdt(void);
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void tss_set_stack(uint32_t esp0);
void load_tss(void);
void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void init_isr(void);
void isr_handler(uint32_t int_no, uint32_t err_code);
void init_irq(void);
void irq_handler(uint32_t irq_no);
void irq_install_handler(int irq, void (*handler)(void*));
void irq_unmask(int irq);
void irq_mask(int irq);

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
uint32_t* alloc_page_directory(void);
uint32_t* get_kernel_page_directory(void);
void switch_page_directory(uint32_t* pd);
void map_page_dir(uint32_t* pd, void* phys, void* virt, uint32_t flags);

void init_heap(void);
void* heap_alloc(size_t size);
void heap_free(void* ptr);

void init_process(void);
process_t* create_process(const char* name, void* entry, uint32_t flags);
process_t* create_user_process(const char* name, void* entry, void* user_stack, uint32_t* page_dir);
void switch_to_user_process(process_t* proc);
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
uint32_t syscall_handler_c(uint32_t syscall_no, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
extern void syscall_stub(void);
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
uint32_t vfs_fsize(int fd);
uint8_t* vfs_fdata(int fd);
int vfs_create_from_mem(const char* path, uint8_t* data, uint32_t size);
void init_load_modules(void);

void init_net(void);
int net_create_socket(int domain, int type, int protocol);
int net_bind(int sock, uint32_t ip, uint16_t port);
int net_listen(int sock, int backlog);
int net_accept(int sock);
int net_connect(int sock, uint32_t ip, uint16_t port);
int net_send(int sock, const void* buf, size_t len);
int net_recv(int sock, void* buf, size_t len);
int net_close(int sock);





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


void init_screen(void);
void putchar(char c);
int puts(const char* str);
void set_putchar_hook(void (*hook)(char c));
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

// ELF loader
int elf_load(const uint8_t* data, uint32_t size, process_t** out_proc);
int elf_validate(const uint8_t* data, uint32_t size);

// initramfs
int initramfs_load(void);
void initramfs_boot(void);

void init_ext2(void);
int  ata_init(void);
int  ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void* buf);
int  ext2_mount(uint8_t drive, uint32_t part_lba);

// EXT2 VFS driver (struct details in ext2.h)
uint32_t ext2_resolve(const char* path);
uint32_t ext2_get_size(const char* path);
int  ext2_read_file(const char* path, void* buf, uint32_t maxlen);
int  ext2_write_file(const char* path, const void* buf, uint32_t len);
int  ext2_create_file(const char* path);
int  ext2_readdir(const char* path, dirent_t* entries, uint32_t max_entries);

// Network driver and polling
void kernel_poll_net(void);
int rtl8139_init(void);
void eth_poll(int iface_idx);
void arp_init(void);
int udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port, const uint8_t* data, uint32_t len, int iface_idx);
void udp_register_listener(uint16_t port, void (*handler)(uint8_t*, uint32_t, uint32_t, uint16_t));
int dhcp_request(int iface_idx);
void cmd_dhcp(int argc, char** argv);
int icmp_ping(uint32_t dst_ip, int count, int iface_idx);
int tcp_init(void);
int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_send(int conn_id, const uint8_t* data, uint32_t len);
int tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len);
int tcp_close(int conn_id);
void init_background_tasks(void);
void run_background_tasks(void);
void irq_scheduler_tick(void);
void ensure_idle_process(void);

// VFS mount layer
#define FS_TYPE_EXT2  1
#define MAX_MOUNT_POINTS 8

typedef struct {
    int type;                    // FS_TYPE_EXT2, etc
    char mount_point[MAX_PATH];
    void* fs_data;              // per-filesystem data
    // Driver operations
    uint32_t (*resolve)(const char* path);
    uint32_t (*get_size)(const char* path);
    int (*read_file)(const char* path, void* buf, uint32_t maxlen);
    int (*write_file)(const char* path, const void* buf, uint32_t len);
    int (*readdir)(const char* path, dirent_t* entries, uint32_t max_entries);
} mount_entry_t;

int vfs_mount(const char* mount_point, int fs_type, void* fs_data);
mount_entry_t* vfs_find_mount(const char* path);

// New VFS helpers
const char* vfs_getcwd(void);
int vfs_chdir(const char* path);
void vfs_list_dir(const char* path);
void vfs_cat_file(const char* path);
int vfs_touch(const char* path);
int vfs_write_file(const char* path, const void* buf, uint32_t len);
int vfs_cp(const char* src, const char* dst);
void vfs_rename(const char* old, const char* new);



// ============================================================
// DECLARACIONES PARA CAMBIO DE DISTRIBUCIÓN DE TECLADO
// ============================================================
extern int keyboard_layout;                 // 0 = US, 1 = ES
void set_keyboard_layout(int layout);

// ===== UTILITY FUNCTIONS =====
int snprintf(char *buf, size_t size, const char *fmt, ...);
char* strcasestr(const char *haystack, const char *needle);

// ===== SOPORTE PARA MODO GRÁFICO VGA =====
void vga_set_mode_13h(void);
void vga_copy_buffer(uint8_t* buffer);

// ===== FUNCIÓN PARA EJECUTAR DOOM =====
void run_doom(void);

// DOOM WAD direct memory access
extern uint8_t* doom_wad_data;
extern uint32_t doom_wad_size;

// ===== VBE / FRAMEBUFFER =====
int vbe_init(void);
int vbe_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
uint32_t vbe_get_width(void);
uint32_t vbe_get_height(void);
uint32_t vbe_get_bpp(void);
void* vbe_get_lfb(void);

void fb_init(uint32_t width, uint32_t height, uint32_t bpp, void* addr);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_blit(const void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h,
             uint32_t dx, uint32_t dy);
void fb_clear(uint32_t color);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
void* fb_get_addr(void);
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

// ===== MOUSE =====
int mouse_init(void);
void mouse_irq_handler(void* unused);
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_buttons(void);
void mouse_set_pos(int x, int y);

// ===== FONT RENDERING =====
void font_draw_char(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg);
void font_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
uint32_t font_get_width(void);
uint32_t font_get_height(void);

// ===== GUI DEMO =====
void gui_demo(void);

// ===== SPEAKER =====
void speaker_init(void);
void speaker_on(uint32_t frequency);
void speaker_off(void);
void speaker_beep(uint32_t frequency, uint32_t duration_ms);

#endif // KERNEL_H