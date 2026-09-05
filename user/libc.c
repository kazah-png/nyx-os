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

/* atoi is (int)strtol(s, NULL, 10) (C11 7.22.1.2): skip every isspace byte, not
 * just ' ', and clamp on overflow. Delegating keeps atoi and strtol in step (the
 * old hand-rolled loop skipped only ' ' and wrapped past INT range). */
int atoi(const char* s) {
    return (int)strtol(s, (char**)0, 10);
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

/* setenv/unsetenv/putenv — modify the process environment. `environ` starts pointing at
 * the envp the kernel laid on the stack, so the first growth copies it into a heap array
 * we own (env_owned) and grows that thereafter. execve passes the current `environ` to a
 * child, so a variable set here is inherited. */
static int env_owned = 0;      /* environ now points at our heap array */
static int env_cap = 0;        /* entries the heap array holds before the NULL terminator */

static int env_len(void) { int n = 0; if (environ) while (environ[n]) n++; return n; }

/* Append `entry` ("NAME=VALUE"), adopting/growing the heap array as needed. */
static int env_append(char* entry) {
    int n = env_len();
    if (!env_owned) {
        env_cap = n + 8;
        char** v = (char**)malloc((env_cap + 1) * sizeof(char*));
        if (!v) return -1;
        for (int i = 0; i < n; i++) v[i] = environ[i];
        environ = v; env_owned = 1;
    } else if (n >= env_cap) {
        env_cap = env_cap * 2 + 8;
        char** v = (char**)malloc((env_cap + 1) * sizeof(char*));
        if (!v) return -1;
        for (int i = 0; i < n; i++) v[i] = environ[i];
        free(environ); environ = v;
    }
    environ[n] = entry;
    environ[n + 1] = 0;
    return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) return -1;
    if (!value) value = "";
    size_t nl = strlen(name), vl = strlen(value);
    int found = -1;
    if (environ) for (int i = 0; environ[i]; i++)
        if (strncmp(environ[i], name, nl) == 0 && environ[i][nl] == '=') { found = i; break; }
    if (found >= 0 && !overwrite) return 0;
    char* entry = (char*)malloc(nl + 1 + vl + 1);
    if (!entry) return -1;
    memcpy(entry, name, nl); entry[nl] = '=';
    memcpy(entry + nl + 1, value, vl); entry[nl + 1 + vl] = '\0';
    if (found >= 0) { environ[found] = entry; return 0; }   /* replace the slot (old entry leaks) */
    return env_append(entry);
}

int unsetenv(const char* name) {
    if (!name || !*name || strchr(name, '=')) return -1;
    if (!environ) return 0;
    size_t nl = strlen(name);
    int w = 0;
    for (int r = 0; environ[r]; r++) {
        if (strncmp(environ[r], name, nl) == 0 && environ[r][nl] == '=') continue;   /* drop matches */
        environ[w++] = environ[r];
    }
    environ[w] = 0;
    return 0;
}

int putenv(char* string) {
    char* eq = strchr(string, '=');
    if (!eq) return unsetenv(string);          /* no '=': remove that name */
    size_t nl = (size_t)(eq - string);
    if (environ) for (int i = 0; environ[i]; i++)
        if (strncmp(environ[i], string, nl) == 0 && environ[i][nl] == '=') { environ[i] = string; return 0; }
    return env_append(string);                 /* POSIX: stores the caller's pointer, no copy */
}

/* =========== Calendar time (gmtime / localtime / strftime) =========== */
/* NyxOS keeps the RTC in UTC, so localtime == gmtime (no timezone database). */

