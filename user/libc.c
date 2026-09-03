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

/* =========== setjmp / longjmp =========== */
/* Naked so no prologue disturbs the RSP/RIP we capture. jmp_buf slots (8 bytes each):
 * [0]=rbx [1]=rbp [2]=r12 [3]=r13 [4]=r14 [5]=r15 [6]=caller RSP [7]=return RIP.
 * buf is in %rdi (SysV arg1); longjmp's val is in %rsi (arg2). */
__attribute__((naked)) int setjmp(jmp_buf buf) {
    (void)buf;
    __asm__ volatile(
        "movq %rbx,  0(%rdi)\n\t"
        "movq %rbp,  8(%rdi)\n\t"
        "movq %r12, 16(%rdi)\n\t"
        "movq %r13, 24(%rdi)\n\t"
        "movq %r14, 32(%rdi)\n\t"
        "movq %r15, 40(%rdi)\n\t"
        "leaq 8(%rsp), %rax\n\t"      /* caller's RSP (past our return address) */
        "movq %rax, 48(%rdi)\n\t"
        "movq (%rsp), %rax\n\t"       /* our return address = where setjmp resumes */
        "movq %rax, 56(%rdi)\n\t"
        "xorl %eax, %eax\n\t"         /* first return: 0 */
        "ret\n\t"
    );
}

__attribute__((naked, noreturn)) void longjmp(jmp_buf buf, int val) {
    (void)buf; (void)val;
    __asm__ volatile(
        "movq  0(%rdi), %rbx\n\t"
        "movq  8(%rdi), %rbp\n\t"
        "movq 16(%rdi), %r12\n\t"
        "movq 24(%rdi), %r13\n\t"
        "movq 32(%rdi), %r14\n\t"
        "movq 40(%rdi), %r15\n\t"
        "movq 48(%rdi), %rsp\n\t"     /* restore caller's stack */
        "movl %esi, %eax\n\t"         /* setjmp returns val ... */
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n\t"           /* ... but never 0 (longjmp(buf,0) -> 1) */
        "1:\n\t"
        "jmp *56(%rdi)\n\t"           /* resume at the saved setjmp return point (plain
                                         `jmp` — no `q` suffix — so TinyCC's assembler
                                         accepts it too; same machine code under GCC) */
    );
}

/* =========== sigsetjmp / siglongjmp =========== */
/* sigsetjmp is a macro (libc.h): __sigsetjmp_save stashes the signal mask, then
 * setjmp saves the register context in the CALLER's frame (so the jump lands back
 * at the sigsetjmp site). siglongjmp restores that mask before the register
 * longjmp — which unblocks the handler's signal and clears its "in handler" state
 * kernel-side — so the SAME fault can be caught again. */
void __sigsetjmp_save(sigjmp_buf buf, int savesigs) {
    buf[9] = (unsigned long)savesigs;
    if (savesigs) {
        unsigned long old = 0;
        sigprocmask(SIG_BLOCK, 0, &old);      /* read the current mask without changing it */
        buf[8] = old;
    } else {
        buf[8] = 0;
    }
}

void siglongjmp(sigjmp_buf buf, int val) {
    if (buf[9]) sigprocmask(SIG_SETMASK, buf[8], 0);   /* restore mask -> unblock + clear sig_active */
    longjmp((unsigned long*)buf, val);                  /* restore regs + jump (never returns) */
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

/* Resize an allocation, preserving its contents. The block header carries the
 * current payload capacity, so a shrink or same-size request returns the same
 * pointer; a grow allocates a new block, copies the old capacity's worth of
 * bytes and frees the old one. realloc(NULL,n)=malloc; realloc(p,0)=free,NULL.
 * A compiler (and most nontrivial programs) can't run without this. */
void* realloc(void* ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    heap_block_t* block = (heap_block_t*)((char*)ptr - HEAP_HEADER_SIZE);
    size_t old = block->size;
    if (old >= size) return ptr;              /* current block already fits */
    void* np = malloc(size);
    if (!np) return 0;                         /* old block left intact (per C standard) */
    memcpy(np, ptr, old);                      /* copy the old payload capacity */
    free(ptr);
    return np;
}

/* Allocate nmemb*size zero-filled bytes, rejecting a multiply overflow. */
void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    if (size != 0 && total / size != nmemb) return 0;   /* overflow */
    void* p = malloc(total ? total : 1);
    if (p) memset(p, 0, total);
    return p;
}

