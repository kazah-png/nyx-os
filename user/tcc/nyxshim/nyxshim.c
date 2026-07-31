/* NyxOS adapter layer for the vendored TinyCC 0.9.27 port (Phase 2).
 *
 * tcc's sources expect POSIX-signature OS functions (variadic open(), time_t
 * time(), ssize_t read/write, ...). Our syscall.h provides them as static-inline
 * wrappers with NyxOS-specific signatures, which clash. So this TU deliberately
 * does NOT include syscall.h: it replicates the raw x86_64 syscall primitive and
 * the few SYS_* numbers it needs, and defines the OS/libc symbols tcc references
 * with clean POSIX signatures. The C-library proper (malloc, printf, mem*, str*)
 * comes from the NyxOS libc.so at link time; here we add only what libc lacks.
 *
 * M0 goal = link tcc.elf + `tcc --version`; the float parsers (strtod/strtof/
 * strtold), ldexp and sscanf are stubs to be implemented in later increments. */

#define NYX_LIBC_NO_SYSCALL 1
#include "libc.h"   /* FILE, malloc, memset, size_t, NULL — declarations only */

/* --- raw x86_64 syscall primitive (ABI copied verbatim from user/syscall.h) --- */
#define SYS_EXIT          0
#define SYS_WRITE         1
#define SYS_OPEN          3
#define SYS_READ          4
#define SYS_CLOSE         5
#define SYS_GETCWD        22
#define SYS_UNLINK        24
#define SYS_MPROTECT      26
#define SYS_LSEEK         46
#define SYS_GETTIMEOFDAY  52

static long nyx_sc(long no, long a1, long a2, long a3) {
    long ret;
    register long r10 asm("r10") = 0;
    register long r8  asm("r8")  = 0;
    register long r9  asm("r9")  = 0;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(no), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

/* errno: tcc reads it after failed calls. We do not populate it from the raw
 * syscalls yet (they return negative); this just satisfies the reference. */
int errno;

/* --- process --- */
void exit(int status)  { nyx_sc(SYS_EXIT, status, 0, 0); for (;;) {} }
void _exit(int status) { nyx_sc(SYS_EXIT, status, 0, 0); for (;;) {} }
void abort(void)       { nyx_sc(SYS_EXIT, 134, 0, 0);    for (;;) {} }  /* 128+SIGABRT */

/* --- file I/O (POSIX signatures over the raw syscalls) --- */
long read(int fd, void* buf, unsigned long n)        { return nyx_sc(SYS_READ,  fd, (long)buf, (long)n); }
long write(int fd, const void* buf, unsigned long n) { return nyx_sc(SYS_WRITE, fd, (long)buf, (long)n); }
int  close(int fd)                                   { return (int)nyx_sc(SYS_CLOSE, fd, 0, 0); }
int  unlink(const char* p)                           { return (int)nyx_sc(SYS_UNLINK, (long)p, 0, 0); }
int  remove(const char* p)                           { return (int)nyx_sc(SYS_UNLINK, (long)p, 0, 0); }
long lseek(int fd, long off, int whence)             { return nyx_sc(SYS_LSEEK, fd, off, whence); }

char* getcwd(char* buf, unsigned long sz) {
    long r = nyx_sc(SYS_GETCWD, (long)buf, (long)sz, 0);
    return (r < 0) ? (char*)0 : buf;
}

/* open(path, flags[, mode]) — variadic like POSIX; mode is only read with O_CREAT. */
int open(const char* path, int flags, ...) {
    int mode = 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, flags);
    mode = __builtin_va_arg(ap, int);
    __builtin_va_end(ap);
    return (int)nyx_sc(SYS_OPEN, (long)path, flags, mode);
}

/* access(): existence/permission probe. We only implement F_OK-style existence by
 * trying to open O_RDONLY (flags 0). Good enough for tcc's "does this file exist". */
int access(const char* path, int mode) {
    (void)mode;
    int fd = (int)nyx_sc(SYS_OPEN, (long)path, 0, 0);
    if (fd < 0) return -1;
    nyx_sc(SYS_CLOSE, fd, 0, 0);
    return 0;
}

/* --- time (POSIX shapes; struct layouts must match the shim headers) --- */
struct nyx_timeval_ { long tv_sec; long tv_usec; };
int gettimeofday(void* tv, void* tz) { (void)tz; return (int)nyx_sc(SYS_GETTIMEOFDAY, (long)tv, 0, 0); }

long time(long* t) {
    struct nyx_timeval_ tv = { 0, 0 };
    nyx_sc(SYS_GETTIMEOFDAY, (long)&tv, 0, 0);
    if (t) *t = tv.tv_sec;
    return tv.tv_sec;
}