static const char* tm_wd[]  = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
static const char* tm_wda[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
static const char* tm_mo[]  = { "January","February","March","April","May","June","July",
                                "August","September","October","November","December" };
static const char* tm_moa[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };

struct tm* gmtime_r(const time_t* tp, struct tm* r) {
    long t = *tp;
    long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }               /* floor division for pre-epoch times */
    r->tm_hour = (int)(rem / 3600); rem %= 3600;
    r->tm_min = (int)(rem / 60); r->tm_sec = (int)(rem % 60);
    r->tm_wday = (int)(((days % 7) + 4 + 7) % 7);           /* 1970-01-01 was Thursday (4) */
    /* civil-from-days (Howard Hinnant): valid for the whole range of `long` */
    long z = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (unsigned)((doe - doe/1460 + doe/36524 - doe/146096) / 365);
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    unsigned mp = (5*doy + 2) / 153;
    unsigned d = doy - (153*mp + 2)/5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    r->tm_year = (int)(y - 1900);
    r->tm_mon = (int)m - 1;
    r->tm_mday = (int)d;
    static const int mdays[] = { 0,31,59,90,120,151,181,212,243,273,304,334 };
    int leap = (y%4==0 && (y%100!=0 || y%400==0));
    r->tm_yday = mdays[r->tm_mon] + (r->tm_mday - 1) + ((r->tm_mon > 1 && leap) ? 1 : 0);
    r->tm_isdst = 0;
    return r;
}
struct tm* gmtime(const time_t* t)         { static struct tm b; return gmtime_r(t, &b); }
struct tm* localtime_r(const time_t* t, struct tm* r) { return gmtime_r(t, r); }
struct tm* localtime(const time_t* t)      { return gmtime(t); }

/* days since 1970-01-01 for a proleptic-Gregorian y/m/d (Howard Hinnant); m in 1..12,
 * d may be any value (the caller absorbs the excess). Inverse of gmtime_r's civil-from-days. */
static long days_from_civil(long y, unsigned m, unsigned d) {
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                     /* [0,399] */
    unsigned doy = (153u*(m > 2 ? m - 3 : m + 9) + 2)/5 + d - 1;  /* [0,365] */
    unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;               /* [0,146096] */
    return era*146097 + (long)doe - 719468;
}

/* timegm/mktime: build a time_t from a broken-down time and canonicalize the struct.
 * NyxOS keeps the RTC in UTC and has no timezone database, so mktime == timegm. The month
 * is floored into 0..11 (carrying into the year); the linear second arithmetic then absorbs
 * any out-of-range sec/min/hour/mday, and gmtime_r re-derives wday/yday and the normalized
 * fields — matching glibc's timegm across overflow and pre-epoch inputs. */
time_t timegm(struct tm* tm) {
    long year = (long)tm->tm_year + 1900;
    long mon  = tm->tm_mon;
    long ym = mon / 12, mr = mon % 12;
    if (mr < 0) { mr += 12; ym -= 1; }                 /* floor, so a negative month borrows a year */
    mon = mr; year += ym;
    long days = days_from_civil(year, (unsigned)(mon + 1), 1) + (long)tm->tm_mday - 1;
    time_t t = (time_t)days*86400 + (long)tm->tm_hour*3600
             + (long)tm->tm_min*60 + (long)tm->tm_sec;
    gmtime_r(&t, tm);
    return t;
}
time_t mktime(struct tm* tm) { return timegm(tm); }

/* Seconds between two times, as a double (C standard). */
double difftime(time_t end, time_t beginning) { return (double)end - (double)beginning; }

static char* tmf_str(char* p, char* end, const char* s) { while (*s && p < end) *p++ = *s++; return p; }
static char* tmf_num(char* p, char* end, long v, int width, char pad) {
    char tmp[24]; int n = 0; long a = v < 0 ? -v : v;
    do { tmp[n++] = (char)('0' + (int)(a % 10)); a /= 10; } while (a);
    if (v < 0 && p < end) *p++ = '-';
    for (int i = n; i < width && p < end; i++) *p++ = pad;
    while (n > 0 && p < end) *p++ = tmp[--n];
    return p;
}

/* strftime subset: %Y %y %C %m %d %e %H %I %M %S %j %p %P %A %a %B %b %h %w %u
 * %F %T %R %D %n %t %%. Unknown specifiers are copied literally. Returns the byte
 * count (excl NUL), or 0 if it does not fit (C semantics). */
