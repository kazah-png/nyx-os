#include "libc.h"

/* =========== Memory =========== */

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

/* Simple free-list allocator */
typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_base = 0;

#define HEAP_HEADER_SIZE ((size_t)sizeof(heap_block_t))

static void* grow_heap(size_t min_size) {
    size_t page_size = 4096;
    if (min_size + HEAP_HEADER_SIZE > page_size) {
        page_size = min_size + HEAP_HEADER_SIZE + 4096;
    }
    page_size = (page_size + 0xFFF) & ~0xFFFULL;
    void* mem = (void*)sbrk((long)page_size);
    if ((long)mem < 0) return 0;

    heap_block_t* new_block = (heap_block_t*)mem;
    new_block->size = page_size - HEAP_HEADER_SIZE;
    new_block->free = 0;
    new_block->next = 0;

    if (!heap_base) {
        heap_base = new_block;
    } else {
        heap_block_t* b = heap_base;
        while (b->next) b = b->next;
        b->next = new_block;
    }
    return new_block;
}

void* malloc(size_t size) {
    if (size == 0) size = 1;
    size = (size + 7) & ~7;

    if (!heap_base) {
        heap_block_t* hb = (heap_block_t*)grow_heap(size);
        if (!hb) return 0;
    }

    heap_block_t* block = heap_base;
    while (block) {
        if (block->free && block->size >= size) {
            if (block->size >= size + HEAP_HEADER_SIZE + 16) {
                heap_block_t* new_block = (heap_block_t*)((char*)block + HEAP_HEADER_SIZE + size);
                new_block->size = block->size - size - HEAP_HEADER_SIZE;
                new_block->free = 1;
                new_block->next = block->next;
                block->size = size;
                block->next = new_block;
            }
            block->free = 0;
            return (void*)((char*)block + HEAP_HEADER_SIZE);
        }
        block = block->next;
    }

    heap_block_t* new_block = (heap_block_t*)grow_heap(size);
    if (!new_block) return 0;
    if (new_block->size >= size + HEAP_HEADER_SIZE + 16) {
        heap_block_t* leftover = (heap_block_t*)((char*)new_block + HEAP_HEADER_SIZE + size);
        leftover->size = new_block->size - size - HEAP_HEADER_SIZE;
        leftover->free = 1;
        leftover->next = 0;
        new_block->size = size;
        new_block->next = leftover;
    }
    new_block->free = 0;
    return (void*)((char*)new_block + HEAP_HEADER_SIZE);
}

void free(void* ptr) {
    if (!ptr) return;
    heap_block_t* block = (heap_block_t*)((char*)ptr - HEAP_HEADER_SIZE);
    block->free = 1;

    if (block->next && block->next->free) {
        block->size += HEAP_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
    }

    heap_block_t* prev = heap_base;
    while (prev && prev->next != block) prev = prev->next;
    if (prev && prev->free) {
        prev->size += HEAP_HEADER_SIZE + block->size;
        prev->next = block->next;
    }
}

/* =========== String =========== */

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    while (--n && *s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char* strcat(char* dest, const char* src) {
    strcpy(dest + strlen(dest), src);
    return dest;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return 0;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    while (*haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
        haystack++;
    }
    return 0;
}

/* =========== Stdlib =========== */

int atoi(const char* s) {
    int n = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return sign * n;
}

int abs(int x) {
    return x < 0 ? -x : x;
}

/* =========== Stdio =========== */

void putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
}

int puts(const char* s) {
    int n = (int)strlen(s);
    write(1, s, n);
    putchar('\n');
    return n + 1;
}

static void pchar(char c) {
    char ch = c;
    write(1, &ch, 1);
}

static void print_u64(unsigned long long val, int base, int pad, char padchar) {
    char buf[32];
    int i = 0;
    if (val == 0) { buf[i++] = '0'; }
    while (val > 0 && i < 31) {
        int d = val % base;
        buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        val /= base;
    }
    while (i < pad) buf[i++] = padchar;
    while (i > 0) pchar(buf[--i]);
}

static void print_int(long long val, int pad, char padchar) {
    if (val < 0) { pchar('-'); val = -val; }
    print_u64((unsigned long long)val, 10, pad, padchar);
}