struct nyx_tm_ { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
void* localtime(const long* t) { (void)t; static struct nyx_tm_ z; return &z; }  /* M0 stub */

/* --- stdlib float parsing + math (v6.4.6) ------------------------------------
 * tcc's lexer (tccpp.c parse_number) turns EVERY floating-point literal into a
 * value by calling strtof/strtod/strtold; the M0 stubs returned 0.0, so the in-OS
 * tcc compiled every float/double constant to ZERO -- both in user code and, worse,
 * inside libtcc1.c (its `18446744073709551616.0` = 2^64 unsigned<->float bias), so
 * float-using programs built in-OS computed garbage. Real strtod fixes that. */
static int nyx_isspace_(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v'; }
static int nyx_hexval_(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
double strtod(const char* s, char** end) {
    const char* p = s;
    while (nyx_isspace_(*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }

    /* Hexadecimal float: 0x<hex>[.<hex>][p<dec exp>], mantissa * 2^exp. */
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (nyx_hexval_(p[2]) >= 0 || p[2] == '.')) {
        p += 2;
        double m = 0.0;
        while (nyx_hexval_(*p) >= 0) { m = m * 16.0 + nyx_hexval_(*p); p++; }
        if (*p == '.') { p++; double f = 1.0 / 16.0;
            while (nyx_hexval_(*p) >= 0) { m += nyx_hexval_(*p) * f; f /= 16.0; p++; } }
        int e = 0, es = 1;
        if (*p == 'p' || *p == 'P') { p++;
            if (*p == '+' || *p == '-') { es = (*p == '-') ? -1 : 1; p++; }
            while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; } e *= es; }
        double r = m;
        while (e > 0) { r *= 2.0; e--; }
        while (e < 0) { r *= 0.5; e++; }
        if (end) *end = (char*)p;
        return neg ? -r : r;
    }

    /* Decimal: accumulate up to 19 significant digits EXACTLY into a u64 (keeps big
     * powers of ten like 2^64 accurate), track the base-10 exponent, then scale. */
    unsigned long long mant = 0;
    int nd = 0, exp10 = 0, seen = 0;
    while (*p >= '0' && *p <= '9') { seen = 1;
        if (nd < 19) { mant = mant * 10ULL + (unsigned)(*p - '0'); nd++; } else exp10++; p++; }
    if (*p == '.') { p++;
        while (*p >= '0' && *p <= '9') { seen = 1;
            if (nd < 19) { mant = mant * 10ULL + (unsigned)(*p - '0'); nd++; exp10--; } p++; } }
    if (!seen) { if (end) *end = (char*)s; return 0.0; }
    int e = 0, es = 1;
    if (*p == 'e' || *p == 'E') { p++;
        if (*p == '+' || *p == '-') { es = (*p == '-') ? -1 : 1; p++; }
        while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; } e *= es; }
    exp10 += e;

    double r = (double)mant;
    int ax = exp10 < 0 ? -exp10 : exp10;
    double base = 10.0, sc = 1.0;
    while (ax) { if (ax & 1) sc *= base; base *= base; ax >>= 1; }   /* sc = 10^|exp10| */
    r = (exp10 < 0) ? r / sc : r * sc;
    if (end) *end = (char*)p;
    return neg ? -r : r;
}
float       strtof(const char* n, char** e)  { return (float)strtod(n, e); }
long double strtold(const char* n, char** e) { return (long double)strtod(n, e); }
double      ldexp(double x, int e) {            /* x * 2^e */
    while (e > 0) { x *= 2.0; e--; }
    while (e < 0) { x *= 0.5; e++; }
    return x;
}

/* sscanf: M0 stub (tcc uses it once). Returns 0 = "no fields matched". */
int sscanf(const char* s, const char* fmt, ...) { (void)s; (void)fmt; return 0; }

/* 64-bit string->int: LP64 makes long 64-bit, so defer to the libc strtoul/strtol. */
unsigned long long strtoull(const char* n, char** e, int b) { return (unsigned long long)strtoul(n, e, b); }
long long          strtoll(const char* n, char** e, int b) { return (long long)strtol(n, e, b); }

/* execvp: only reached by tcc's cross-compiler multiplexer tool, unused in M0. */
int execvp(const char* file, char* const argv[]) { (void)file; (void)argv; return -1; }

/* --- symbols referenced only by tcc's -run/JIT + assert paths (not hit by
 * `tcc --version` / `-c`). mprotect is real; the signal calls are stubs (there is
 * no in-OS JIT execution yet); __assert_fail prints and aborts. --- */
int mprotect(void* addr, unsigned long len, int prot) {
    return (int)nyx_sc(SYS_MPROTECT, (long)addr, (long)len, prot);
}
int sigaction(int sig, const void* act, void* old) { (void)sig; (void)act; (void)old; return 0; }
int sigemptyset(void* set) { (void)set; return 0; }

void __assert_fail(const char* expr, const char* file, unsigned int line, const char* func) {
    (void)file; (void)line; (void)func;
    write(2, "tcc: assertion failed: ", 23);
    if (expr) write(2, expr, (unsigned long)strlen(expr));
    write(2, "\n", 1);
    nyx_sc(SYS_EXIT, 134, 0, 0);
    for (;;) {}
}

char* strerror(int e) { (void)e; return "error"; }

/* fdopen: wrap an existing fd in a FILE using the NyxOS libc FILE layout. */
FILE* fdopen(int fd, const char* mode) {
    FILE* f = (FILE*)malloc(sizeof(FILE));
    if (!f) return (FILE*)0;
    memset(f, 0, sizeof(FILE));
    f->fd = fd;
    f->can_read  = (mode && (mode[0] == 'r' || (mode[0] && mode[1] == '+'))) ? 1 : 0;
    f->can_write = (mode && (mode[0] == 'w' || mode[0] == 'a' || (mode[0] && mode[1] == '+'))) ? 1 : 0;
    return f;
}