size_t strftime(char* s, size_t max, const char* fmt, const struct tm* tm) {
    if (max == 0) return 0;
    char* p = s; char* end = s + max - 1;
    int y = tm->tm_year + 1900;
    int h12 = tm->tm_hour % 12; if (h12 == 0) h12 = 12;
    char b[32];
    for (const char* f = fmt; *f; f++) {
        if (*f != '%') { if (p < end) *p++ = *f; else { *s = 0; return 0; } continue; }
        f++;
        switch (*f) {
            case 'Y': p = tmf_num(p, end, y, 0, '0'); break;
            case 'y': p = tmf_num(p, end, (y % 100 + 100) % 100, 2, '0'); break;
            case 'C': p = tmf_num(p, end, y / 100, 2, '0'); break;
            case 'm': p = tmf_num(p, end, tm->tm_mon + 1, 2, '0'); break;
            case 'd': p = tmf_num(p, end, tm->tm_mday, 2, '0'); break;
            case 'e': p = tmf_num(p, end, tm->tm_mday, 2, ' '); break;
            case 'H': p = tmf_num(p, end, tm->tm_hour, 2, '0'); break;
            case 'I': p = tmf_num(p, end, h12, 2, '0'); break;
            case 'M': p = tmf_num(p, end, tm->tm_min, 2, '0'); break;
            case 'S': p = tmf_num(p, end, tm->tm_sec, 2, '0'); break;
            case 'j': p = tmf_num(p, end, tm->tm_yday + 1, 3, '0'); break;
            case 'w': p = tmf_num(p, end, tm->tm_wday, 0, '0'); break;
            case 'u': p = tmf_num(p, end, tm->tm_wday == 0 ? 7 : tm->tm_wday, 0, '0'); break;
            case 'p': p = tmf_str(p, end, tm->tm_hour < 12 ? "AM" : "PM"); break;
            case 'P': p = tmf_str(p, end, tm->tm_hour < 12 ? "am" : "pm"); break;
            case 'A': p = tmf_str(p, end, tm_wd[tm->tm_wday]); break;
            case 'a': p = tmf_str(p, end, tm_wda[tm->tm_wday]); break;
            case 'B': p = tmf_str(p, end, tm_mo[tm->tm_mon]); break;
            case 'b': case 'h': p = tmf_str(p, end, tm_moa[tm->tm_mon]); break;
            case 'n': if (p < end) *p++ = '\n'; break;
            case 't': if (p < end) *p++ = '\t'; break;
            case '%': if (p < end) *p++ = '%'; break;
            case 'F': snprintf(b, sizeof b, "%04d-%02d-%02d", y, tm->tm_mon+1, tm->tm_mday); p = tmf_str(p, end, b); break;
            case 'T': snprintf(b, sizeof b, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec); p = tmf_str(p, end, b); break;
            case 'R': snprintf(b, sizeof b, "%02d:%02d", tm->tm_hour, tm->tm_min); p = tmf_str(p, end, b); break;
            case 'D': snprintf(b, sizeof b, "%02d/%02d/%02d", tm->tm_mon+1, tm->tm_mday, (y%100+100)%100); p = tmf_str(p, end, b); break;
            case '\0': if (p < end) *p++ = '%'; f--; break;   /* trailing '%' is literal */
            default: if (p < end) *p++ = '%'; if (p < end) *p++ = *f; break;   /* unknown: copy literally */
        }
    }
    if (p > end) { *s = 0; return 0; }
    *p = '\0';
    return (size_t)(p - s);
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
/* C-standard overflow clamping (no <limits.h> here): strtoul saturates at ULONG_MAX,
 * strtol at LONG_MAX / LONG_MIN. Both still consume every digit (endptr past them). */
#define LC_LONG_MAX  0x7FFFFFFFFFFFFFFFL
#define LC_LONG_MIN  (-LC_LONG_MAX - 1L)
#define LC_ULONG_MAX 0xFFFFFFFFFFFFFFFFUL

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') { base = 8; }
    else if (base == 0) { base = 10; }
    unsigned long cutoff = LC_ULONG_MAX / (unsigned long)base;
    int cutlim = (int)(LC_ULONG_MAX % (unsigned long)base);
    unsigned long acc = 0; int any = 0;
    for (;;) {
        int c = (unsigned char)*s, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && d > cutlim)) any = -1;   /* overflow -> saturate */
        else { acc = acc * (unsigned long)base + (unsigned long)d; any = 1; }
        s++;
    }
    if (endptr) *endptr = (char*)(any ? s : nptr);
    if (any < 0) return LC_ULONG_MAX;
    return neg ? (unsigned long)(-(long)acc) : acc;
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') { base = 8; }
    else if (base == 0) { base = 10; }
    unsigned long cutoff = neg ? -(unsigned long)LC_LONG_MIN : (unsigned long)LC_LONG_MAX;
    int cutlim = (int)(cutoff % (unsigned long)base);
    cutoff /= (unsigned long)base;
    unsigned long acc = 0; int any = 0;
    for (;;) {
        int c = (unsigned char)*s, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        if (any < 0 || acc > cutoff || (acc == cutoff && d > cutlim)) any = -1;   /* overflow -> clamp */
        else { acc = acc * (unsigned long)base + (unsigned long)d; any = 1; }
        s++;
    }
    if (endptr) *endptr = (char*)(any ? s : nptr);
    if (any < 0) return neg ? LC_LONG_MIN : LC_LONG_MAX;
    return neg ? -(long)acc : (long)acc;
}

