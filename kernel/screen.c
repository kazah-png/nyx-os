// ============================================================
// screen.c - Controlador de pantalla VGA de NyxOS
// Tema negro con soporte de cambio de color
// ============================================================
#include "kernel.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t terminal_color = 0x0F;

// Pipe capture buffer
static char pipe_buffer[4096];
static int pipe_pos = 0;
static int pipe_active = 0;

// Terminal capture hook
static int (*putchar_hook)(int c) = NULL;

void set_putchar_hook(int (*hook)(int c)) {
    putchar_hook = hook;
}

void pipe_start(void) {
    pipe_pos = 0;
    pipe_active = 1;
}

int pipe_stop(void) {
    pipe_active = 0;
    if (pipe_pos < 4096) pipe_buffer[pipe_pos] = '\0';
    return pipe_pos;
}

const char* pipe_get_data(void) {
    return pipe_buffer;
}

int pipe_get_len(void) {
    return pipe_pos;
}

// El enum vga_color ya está en kernel.h, no se redefine

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

void init_screen(void) {
    cursor_x = 0;
    cursor_y = 0;
    terminal_color = vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
}

void update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

void scroll_up(void) {
    if (cursor_y >= VGA_HEIGHT) {
        for (int y = 0; y < VGA_HEIGHT - 1; y++) {
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
            }
        }
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
        cursor_y = VGA_HEIGHT - 1;
    }
}

int putchar(int c) {
    char ch = (char)c;
    if (putchar_hook) putchar_hook(c);
    if (pipe_active && pipe_pos < 4096) {
        pipe_buffer[pipe_pos++] = ch;
    }
    serial_putchar(ch);
    if (ch == '\n') {
        cursor_x = 0;
        cursor_y++;
        scroll_up();
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b' || c == 0x7F) {
        if (cursor_x > 0) {
            cursor_x--;
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(' ', terminal_color);
        }
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    } else {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(ch, terminal_color);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll_up();
    update_cursor();
    return c;
}

int puts(const char* str) {
    int n = 0;
    while (*str) {
        putchar(*str++);
        n++;
    }
    putchar('\n');
    n++;
    return n;
}

// Cambiado: ahora devuelve int (número de caracteres escritos, aunque no se calcula exactamente)
int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
    return ret;
}

// Cambiado: ahora devuelve int
static void print_u64(uint64_t v, int base) {
    char buf[24];
    int i = 23;
    buf[i] = '\0';
    do {
        buf[--i] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    char* p = &buf[i];
    while (*p) { putchar(*p++); }
}

// Render an unsigned value into out[] (no prefix), returning its length.
static int u64_to_str(uint64_t v, int base, char* out) {
    char tmp[24]; int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = "0123456789abcdef"[v % base]; v /= base; }
    for (int j = 0; j < i; j++) out[j] = tmp[i - 1 - j];
    out[i] = '\0';
    return i;
}

// Emit a string padded to `width`. Right-aligned pads on the left with `pad`
// ('0' or ' '); left-aligned (`left`) always pads on the right with spaces.
static void emit_padded(const char* s, int width, int left, char pad, int* count) {
    int len = 0; for (const char* q = s; *q; q++) len++;
    int npad = width > len ? width - len : 0;
    if (!left) for (int i = 0; i < npad; i++) { putchar(pad); (*count)++; }
    for (const char* p = s; *p; p++) { putchar(*p); (*count)++; }
    if (left)  for (int i = 0; i < npad; i++) { putchar(' '); (*count)++; }
}

int vprintf(const char* fmt, va_list args) {
    int count = 0;
    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            // Flags: '-' left-align, '0' zero-pad.
            int left = 0, zero = 0;
            for (;;) {
                if (*fmt == '-') { left = 1; fmt++; }
                else if (*fmt == '0') { zero = 1; fmt++; }
                else break;
            }
            // Width (decimal). Precision is not supported.
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
            // Length modifier.
            int long_count = 0;
            while (*fmt == 'l') { long_count++; fmt++; }
            char pad = (zero && !left) ? '0' : ' ';
            char nbuf[32];
            switch (*fmt) {
                case 's': {
                    char* s = va_arg(args, char*);
                    if (!s) s = "(null)";
                    emit_padded(s, width, left, ' ', &count);   // strings always space-pad
                    break;
                }
                case 'd':
                case 'i': {
                    int64_t v = (long_count >= 2) ? va_arg(args, int64_t)
                                                  : (int64_t)va_arg(args, int);
                    int neg = v < 0;
                    uint64_t uv = neg ? (uint64_t)(-v) : (uint64_t)v;
                    int k = 0;
                    if (neg) nbuf[k++] = '-';
                    k += u64_to_str(uv, 10, nbuf + k);
                    nbuf[k] = '\0';
                    emit_padded(nbuf, width, left, pad, &count);
                    break;
                }
                case 'u': {
                    uint64_t v = (long_count >= 2) ? va_arg(args, uint64_t)
                                                   : (uint64_t)va_arg(args, unsigned int);
                    u64_to_str(v, 10, nbuf);
                    emit_padded(nbuf, width, left, pad, &count);
                    break;
                }
                case 'x':
                case 'X': {
                    uint64_t v = (long_count >= 1) ? va_arg(args, uint64_t)
                                                   : (uint64_t)va_arg(args, unsigned int);
                    // Standard C %x: NO auto "0x" prefix (matches vsnprintf + userspace
                    // libc). Callers write "0x%x" when they want the prefix; the old
                    // auto-prefix produced "0x0x…" for every such site and uglified hex
                    // dumps / MAC octets ("0x52:0x54"). Use %p for a prefixed pointer.
                    u64_to_str(v, 16, nbuf);
                    emit_padded(nbuf, width, left, pad, &count);
                    break;
                }
                case 'p': {
                    void* p = va_arg(args, void*);
                    putchar('0'); putchar('x'); count += 2;
                    print_u64((uint64_t)(uintptr_t)p, 16);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    putchar(c);
                    count++;
                    break;
                }
                case '%':
                    putchar('%');
                    count++;
                    break;
                default:
                    putchar('%');
                    putchar(*fmt);
                    count += 2;
                    break;
            }
        } else {
            putchar(*fmt);
            count++;
        }
        fmt++;
    }
    return count;
}

void clear_screen(void) {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
        }
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

// itoa (ya está aquí, no se duplica en kernel.c)
char* itoa(int value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    unsigned int uvalue;
    if (base < 2 || base > 36) {
        if (str) *str = '\0';
        return str;
    }
    if (!str) return "";
    rc = ptr = str;
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        uvalue = (unsigned int)(-value);
    } else {
        uvalue = (unsigned int)value;
    }
    low = ptr;
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[uvalue % base];
        uvalue /= base;
    } while (uvalue);
    *ptr-- = '\0';
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

char* lltoa(long long value, char* str, int base) {
    char* rc;
    char* ptr;
    char* low;
    unsigned long long uvalue;
    if (base < 2 || base > 36) {
        if (str) *str = '\0';
        return str;
    }
    if (!str) return "";
    rc = ptr = str;
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        uvalue = (unsigned long long)(-value);
    } else {
        uvalue = (unsigned long long)value;
    }
    low = ptr;
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[uvalue % base];
        uvalue /= base;
    } while (uvalue);
    *ptr-- = '\0';
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

void set_terminal_color(uint8_t color) {
    terminal_color = color;
}

uint8_t get_terminal_color(void) {
    return terminal_color;
}