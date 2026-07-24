#include "fake_stdlib.h"
#include "kernel.h"

#define MAX_OPEN_FILES 16

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t pos;
    int fd;
    int in_use;
} nyx_cookie_t;

static nyx_cookie_t cookies[MAX_OPEN_FILES];

#define FILE_MAGIC 0x4E59
typedef struct {
    int magic;
    int index;
} nyx_file_hdr;

static nyx_file_hdr null_file = { FILE_MAGIC, -1 };
static nyx_file_hdr out_file = { FILE_MAGIC, -2 };
static nyx_file_hdr err_file = { FILE_MAGIC, -3 };

FILE *stdin = (FILE*)&null_file;
FILE *stdout = (FILE*)&out_file;
FILE *stderr = (FILE*)&err_file;

int errno = 0;

static nyx_cookie_t* get_cookie(FILE* f) {
    nyx_file_hdr* hdr = (nyx_file_hdr*)f;
    if (!f || hdr->magic != FILE_MAGIC) return NULL;
    if (hdr->index < 0 || hdr->index >= MAX_OPEN_FILES) return NULL;
    if (!cookies[hdr->index].in_use) return NULL;
    return &cookies[hdr->index];
}

static int alloc_cookie(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!cookies[i].in_use) {
            cookies[i].in_use = 1;
            cookies[i].data = NULL;
            cookies[i].size = 0;
            cookies[i].pos = 0;
            cookies[i].fd = -1;
            return i;
        }
    }
    return -1;
}

void *memcpy(void *dest, const void *src, size_t n) {
    memcpy_asm(dest, src, n);
    return dest;
}

void *memset(void *s, int c, size_t n) {
    memset_asm(s, (uint8_t)c, n);
    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    return memcpy(dest, src, n);
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *orig = dest;
    while ((*dest++ = *src++) != '\0');
    return orig;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcat(char *dest, const char *src) {
    char *orig = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++) != '\0');
    return orig;
}

char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (char*)last;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a = accept;
        while (*a) { if (*s == *a) return (char*)s; a++; }
        s++;
    }
    return NULL;
}

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    while (*s) {
        const char *r = reject;
        int found = 0;
        while (*r) { if (*s == *r) { found = 1; break; } r++; }
        if (found) break;
        count++; s++;
    }
    return count;
}

size_t strspn(const char *s, const char *accept) {
    size_t count = 0;
    while (*s) {
        const char *a = accept;
        int found = 0;
        while (*a) { if (*s == *a) { found = 1; break; } a++; }
        if (!found) break;
        count++; s++;
    }
    return count;
}

char *strtok(char *str, const char *delim) {
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

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *nv = malloc(len);
    if (nv) strcpy(nv, s);
    return nv;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2 && ((*s1|0x20) == (*s2|0x20))) { s1++; s2++; }
    return (*(unsigned char *)s1|0x20) - (*(unsigned char *)s2|0x20);
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s2 && ((*s1|0x20) == (*s2|0x20))) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return (*(unsigned char *)s1|0x20) - (*(unsigned char *)s2|0x20);
}

void *malloc(size_t size) { return kmalloc(size); }
void free(void *ptr) { kfree(ptr); }
void abort(void) { while(1); }
int abs(int j) { return j < 0 ? -j : j; }
long int labs(long int j) { return j < 0 ? -j : j; }

int atoi(const char *s) {
    int sign = 1, result = 0;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { result = result * 10 + (*s - '0'); s++; }
    return sign * result;
}