/* =========== strtod / atof =========== */
/* 10^0..10^22 are each exactly representable as a double. */
static const double POW10[23] = {
    1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,1e10,1e11,1e12,
    1e13,1e14,1e15,1e16,1e17,1e18,1e19,1e20,1e21,1e22
};
static double strtod_scale(double r, int e) {   /* r * 10^e (slow path, any sign) */
    if (e >= 0) { while (e > 22) { r *= 1e22; e -= 22; } r *= POW10[e]; }
    else        { e = -e; while (e > 22) { r /= 1e22; e -= 22; } r /= POW10[e]; }
    return r;
}
static int strtod_hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* strtod: decimal and 0x hex floats, with sign/exponent/endptr. The common case
 * (<=15 significant digits and |exp10| <= 22) is CORRECTLY ROUNDED — one IEEE * or
 * / of exact operands, byte-identical to a reference strtod; extreme magnitudes
 * (|exp10| > 22) fall back to iterative scaling and may differ by a few ULP. */
double strtod(const char* s, char** end) {
    const char* p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (strtod_hex(p[2]) >= 0 || p[2] == '.')) {         /* hex float */
        p += 2;
        double m = 0.0;
        while (strtod_hex(*p) >= 0) { m = m * 16.0 + strtod_hex(*p); p++; }
        if (*p == '.') {
            p++;
            double f = 1.0 / 16.0;
            while (strtod_hex(*p) >= 0) { m += strtod_hex(*p) * f; f /= 16.0; p++; }
        }
        int e = 0, es = 1;
        if (*p == 'p' || *p == 'P') {
            p++;
            if (*p == '+' || *p == '-') { es = (*p == '-') ? -1 : 1; p++; }
            while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; }
            e *= es;
        }
        double r = m;
        while (e > 0) { r *= 2.0; e--; }
        while (e < 0) { r *= 0.5; e++; }
        if (end) *end = (char*)p;
        return neg ? -r : r;
    }

    unsigned long mant = 0;                               /* decimal */
    int sig = 0, exp10 = 0, seen = 0;
    while (*p >= '0' && *p <= '9') {
        seen = 1;
        if (sig < 19) { mant = mant * 10UL + (unsigned)(*p - '0'); sig++; } else exp10++;
        p++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            seen = 1;
            if (sig < 19) { mant = mant * 10UL + (unsigned)(*p - '0'); sig++; exp10--; }
            p++;
        }
    }
    if (!seen) { if (end) *end = (char*)s; return 0.0; }
    int e = 0, es = 1;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        if (*q == '+' || *q == '-') { es = (*q == '-') ? -1 : 1; q++; }
        if (*q >= '0' && *q <= '9') {                     /* only consume 'e' if a digit follows */
            p = q;
            while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; }
            e *= es;
        }
    }
    exp10 += e;

    double r;
    if (sig <= 15 && exp10 >= -22 && exp10 <= 22) {        /* fast path: correctly rounded */
        r = (double)mant;
        if (exp10 >= 0) r *= POW10[exp10];
        else            r /= POW10[-exp10];
    } else {
        r = strtod_scale((double)mant, exp10);             /* slow path: approximate */
    }
    if (end) *end = (char*)p;
    return neg ? -r : r;
}

