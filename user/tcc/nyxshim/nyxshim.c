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

/* --- stdlib float parsing + math: M0 stubs (return 0), to be implemented later --- */
double      strtod(const char* n, char** e)  { if (e) *e = (char*)n; return 0.0; }
float       strtof(const char* n, char** e)  { if (e) *e = (char*)n; return 0.0f; }
long double strtold(const char* n, char** e) { if (e) *e = (char*)n; return 0.0L; }
double      ldexp(double x, int e)           { (void)e; return x; }

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