/* memcpy that tolerates overlapping regions (copies backward when dest > src). */
void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) return dest;
    if (d < s) { for (size_t i = 0; i < n; i++) d[i] = s[i]; }
    else       { for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1]; }
    return dest;
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
    char ch = (char)c;
    for (;; s++) {
        if (*s == ch) return (char*)s;   /* c == '\0' matches the terminating NUL */
        if (!*s) return 0;
    }
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

/* Length of the initial run of `s` made up entirely of bytes in `accept`. */
size_t strspn(const char* s, const char* accept) {
    const char* p = s;
    for (; *p; p++) {
        const char* a = accept;
        while (*a && *a != *p) a++;
        if (!*a) break;                    /* *p not in accept — run ends */
    }
    return (size_t)(p - s);
}

/* Length of the initial run of `s` made up of bytes NOT in `reject`. */
size_t strcspn(const char* s, const char* reject) {
    const char* p = s;
    for (; *p; p++) {
        const char* r = reject;
        while (*r && *r != *p) r++;
        if (*r) break;                     /* *p is in reject — run ends */
    }
    return (size_t)(p - s);
}

/* First byte of `s` that appears in `accept`, or NULL. */
char* strpbrk(const char* s, const char* accept) {
    for (; *s; s++)
        for (const char* a = accept; *a; a++)
            if (*a == *s) return (char*)s;
    return 0;
}

/* Reentrant tokenizer: split `str` on any byte in `delim`, carrying state in
 * *saveptr so the caller owns it (no static). Pass str once, then NULL to
 * continue. Empty tokens between adjacent delimiters are skipped, matching the
 * standard/glibc contract exactly. */
char* strtok_r(char* str, const char* delim, char** saveptr) {
    char* s = str ? str : *saveptr;
    if (!s) return 0;
    if (*s == '\0') { *saveptr = s; return 0; }
    s += strspn(s, delim);                 /* skip leading delimiters */
    if (*s == '\0') { *saveptr = s; return 0; }
    char* end = s + strcspn(s, delim);     /* scan to the next delimiter/end */
    if (*end == '\0') { *saveptr = end; return s; }   /* last token */
    *end = '\0';
    *saveptr = end + 1;
    return s;
}

/* Non-reentrant strtok over a single hidden cursor (first call must pass str). */
char* strtok(char* str, const char* delim) {
    static char* save;
    return strtok_r(str, delim, &save);
}

/* Case-insensitive string ops (ASCII fold). strcasecmp/strncasecmp order like their
 * case-sensitive twins; strcasestr is a case-folding strstr (what `grep -i` wants). */
static char ci_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

int strcasecmp(const char* a, const char* b) {
    for (;; a++, b++) {
        char ca = ci_lower(*a), cb = ci_lower(*b);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        if (!ca) return 0;
    }
}

int strncasecmp(const char* a, const char* b, size_t n) {
    for (; n > 0; n--, a++, b++) {
        char ca = ci_lower(*a), cb = ci_lower(*b);
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        if (!ca) return 0;
    }
    return 0;
}