double atof(const char* s) { return strtod(s, (char**)0); }

/* =========== sscanf / vsscanf =========== */
/* Copy a plausible numeric field — a prefix of s, capped by `width` (0 = the buffer)
 * — into `buf` for strtol/strtoul to parse. The character class is a superset of
 * every integer form (sign, decimal + hex digits, 0x prefix), so strtol's endptr,
 * not this copy, decides where the number really ends. */
static int scanf_num_field(const char* s, int width, char* buf, int bufsz) {
    int n = 0;
    int cap = (width > 0 && width < bufsz - 1) ? width : bufsz - 1;
    while (s[n] && n < cap) {
        char c = s[n];
        int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') ||
                 c == '.' || c == 'x' || c == 'X' || c == '+' || c == '-' ||
                 c == 'p' || c == 'P' || c == 'e' || c == 'E';
        if (!ok) break;
        buf[n] = c; n++;
    }
    buf[n] = '\0';
    return n;
}

/* Subset scanf: %d %i %u %o %x %X %c %s %f %e %g %n %% with field width, '*'
 * suppression and h/hh/l/ll/z/j length modifiers. Integers go through strtol/strtoul
 * (`long` is 64-bit here, so it also covers ll/z/j); floats through strtod (%lf ->
 * double, %f -> float). Scansets (%[...]) and %p stop the scan. Returns the count of
 * assigned conversions, or EOF (-1) if input ends before the first. */