static void print_string(const char* s, int pad, char padchar) {
    if (!s) s = "(null)";
    int len = (int)strlen(s);
    for (int i = 0; i < pad - len; i++) pchar(padchar);
    for (int i = 0; i < len; i++) pchar(s[i]);
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt != '%') { pchar(*fmt); fmt++; continue; }
        fmt++;
        int pad = 0;
        char padchar = ' ';
        if (*fmt == '0') { padchar = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { pad = pad * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
            case 'd':
            case 'i': { int v = va_arg(args, int); print_int(v, pad, padchar); break; }
            case 'u': { unsigned int v = va_arg(args, unsigned int); print_u64(v, 10, pad, padchar); break; }
            case 'x':
            case 'X': { unsigned int v = va_arg(args, unsigned int); print_u64(v, 16, pad, padchar); break; }
            case 'l': {
                fmt++;
                if (*fmt == 'u') { unsigned long v = va_arg(args, unsigned long); print_u64(v, 10, pad, padchar); }
                else if (*fmt == 'x' || *fmt == 'X') { unsigned long v = va_arg(args, unsigned long); print_u64(v, 16, pad, padchar); }
                else if (*fmt == 'd' || *fmt == 'i') { long v = va_arg(args, long); print_int(v, pad, padchar); }
                break;
            }
            case 'p': { unsigned long v = va_arg(args, unsigned long); pchar('0'); pchar('x'); print_u64(v, 16, pad - 2, padchar); break; }
            case 's': { const char* s = va_arg(args, const char*); print_string(s, pad, padchar); break; }
            case 'c': { int c = va_arg(args, int); pchar((char)c); for (int i = 1; i < pad; i++) pchar(padchar); break; }
            case '%': pchar('%'); break;
            default: pchar('%'); pchar(*fmt); break;
        }
        fmt++;
    }
    va_end(args);
    return 0;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t pos = 0;
    while (*fmt && pos + 1 < size) {
        if (*fmt != '%') { buf[pos++] = *fmt; fmt++; continue; }
        fmt++;
        switch (*fmt) {
            case 'd': {
                int v = va_arg(args, int);
                char tmp[32]; int ti = 0; int neg = 0;
                if (v < 0) { neg = 1; v = -v; }
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                if (neg && pos + 1 < size) buf[pos++] = '-';
                while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s && pos + 1 < size) buf[pos++] = *s++;
                break;
            }
            case 'x': case 'X': {
                unsigned int v = va_arg(args, unsigned int);
                char tmp[32]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { int d = v % 16; tmp[ti++] = (d < 10) ? ('0' + d) : ('a' + d - 10); v /= 16; }
                while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
                break;
            }
            case 'p': {
                unsigned long v = va_arg(args, unsigned long);
                if (pos + 1 < size) buf[pos++] = '0';
                if (pos + 1 < size) buf[pos++] = 'x';
                char tmp[32]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { int d = v % 16; tmp[ti++] = (d < 10) ? ('0' + d) : ('a' + d - 10); v /= 16; }
                while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
                break;
            }
            case 'u': {
                unsigned int v = va_arg(args, unsigned int);
                char tmp[32]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                while (ti > 0 && pos + 1 < size) buf[pos++] = tmp[--ti];
                break;
            }
            case 'c': { int c = va_arg(args, int); buf[pos++] = (char)c; break; }
            default: if (pos + 1 < size) buf[pos++] = *fmt; break;
        }
        fmt++;
    }
    buf[pos] = '\0';
    va_end(args);
    return (int)pos;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t pos = 0;
    while (*fmt) {
        if (*fmt != '%') { buf[pos++] = *fmt; fmt++; continue; }
        fmt++;
        switch (*fmt) {
            case 'd': {
                int v = va_arg(args, int);
                char tmp[32]; int ti = 0; int neg = 0;
                if (v < 0) { neg = 1; v = -v; }
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                if (neg) buf[pos++] = '-';
                while (ti > 0) buf[pos++] = tmp[--ti];
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                while (*s) buf[pos++] = *s++;
                break;
            }
            case 'x': case 'X': {
                unsigned int v = va_arg(args, unsigned int);
                char tmp[32]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { int d = v % 16; tmp[ti++] = (d < 10) ? ('0' + d) : ('a' + d - 10); v /= 16; }
                while (ti > 0) buf[pos++] = tmp[--ti];
                break;
            }
            case 'p': {
                unsigned long v = va_arg(args, unsigned long);
                buf[pos++] = '0';
                buf[pos++] = 'x';
                char tmp[32]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { int d = v % 16; tmp[ti++] = (d < 10) ? ('0' + d) : ('a' + d - 10); v /= 16; }
                while (ti > 0) buf[pos++] = tmp[--ti];
                break;
            }
            case 'u': {
                unsigned int v = va_arg(args, unsigned int);
                char tmp[32]; int ti = 0;
                if (v == 0) tmp[ti++] = '0';
                while (v > 0 && ti < 30) { tmp[ti++] = '0' + (v % 10); v /= 10; }
                while (ti > 0) buf[pos++] = tmp[--ti];
                break;
            }
            case 'c': { int c = va_arg(args, int); buf[pos++] = (char)c; break; }
            default: buf[pos++] = *fmt; break;
        }
        fmt++;
    }
    buf[pos] = '\0';
    va_end(args);
    return (int)pos;
}