char* strcasestr(const char* haystack, const char* needle) {
    if (!needle[0]) return (char*)haystack;
    for (const char* h = haystack; *h; h++) {
        const char* a = h; const char* b = needle;
        while (*a && *b && ci_lower(*a) == ci_lower(*b)) { a++; b++; }
        if (!*b) return (char*)h;
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

/* The process environment: a NULL-terminated array of "NAME=VALUE" strings. crt0
 * points this at the envp vector the kernel laid on the entry stack (built by
 * execve from the parent's environment), so a child inherits its parent's env. */
char** environ = 0;

/* getenv(name): return the value of environment variable `name`, or NULL. Scans
 * `environ` for a "name=" prefix and returns the text after the '='. */
char* getenv(const char* name) {
    if (!environ) return 0;
    size_t n = strlen(name);
    for (char** e = environ; *e; e++) {
        const char* s = *e;
        if (strncmp(s, name, n) == 0 && s[n] == '=') return (char*)(s + n + 1);
    }
    return 0;
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

/* ---- printf-family shared formatting core (v6.1.4) ----
 * One formatter drives printf, snprintf/sprintf/vsnprintf AND fprintf/vfprintf via
 * an output "sink": each formatted character is handed to emit(c, ctx). This unifies
 * three previously-separate switches. printf's rich behaviour is preserved exactly
 * ('-'/'0' flags, field width, %d/%i/%u/%o/%x/%X, the %l* variants, %p/%s/%c/%%);
 * snprintf/sprintf inherit it (a strict superset of their old basic switch), and
 * `left` (the '-' flag) right-pads with spaces for left-justification. */
typedef void (*emit_fn)(char c, void* ctx);

static void emit_stdout(char c, void* ctx) { (void)ctx; pchar(c); }

typedef struct { char* buf; size_t size; size_t count; } fmt_bufsink;
static void emit_buf(char c, void* ctx) {
    fmt_bufsink* s = (fmt_bufsink*)ctx;
    if (s->size && s->count + 1 < s->size) s->buf[s->count] = c;  /* keep room for NUL */
    s->count++;                                                   /* C99 would-be length */
}

typedef struct { FILE* f; int count; } fmt_filesink;
static void emit_file(char c, void* ctx) {
    fmt_filesink* s = (fmt_filesink*)ctx;
    fputc((unsigned char)c, s->f);
    s->count++;
}

static void fmt_u64(emit_fn emit, void* ctx, unsigned long long val, int base, int pad, char padchar, int left, int upper) {
    char buf[32];
    int n = 0;
    char alpha = upper ? 'A' : 'a';   /* 'X' wants A-F, 'x'/'p' want a-f */
    if (val == 0) { buf[n++] = '0'; }
    while (val > 0 && n < 31) {
        int d = (int)(val % base);
        buf[n++] = (d < 10) ? ('0' + d) : (alpha + d - 10);
        val /= base;
    }
    int digits = n;
    if (left) {
        while (n > 0) emit(buf[--n], ctx);
        for (int i = digits; i < pad; i++) emit(' ', ctx);
    } else {
        for (int i = digits; i < pad; i++) emit(padchar, ctx);
        while (n > 0) emit(buf[--n], ctx);
    }
}

static void fmt_int(emit_fn emit, void* ctx, long long val, int pad, char padchar, int left) {
    if (val < 0) { emit('-', ctx); val = -val; }
    fmt_u64(emit, ctx, (unsigned long long)val, 10, pad, padchar, left, 0);
}

static void fmt_string(emit_fn emit, void* ctx, const char* s, int pad, char padchar, int left) {
    if (!s) s = "(null)";
    int len = (int)strlen(s);
    if (left) {
        for (int i = 0; i < len; i++) emit(s[i], ctx);
        for (int i = len; i < pad; i++) emit(' ', ctx);
    } else {
        for (int i = 0; i < pad - len; i++) emit(padchar, ctx);
        for (int i = 0; i < len; i++) emit(s[i], ctx);
    }
}

static void format_core(emit_fn emit, void* ctx, const char* fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') { emit(*fmt, ctx); fmt++; continue; }
        fmt++;
        int pad = 0; char padchar = ' '; int left = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { padchar = '0'; fmt++; }
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') { pad = pad * 10 + (*fmt - '0'); fmt++; }
        if (left) padchar = ' ';   /* '-' overrides '0' zero-padding (C standard) */
        switch (*fmt) {
            case 'd':
            case 'i': { int v = va_arg(args, int); fmt_int(emit, ctx, v, pad, padchar, left); break; }
            case 'u': { unsigned int v = va_arg(args, unsigned int); fmt_u64(emit, ctx, v, 10, pad, padchar, left, 0); break; }
            case 'o': { unsigned int v = va_arg(args, unsigned int); fmt_u64(emit, ctx, v, 8, pad, padchar, left, 0); break; }
            case 'x': { unsigned int v = va_arg(args, unsigned int); fmt_u64(emit, ctx, v, 16, pad, padchar, left, 0); break; }
            case 'X': { unsigned int v = va_arg(args, unsigned int); fmt_u64(emit, ctx, v, 16, pad, padchar, left, 1); break; }
            case 'z':   /* size_t / ssize_t (64-bit on LP64) */
            case 't':   /* ptrdiff_t   (64-bit on LP64) */
            case 'l': {
                fmt++;
                if (*fmt == 'l') fmt++;   /* ll: long long == long on LP64, so read one 64-bit slot */
                if      (*fmt == 'u') { unsigned long v = va_arg(args, unsigned long); fmt_u64(emit, ctx, v, 10, pad, padchar, left, 0); }
                else if (*fmt == 'x') { unsigned long v = va_arg(args, unsigned long); fmt_u64(emit, ctx, v, 16, pad, padchar, left, 0); }
                else if (*fmt == 'X') { unsigned long v = va_arg(args, unsigned long); fmt_u64(emit, ctx, v, 16, pad, padchar, left, 1); }
                else if (*fmt == 'o') { unsigned long v = va_arg(args, unsigned long); fmt_u64(emit, ctx, v, 8, pad, padchar, left, 0); }
                else if (*fmt == 'd' || *fmt == 'i') { long v = va_arg(args, long); fmt_int(emit, ctx, v, pad, padchar, left); }
                break;
            }
            case 'p': { unsigned long v = va_arg(args, unsigned long); emit('0', ctx); emit('x', ctx); fmt_u64(emit, ctx, v, 16, pad - 2, padchar, left, 0); break; }
            case 's': { const char* s = va_arg(args, const char*); fmt_string(emit, ctx, s, pad, padchar, left); break; }
            case 'c': { int c = va_arg(args, int);
                if (left) { emit((char)c, ctx); for (int i = 1; i < pad; i++) emit(' ', ctx); }
                else { for (int i = 1; i < pad; i++) emit(padchar, ctx); emit((char)c, ctx); }
                break; }
            case '%': emit('%', ctx); break;
            default: emit('%', ctx); emit(*fmt, ctx); break;
        }
        fmt++;
    }
}

int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    format_core(emit_stdout, NULL, fmt, args);
    va_end(args);
    return 0;
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    fmt_bufsink s = { buf, size, 0 };
    format_core(emit_buf, &s, fmt, ap);
    if (size) buf[(s.count < size) ? s.count : size - 1] = '\0';
    return (int)s.count;   /* C99: the length that WOULD have been written */
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);   /* unbounded, like before */
    va_end(ap);
    return r;
}