long int strtol(const char *s, char **end, int base) {
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

void exit(int status) { (void)status; while(1); }

FILE *fopen(const char *path, const char *mode) {
    (void)mode;
    extern void serial_puts(const char*);
    extern uint8_t* doom_wad_data;
    extern uint32_t doom_wad_size;
    
    // Special handling for doom1.wad - use direct memory WAD
    if (doom_wad_data && doom_wad_size > 0) {
        if (strcmp(path, "doom1.wad") == 0 || strcmp(path, "/doom1.wad") == 0 || strcmp(path, "/boot/doom1.wad") == 0) {
            int idx = alloc_cookie();
            if (idx < 0) return NULL;
            cookies[idx].data = doom_wad_data;
            cookies[idx].size = doom_wad_size;
            cookies[idx].pos = 0;
            cookies[idx].fd = -1;  // Special marker for direct memory
            nyx_file_hdr* hdr = (nyx_file_hdr*)kmalloc(sizeof(nyx_file_hdr));
            if (!hdr) { cookies[idx].in_use = 0; return NULL; }
            hdr->magic = FILE_MAGIC;
            hdr->index = idx;
            return (FILE*)hdr;
        }
    }
    
    int idx = alloc_cookie();
    if (idx < 0) return NULL;
    int fd = vfs_open(path, 0, 0);
    if (fd < 0 && path[0] != '/') {
        char altpath[128];
        int w = snprintf(altpath, sizeof(altpath), "/boot/%s", path);
        if (w > 0 && w < (int)sizeof(altpath)) fd = vfs_open(altpath, 0, 0);
        if (fd < 0) {
            w = snprintf(altpath, sizeof(altpath), "/%s", path);
            if (w > 0 && w < (int)sizeof(altpath)) fd = vfs_open(altpath, 0, 0);
        }
    }
    if (fd < 0) { 
        cookies[idx].in_use = 0; 
        return NULL; 
    }
    cookies[idx].data = vfs_fdata(fd);
    cookies[idx].size = vfs_fsize(fd);
    cookies[idx].pos = 0;
    cookies[idx].fd = fd;
    nyx_file_hdr* hdr = (nyx_file_hdr*)kmalloc(sizeof(nyx_file_hdr));
    if (!hdr) { vfs_close(fd); cookies[idx].in_use = 0; return NULL; }
    hdr->magic = FILE_MAGIC;
    hdr->index = idx;
    return (FILE*)hdr;
}

int fclose(FILE *stream) {
    nyx_cookie_t* c = get_cookie(stream);
    if (!c) return EOF;
    if (c->fd >= 0) vfs_close(c->fd);
    c->in_use = 0;
    kfree(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream) {
    nyx_cookie_t* c = get_cookie(stream);
    if (!c) return 0;
    size_t total = size * count;
    if (c->pos >= c->size) return 0;
    if (c->pos + total > c->size) total = c->size - c->pos;
    if (total == 0) return 0;
    memcpy(ptr, c->data + c->pos, total);
    c->pos += total;
    return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream) {
    (void)ptr; (void)size; (void)count; (void)stream;
    return count;
}

int fseek(FILE *stream, long offset, int whence) {
    nyx_cookie_t* c = get_cookie(stream);
    if (!c) return -1;
    switch (whence) {
        case SEEK_SET: c->pos = (uint32_t)offset; break;
        case SEEK_CUR: c->pos += (uint32_t)offset; break;
        case SEEK_END: c->pos = c->size + (uint32_t)offset; break;
    }
    if (c->pos > c->size) c->pos = c->size;
    return 0;
}

long ftell(FILE *stream) {
    nyx_cookie_t* c = get_cookie(stream);
    if (!c) return -1;
    return (long)c->pos;
}

int feof(FILE *stream) {
    nyx_cookie_t* c = get_cookie(stream);
    if (!c) return 1;
    return c->pos >= c->size;
}

int ferror(FILE *stream) { (void)stream; return 0; }

char *strncat(char *dest, const char *src, size_t n) {
    char *orig = dest;
    while (*dest) dest++;
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
    return orig;
}

int sscanf(const char *str, const char *format, ...) {
    (void)str; (void)format;
    return 0;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    char *p = buf;
    char *end = buf + size - 1;
    const char *f = fmt;
    
    if (size == 0) return 0;
    
    while (*f && p < end) {
        if (*f == '%') {
            f++;
            if (*f == '%') {
                if (p < end) *p++ = '%';
            } else {
                int pad = 0, zeropad = 0, precision = -1;
                while (*f == '0') { zeropad = 1; f++; }
                while (*f >= '0' && *f <= '9') { pad = pad * 10 + (*f - '0'); f++; }
                if (*f == '.') {
                    f++;
                    precision = 0;
                    while (*f >= '0' && *f <= '9') { precision = precision * 10 + (*f - '0'); f++; }
                }
                if (*f == 's') {
                    const char *s = va_arg(args, const char*);
                    if (!s) s = "(null)";
                    int slen = 0;
                    while (s[slen]) slen++;
                    if (precision >= 0 && precision < slen) slen = precision;
                    if (pad > slen) {
                        int spaces = pad - slen;
                        while (spaces-- > 0 && p < end) *p++ = ' ';
                    }
                    int i = 0;
                    while (i < slen && p < end) *p++ = s[i++];
                } else if (*f == 'd' || *f == 'i') {
                    int val = va_arg(args, int);
                    char tmp[16];
                    int len = 0, neg = 0;
                    if (val < 0) { neg = 1; val = -val; }
                    int v = val;
                    do { tmp[len++] = '0' + v % 10; v /= 10; } while (v > 0);
                    if (precision > len) {
                        int z = precision - len;
                        while (z-- > 0) tmp[len++] = '0';
                    }
                    int total = len + neg;
                    if (zeropad && pad > total) {
                        if (neg && p < end) *p++ = '-';
                        int z = pad - total;
                        while (z-- > 0 && p < end) *p++ = '0';
                        while (len > 0 && p < end) *p++ = tmp[--len];
                    } else {
                        if (pad > total) {
                            int spaces = pad - total;
                            while (spaces-- > 0 && p < end) *p++ = ' ';
                        }
                        if (neg && p < end) *p++ = '-';
                        while (len > 0 && p < end) *p++ = tmp[--len];
                    }
                } else if (*f == 'c') {
                    int c = va_arg(args, int);
                    if (p < end) *p++ = (char)c;
                } else if (*f == 'x' || *f == 'X') {
                    unsigned int val = va_arg(args, unsigned int);
                    char tmp[16];
                    int len = 0;
                    unsigned int v = val;
                    do {
                        int d = v % 16;
                        tmp[len++] = d < 10 ? '0' + d : (*f == 'X' ? 'A' : 'a') + d - 10;
                        v /= 16;
                    } while (v > 0);
                    if (precision > len) {
                        int z = precision - len;
                        while (z-- > 0) tmp[len++] = '0';
                    }
                    if (pad > len) {
                        int spaces = pad - len;
                        while (spaces-- > 0 && p < end) *p++ = ' ';
                    }
                    while (len > 0 && p < end) *p++ = tmp[--len];
                } else if (*f == 'u') {
                    unsigned int val = va_arg(args, unsigned int);
                    char tmp[16];
                    int len = 0;
                    unsigned int v = val;
                    do { tmp[len++] = '0' + v % 10; v /= 10; } while (v > 0);
                    if (precision > len) {
                        int z = precision - len;
                        while (z-- > 0) tmp[len++] = '0';
                    }
                    if (pad > len) {
                        int spaces = pad - len;
                        while (spaces-- > 0 && p < end) *p++ = ' ';
                    }
                    while (len > 0 && p < end) *p++ = tmp[--len];
                } else {
                    if (p < end) *p++ = '%';
                    if (p < end) *p++ = *f;
                }
            }
        } else {
            *p++ = *f;
        }
        f++;
    }
    *p = '\0';
    return p - buf;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    void *nv = kmalloc(size);
    if (nv) memcpy(nv, ptr, size);
    kfree(ptr);
    return nv;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

int remove(const char *pathname) {
    (void)pathname;
    return -1;
}

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return -1;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    (void)stream;
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
    return ret;
}

int vfprintf(FILE *stream, const char *fmt, va_list args) {
    (void)stream;
    return vprintf(fmt, args);
}

int system(const char *command) {
    (void)command;
    return -1;
}

double atof(const char *s) {
    (void)s;
    return 0.0;
}

int mkdir(const char *pathname) {
    return vfs_mkdir(pathname, 0755);
}

double sin(double x) {
    double result = 0;
    double term = x;
    int sign = 1;
    for (int n = 1; n <= 15; n += 2) {
        result += sign * term;
        sign = -sign;
        term = term * x * x / ((n+1)*(n+2));
    }
    return result;
}

double cos(double x) {
    double result = 1;
    double term = 1;
    int sign = -1;
    for (int n = 2; n <= 14; n += 2) {
        term = term * x * x / ((n-1)*n);
        result += sign * term;
        sign = -sign;
    }
    return result;
}

double sqrt(double x) {
    if (x == 0) return 0;
    double guess = x / 2;
    double prev;
    do {
        prev = guess;
        guess = (guess + x / guess) / 2;
    } while (guess != prev);
    return guess;
}

double fabs(double x) { return x < 0 ? -x : x; }
double pow(double x, double y) { (void)x; (void)y; return 0; }
double floor(double x) { return (double)(int)x; }
double ceil(double x) { return (double)((int)x + (x > 0 ? 1 : 0)); }
