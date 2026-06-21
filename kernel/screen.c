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

void putchar(char c) {
    if (pipe_active && pipe_pos < 4096) {
        pipe_buffer[pipe_pos++] = c;
    }
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        scroll_up();
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\b') {
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
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, terminal_color);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll_up();
    update_cursor();
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
int vprintf(const char* fmt, va_list args) {
    char buf[32];
    int count = 0;  // contador de caracteres escritos
    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            switch (*fmt) {
                case 's': {
                    char* s = va_arg(args, char*);
                    while (*s) { putchar(*s++); count++; }
                    break;
                }
                case 'd': {
                    int d = va_arg(args, int);
                    itoa(d, buf, 10);
                    char *p = buf;
                    while (*p) { putchar(*p++); count++; }
                    break;
                }
                case 'x': {
                    uint32_t x = va_arg(args, uint32_t);
                    itoa(x, buf, 16);
                    putchar('0'); putchar('x'); count += 2;
                    char *p = buf;
                    while (*p) { putchar(*p++); count++; }
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
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    if (value < 0 && base == 10) {
        *ptr++ = '-';
        value = -value;
    }
    low = ptr;
    do {
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
        value /= base;
    } while (value);
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