/* fprintf family — format onto a FILE* (v6.1.4) via the shared core + fputc. */
int vfprintf(FILE* f, const char* fmt, va_list ap) {
    fmt_filesink s = { f, 0 };
    format_core(emit_file, &s, fmt, ap);
    return s.count;
}

int fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

/* =========== ctype — character classification/conversion (ASCII) ===========
 * The primitives a compiler's lexer leans on. Pure, no allocation. */
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isprint(c) && c != ' ' && !isalnum(c); }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* =========== string -> integer =========== */
/* Skips leading whitespace and an optional +/- sign; base 0 auto-detects a 0x/0X
 * prefix (hex) or a leading 0 (octal), else decimal. On return *endptr (if non-NULL)
 * points just past the last digit consumed, or at nptr if none were. Wraps on
 * overflow (no errno). A leading '-' negates in the unsigned return type, per C. */
unsigned long strtoul(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') { base = 8; }
    else if (base == 0) { base = 10; }
    unsigned long acc = 0; int any = 0;
    for (;;) {
        int c = (unsigned char)*s, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        acc = acc * (unsigned long)base + (unsigned long)d;
        any = 1; s++;
    }
    if (endptr) *endptr = (char*)(any ? s : nptr);
    return neg ? (unsigned long)(-(long)acc) : acc;
}

long strtol(const char* nptr, char** endptr, int base) {
    return (long)strtoul(nptr, endptr, base);   /* strtoul already applied the sign */
}

/* =========== string / stdlib extras (compiler staples) =========== */

/* Find the first byte equal to c in the first n bytes of s (NUL is not special). */
void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    unsigned char ch = (unsigned char)c;
    for (size_t i = 0; i < n; i++) if (p[i] == ch) return (void*)(p + i);
    return 0;
}