int vsscanf(const char* str, const char* fmt, va_list ap) {
    const char* s = str;
    const char* f = fmt;
    int assigned = 0;
    for (; *f; ) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) f++;
            while (isspace((unsigned char)*s)) s++;
            continue;
        }
        if (*f != '%') {                       /* literal must match */
            if (*s == '\0') return assigned ? assigned : -1;
            if (*s != *f) return assigned;
            s++; f++; continue;
        }
        f++;                                   /* past '%' */
        if (*f == '%') {                        /* %% : skip ws, match '%' */
            while (isspace((unsigned char)*s)) s++;
            if (*s == '\0') return assigned ? assigned : -1;
            if (*s != '%') return assigned;
            s++; f++; continue;
        }
        int suppress = 0; if (*f == '*') { suppress = 1; f++; }
        int width = 0;    while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        int lm = 0;  /* 0 none, 1 h, 2 hh, 3 l, 4 ll, 5 z, 6 L */
        if (*f == 'h') { f++; if (*f == 'h') { lm = 2; f++; } else lm = 1; }
        else if (*f == 'l') { f++; if (*f == 'l') { lm = 4; f++; } else lm = 3; }
        else if (*f == 'z') { lm = 5; f++; }
        else if (*f == 'j' || *f == 't') { lm = 4; f++; }
        else if (*f == 'L') { lm = 6; f++; }
        char conv = *f; if (conv) f++;

        if (conv == 'c') {                      /* width chars (default 1), no ws skip */
            int cnt = width > 0 ? width : 1;
            char* dst = suppress ? 0 : va_arg(ap, char*);
            int got = 0;
            while (got < cnt && *s) { if (dst) dst[got] = *s; s++; got++; }
            if (got < cnt) return assigned ? assigned : -1;
            if (dst) assigned++;
            continue;
        }
        if (conv == 's') {                      /* non-ws run; skips leading ws */
            while (isspace((unsigned char)*s)) s++;
            if (*s == '\0') return assigned ? assigned : -1;
            char* dst = suppress ? 0 : va_arg(ap, char*);
            int got = 0, cap = width > 0 ? width : 0x7fffffff;
            while (*s && !isspace((unsigned char)*s) && got < cap) { if (dst) dst[got] = *s; s++; got++; }
            if (dst) dst[got] = '\0';
            if (!suppress) assigned++;
            continue;
        }
        if (conv == 'n') {                      /* chars consumed so far; not an assignment */
            if (!suppress) {
                int v = (int)(s - str);
                switch (lm) {
                    case 2: *va_arg(ap, signed char*) = (signed char)v; break;
                    case 1: *va_arg(ap, short*) = (short)v; break;
                    case 3: *va_arg(ap, long*) = v; break;
                    case 4: *va_arg(ap, long long*) = v; break;
                    case 5: *va_arg(ap, size_t*) = (size_t)v; break;
                    default: *va_arg(ap, int*) = v;
                }
            }
            continue;
        }
        /* numeric conversions skip leading ws */
        while (isspace((unsigned char)*s)) s++;
        if (*s == '\0') return assigned ? assigned : -1;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            char buf[130];
            if (scanf_num_field(s, width, buf, sizeof buf) == 0) return assigned;
            int base = (conv == 'd' || conv == 'u') ? 10 : (conv == 'o') ? 8 : (conv == 'x' || conv == 'X') ? 16 : 0;
            int isuns = (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X');
            char* ep = 0;
            unsigned long uv = 0; long sv = 0;
            if (isuns) uv = strtoul(buf, &ep, base); else sv = strtol(buf, &ep, base);
            if (ep == buf) return assigned;      /* matching failure */
            s += (ep - buf);
            if (!suppress) {
                if (isuns) { switch (lm) {
                    case 2: *va_arg(ap, unsigned char*) = (unsigned char)uv; break;
                    case 1: *va_arg(ap, unsigned short*) = (unsigned short)uv; break;
                    case 3: *va_arg(ap, unsigned long*) = (unsigned long)uv; break;
                    case 4: *va_arg(ap, unsigned long long*) = uv; break;
                    case 5: *va_arg(ap, size_t*) = (size_t)uv; break;
                    default: *va_arg(ap, unsigned*) = (unsigned)uv;
                } } else { switch (lm) {
                    case 2: *va_arg(ap, signed char*) = (signed char)sv; break;
                    case 1: *va_arg(ap, short*) = (short)sv; break;
                    case 3: *va_arg(ap, long*) = (long)sv; break;
                    case 4: *va_arg(ap, long long*) = sv; break;
                    case 5: *va_arg(ap, size_t*) = (size_t)sv; break;
                    default: *va_arg(ap, int*) = (int)sv;
                } }
                assigned++;
            }
            continue;
        }
        if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G' || conv == 'a' || conv == 'A') {
            char buf[130];
            if (scanf_num_field(s, width, buf, sizeof buf) == 0) return assigned;
            char* ep = 0; double dv = strtod(buf, &ep);
            if (ep == buf) return assigned;      /* matching failure */
            s += (ep - buf);
            if (!suppress) {
                if (lm == 3 || lm == 6) *va_arg(ap, double*) = dv; else *va_arg(ap, float*) = (float)dv;
                assigned++;
            }
            continue;
        }
        return assigned;                         /* unsupported conversion (scanset/%p) */
    }
    return assigned;
}

int sscanf(const char* str, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
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