/* Find the LAST occurrence of c in s (c==0 matches the terminating NUL). */
char* strrchr(const char* s, int c) {
    const char* last = 0;
    char ch = (char)c;
    for (;;) { if (*s == ch) last = s; if (!*s) break; s++; }
    return (char*)last;
}

/* Append up to n bytes of src onto dest, always NUL-terminating. */
char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    size_t i = 0;
    while (i < n && src[i]) { d[i] = src[i]; i++; }
    d[i] = '\0';
    return dest;
}

/* Return a malloc'd copy of s (caller frees), or NULL if out of memory. */
char* strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void qsort_swap(char* a, char* b, size_t size) {
    for (size_t i = 0; i < size; i++) { char t = a[i]; a[i] = b[i]; b[i] = t; }
}

/* Sort nmemb elements of `size` bytes in place using `cmp`. Shell sort (Knuth
 * gaps): no recursion and no scratch memory, and far better than plain insertion
 * sort on the larger arrays a compiler will hand it (symbol/reloc tables). */
void qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*)) {
    char* arr = (char*)base;
    if (nmemb < 2 || size == 0) return;
    size_t gap = 1;
    while (gap < nmemb / 3) gap = gap * 3 + 1;
    for (; gap >= 1; gap /= 3) {
        for (size_t i = gap; i < nmemb; i++) {
            for (size_t j = i; j >= gap && cmp(arr + (j - gap) * size, arr + j * size) > 0; j -= gap) {
                qsort_swap(arr + (j - gap) * size, arr + j * size, size);
            }
        }
        if (gap == 1) break;   /* size_t: avoid 1/3 == 0 looping forever */
    }
}

/* Binary search over a sorted array (the natural companion to qsort). Returns a
 * pointer to a matching element, or NULL. `cmp(key, elem)` orders like qsort's. */
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*)) {
    const char* b = (const char*)base;
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char* p = b + mid * size;
        int r = cmp(key, p);
        if (r < 0)      hi = mid;
        else if (r > 0) lo = mid + 1;
        else            return (void*)p;
    }
    return 0;
}

/* =========== stdio (FILE*) — buffered file I/O over the fd syscalls ===========
 * A minimal but correct FILE core: one 1 KB buffer holds either read-ahead OR
 * pending writes at a time (tracked by `mode`), refilled/flushed as needed. On an
 * r+/w+/a+ stream a read->write switch rewinds the fd to the logical read position
 * and a write->read switch flushes first, so the file offset stays consistent.
 * (fprintf family: v6.1.4; stdin/out/err + fgets/fputs/fseek/ftell: v6.1.5.) */

FILE* fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    int flags, cr = 0, cw = 0;
    switch (mode[0]) {
        case 'r': flags = O_RDONLY;           cr = 1; break;
        case 'w': flags = O_CREAT | O_TRUNC;  cw = 1; break;
        case 'a': flags = O_CREAT | O_APPEND; cw = 1; break;
        default:  return NULL;
    }
    for (const char* p = mode + 1; *p; p++) if (*p == '+') { cr = 1; cw = 1; }
    int fd = (int)open(path, flags, 0644);
    if (fd < 0) return NULL;
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd = fd; f->pos = 0; f->len = 0; f->mode = 0;
    f->can_read = cr; f->can_write = cw; f->eof = 0; f->err = 0;
    f->flush_each = 0;   /* fopen'd streams are fully buffered (malloc: not zeroed) */
    return f;
}

int fflush(FILE* f) {
    if (!f) return 0;
    if (f->mode == 'w' && f->len > 0) {
        long n = write(f->fd, f->buf, f->len);
        if (n < f->len) { f->err = 1; f->len = 0; return EOF; }
        f->len = 0;
    }
    return 0;
}

int fgetc(FILE* f) {
    if (!f || !f->can_read) { if (f) f->err = 1; return EOF; }
    if (f->mode == 'w') { if (fflush(f) == EOF) return EOF; f->mode = 0; }
    if (f->mode != 'r' || f->pos >= f->len) {
        long n = read(f->fd, f->buf, (long)sizeof(f->buf));
        if (n <= 0) { if (n == 0) f->eof = 1; else f->err = 1; return EOF; }
        f->len = (int)n; f->pos = 0; f->mode = 'r';
    }
    return f->buf[f->pos++];
}

int fputc(int c, FILE* f) {
    if (!f || !f->can_write) { if (f) f->err = 1; return EOF; }
    if (f->mode == 'r') {                       /* read->write: rewind the fd to the */
        if (f->len > f->pos)                    /* logical position (prefetched but   */
            lseek(f->fd, -(long)(f->len - f->pos), SEEK_CUR);  /* unconsumed bytes)   */
        f->len = 0; f->pos = 0; f->mode = 0;
    }
    if (f->mode != 'w') { f->mode = 'w'; f->len = 0; }
    f->buf[f->len++] = (unsigned char)c;
    if (f->flush_each || f->len == (int)sizeof(f->buf)) { if (fflush(f) == EOF) return EOF; }
    return (unsigned char)c;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (size == 0 || nmemb == 0) return 0;
    unsigned char* p = (unsigned char*)ptr;
    size_t total = size * nmemb, i;
    for (i = 0; i < total; i++) { int c = fgetc(f); if (c < 0) break; p[i] = (unsigned char)c; }
    return i / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    if (size == 0 || nmemb == 0) return 0;
    const unsigned char* p = (const unsigned char*)ptr;
    size_t total = size * nmemb, i;
    for (i = 0; i < total; i++) { if (fputc(p[i], f) < 0) break; }
    return i / size;
}

int fclose(FILE* f) {
    if (!f) return EOF;
    int rc = fflush(f);
    long c = close(f->fd);
    free(f);
    return (rc == EOF || c < 0) ? EOF : 0;
}

int feof(FILE* f)   { return f ? f->eof : 0; }
int ferror(FILE* f) { return f ? f->err : 0; }

/* ---- Standard streams + fgets/fputs/fseek/ftell (v6.1.5) ----
 * The three predefined streams wrap fds 0/1/2 (opened by the kernel for every
 * process). stdout/stderr are unbuffered (flush_each), so fprintf(stdout,...) and
 * fputc(c,stdout) reach the terminal immediately and interleave with printf's
 * direct write(1,...). Static storage zero-inits every unnamed field (pos/len/
 * mode/eof/err = 0, and stdin's can_write / stdout's can_read = 0). */
static FILE _stdin  = { .fd = 0, .can_read = 1 };
static FILE _stdout = { .fd = 1, .can_write = 1, .flush_each = 1 };
static FILE _stderr = { .fd = 2, .can_write = 1, .flush_each = 1 };
FILE* stdin  = &_stdin;
FILE* stdout = &_stdout;
FILE* stderr = &_stderr;

/* Read at most size-1 bytes into s, stopping after a newline (kept) or at EOF, and
 * NUL-terminate. Returns s, or NULL if EOF/error hits before any byte is read. */
char* fgets(char* s, int size, FILE* f) {
    if (!s || size <= 0 || !f) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(f);
        if (c == EOF) { if (i == 0) return NULL; break; }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

/* Write the string (no NUL, no trailing newline). Non-negative on success, EOF on error. */
int fputs(const char* s, FILE* f) {
    if (!s || !f) return EOF;
    for (const char* p = s; *p; p++)
        if (fputc((unsigned char)*p, f) == EOF) return EOF;
    return 0;
}

/* Reposition the stream. Sync the fd to the logical position first (flush a pending
 * write buffer; rewind the fd past any unconsumed read-ahead so SEEK_CUR is exact),
 * then discard the buffer and lseek. Clears EOF. Returns 0, or -1 on error. */
int fseek(FILE* f, long offset, int whence) {
    if (!f) return -1;
    if (f->mode == 'w') { if (fflush(f) == EOF) return -1; }
    else if (f->mode == 'r' && f->len > f->pos)
        lseek(f->fd, -(long)(f->len - f->pos), SEEK_CUR);
    f->mode = 0; f->pos = 0; f->len = 0; f->eof = 0;
    if (lseek(f->fd, offset, whence) < 0) { f->err = 1; return -1; }
    return 0;
}

/* Current logical position: the fd offset adjusted for buffered bytes — pending
 * writes sit past it (+len), unconsumed read-ahead before it (-(len-pos)). */
long ftell(FILE* f) {
    if (!f) return -1;
    long off = lseek(f->fd, 0, SEEK_CUR);
    if (off < 0) { f->err = 1; return -1; }
    if (f->mode == 'w') off += f->len;
    else if (f->mode == 'r') off -= (f->len - f->pos);
    return off;
}

/* abort: abnormal termination. NyxOS does not deliver SIGABRT to self here, so this
 * just exits non-zero. It also supplies the `abort` symbol that TinyCC's va_list
 * runtime (user/tcc/lib/va_list.c, shipped as va_list.o) references in its unreachable
 * default case, so tcc-compiled varargs code links against our libc. */
void abort(void) {
    exit(1);
    for (;;) { }   /* exit() never returns; keep abort provably non-returning */
}

/* ===== float math (v6.4.369) — NyxOS's first userland math library.
 * Userland is SSE-OK (the kernel is -mno-sse, but #40/FPU ctx-save is fixed so a
 * preempted user task keeps its XMM state). sqrtf/fabsf lower to single SSE ops;
 * sin/cos/atan are range-reduced then minimax/Taylor. Accuracy is ~1e-4 — plenty for
 * graphics/games (the motivating consumer is the SM64 port; any float app benefits). */
float fabsf(float x) { return __builtin_fabsf(x); }
float sqrtf(float x) { return x <= 0.0f ? 0.0f : __builtin_sqrtf(x); }

float floorf(float x) { float t = (float)(long)x; return (t > x) ? t - 1.0f : t; }
float ceilf(float x)  { float t = (float)(long)x; return (t < x) ? t + 1.0f : t; }
float fmodf(float x, float y) { return (y == 0.0f) ? 0.0f : x - (float)(long)(x / y) * y; }

/* reduce x into [-pi, pi] */
static float nyx_reduce_pi(float x) {
    const float TWO_PI = 6.28318530718f, PI = 3.14159265359f;
    float k = x * (1.0f / TWO_PI);
    k = (k >= 0.0f) ? (float)(long)(k + 0.5f) : -(float)(long)(-k + 0.5f);
    x -= k * TWO_PI;
    if (x > PI) x -= TWO_PI; else if (x < -PI) x += TWO_PI;
    return x;
}
float sinf(float x) {
    const float PI = 3.14159265359f, HALF_PI = 1.57079632679f;
    x = nyx_reduce_pi(x);
    if (x > HALF_PI) x = PI - x; else if (x < -HALF_PI) x = -PI - x;   /* fold to [-pi/2, pi/2] */
    float x2 = x * x;   /* Taylor to x^9: < 2e-6 on [-pi/2, pi/2] */
    return x * (1.0f + x2 * (-1.0f/6.0f + x2 * (1.0f/120.0f + x2 * (-1.0f/5040.0f + x2 * (1.0f/362880.0f)))));
}
float cosf(float x) { return sinf(x + 1.57079632679f); }
void  sincosf(float x, float* s, float* c) { if (s) *s = sinf(x); if (c) *c = cosf(x); }
float tanf(float x) { float c = cosf(x); return (c == 0.0f) ? 0.0f : sinf(x) / c; }

/* atanf: minimax on |x|<=1; fold |x|>1 via atan(x) = ±pi/2 - atan(1/x). ~1e-5. */
static float nyx_atan_unit(float x) {
    float x2 = x * x;
    return x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f + x2 * (0.05265332f + x2 * (-0.01172120f))))));
}
float atanf(float x) {
    const float HALF_PI = 1.57079632679f;
    if (x >  1.0f) return  HALF_PI - nyx_atan_unit(1.0f / x);
    if (x < -1.0f) return -HALF_PI - nyx_atan_unit(1.0f / x);
    return nyx_atan_unit(x);
}
float atan2f(float y, float x) {
    const float PI = 3.14159265359f, HALF_PI = 1.57079632679f;
    if (x > 0.0f) return atanf(y / x);
    if (x < 0.0f) return (y >= 0.0f) ? atanf(y / x) + PI : atanf(y / x) - PI;
    if (y > 0.0f) return  HALF_PI;
    if (y < 0.0f) return -HALF_PI;
    return 0.0f;
}
