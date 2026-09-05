#ifndef _NYXOS_LIBC_H
#define _NYXOS_LIBC_H

/* Normally libc.h pulls in the raw syscall wrappers. A consumer that provides its
 * own OS layer with different (e.g. strict-POSIX) signatures — the vendored TinyCC
 * port, whose open()/time() prototypes clash with syscall.h — can define
 * NYX_LIBC_NO_SYSCALL to take just the C-library declarations below. */
#ifndef NYX_LIBC_NO_SYSCALL
#include "syscall.h"
#endif

/* va_list: GCC provides __builtin_va_list; TinyCC (which defines __TINYC__ and has no
 * such builtin) supplies it via its own <stdarg.h> — shipped in the initramfs at
 * /usr/lib/tcc/include so tcc can compile this libc IN-OS (self-hosting). The GCC path
 * below is unchanged, so the normal build is byte-identical. */
#ifdef __TINYC__
#include <stdarg.h>
#else
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#endif

typedef unsigned long size_t;

void* malloc(size_t size);
void free(void* ptr);
void abort(void);
void* realloc(void* ptr, size_t size);
void* calloc(size_t nmemb, size_t size);
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcat(char* dest, const char* src);
char* strchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
/* String scanning / tokenization (a parser's staples; strtok is what most ports reach for). */
size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char* strpbrk(const char* s, const char* accept);
char* strtok(char* str, const char* delim);
char* strtok_r(char* str, const char* delim, char** saveptr);
/* Case-insensitive (ASCII) compares + substring search (grep -i, header matching, ports). */
int strcasecmp(const char* a, const char* b);
int strncasecmp(const char* a, const char* b, size_t n);
char* strcasestr(const char* haystack, const char* needle);

void putchar(int c);
int puts(const char* s);
int printf(const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
/* sscanf/vsscanf: %d %i %u %o %x %X %c %s %f %e %g %n %% + width, '*' suppression
 * and h/hh/l/ll/z/j length modifiers. Scansets (%[...]) and %p are not supported
 * (the scan stops at such a directive). Returns the assignment count (or EOF if
 * input ends before the first conversion), matching C sscanf. */
int sscanf(const char* str, const char* fmt, ...);
int vsscanf(const char* str, const char* fmt, va_list ap);

int atoi(const char* s);
int abs(int x);

/* ctype — character classification/conversion (ASCII); a lexer's staples. */
int isspace(int c);
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int iscntrl(int c);
int isprint(int c);
int isgraph(int c);
int ispunct(int c);
int toupper(int c);
int tolower(int c);

/* String -> integer. base 0 auto-detects a 0x (hex) or 0 (octal) prefix. */
long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);

/* String -> double. Decimal and 0x hex floats; correctly rounded for the common
 * range (<=15 significant digits, |exp| <= 22), approximate for extreme magnitudes. */
double strtod(const char* nptr, char** endptr);
double atof(const char* nptr);

/* String / stdlib extras a compiler leans on. */
void* memchr(const void* s, int c, size_t n);
char* strrchr(const char* s, int c);
char* strncat(char* dest, const char* src, size_t n);
char* strdup(const char* s);
void  qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*cmp)(const void*, const void*));

/* ===== stdio (FILE*) — buffered file I/O over the fd syscalls =====
 * Core layer (v6.1.3): fopen/fclose/fread/fwrite/fgetc/fputc/fflush/feof/ferror;
 * fprintf family (v6.1.4); stdin/stdout/stderr + fgets/fputs/fseek/ftell (v6.1.5).
 * FILE is a complete type here; callers use it via FILE*. */
#ifndef NULL
#define NULL ((void*)0)
#endif
#define EOF (-1)

typedef struct FILE {
    int fd;
    unsigned char buf[1024];
    int pos;          /* next index into buf */
    int len;          /* valid bytes currently in buf */
    int mode;         /* 0 idle, 'r' read-buffered, 'w' write-buffered */
    int can_read, can_write;
    int eof, err;
    int flush_each;   /* 1 = unbuffered: flush after each fputc (stdout/stderr) */
} FILE;

FILE*  fopen(const char* path, const char* mode);
int    fclose(FILE* f);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f);
int    fgetc(FILE* f);
int    fputc(int c, FILE* f);
int    fflush(FILE* f);
int    feof(FILE* f);
int    ferror(FILE* f);
int    fprintf(FILE* f, const char* fmt, ...);
int    vfprintf(FILE* f, const char* fmt, va_list ap);

/* Standard streams (v6.1.5): predefined FILE* over fds 0/1/2. stdout/stderr are
 * unbuffered so their bytes reach the terminal immediately, matching printf. */
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

char*  fgets(char* s, int size, FILE* f);
int    fputs(const char* s, FILE* f);
int    fseek(FILE* f, long offset, int whence);
long   ftell(FILE* f);

/* Process environment (set by crt0 from execve's envp). getenv reads it. */
extern char** environ;
char* getenv(const char* name);
/* Modify the environment (POSIX). setenv copies name+value; putenv stores the caller's
 * "NAME=VALUE" pointer as-is. The first modification moves environ to a heap array. */
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
int putenv(char* string);

/* Calendar time (C standard). `time_t` is Unix epoch seconds; gmtime/localtime break it
 * down (NyxOS keeps the RTC in UTC, so localtime == gmtime — no timezone), and strftime
 * formats a struct tm. Get the epoch from gettimeofday() (see syscall.h). */
typedef long time_t;
struct tm {
    int tm_sec, tm_min, tm_hour;   /* 0-60, 0-59, 0-23 */
    int tm_mday, tm_mon, tm_year;  /* 1-31, 0-11, years since 1900 */
    int tm_wday, tm_yday, tm_isdst;/* 0-6 (Sun=0), 0-365, 0 */
};
struct tm* gmtime(const time_t* t);
struct tm* gmtime_r(const time_t* t, struct tm* r);
struct tm* localtime(const time_t* t);
struct tm* localtime_r(const time_t* t, struct tm* r);
size_t strftime(char* s, size_t max, const char* fmt, const struct tm* tm);
time_t mktime(struct tm* tm);     /* broken-down local time -> epoch (== timegm: RTC is UTC) */
time_t timegm(struct tm* tm);     /* broken-down UTC time -> epoch; normalizes the struct */
double difftime(time_t end, time_t beginning);

/* Non-local jump (fault recovery from a signal handler). setjmp saves the caller's
 * callee-saved regs + RSP + RIP and returns 0; longjmp restores them and makes the
 * matching setjmp return `val` (or 1 if val==0). jmp_buf = {rbx,rbp,r12..r15,rsp,rip}. */
typedef unsigned long jmp_buf[8];
int  setjmp(jmp_buf buf);
void longjmp(jmp_buf buf, int val) __attribute__((noreturn));

/* Signal-mask-saving non-local jump — multi-fault recovery. sigsetjmp(buf, 1)
 * saves the caller's context AND (savesigs != 0) the current signal mask; a
 * later siglongjmp back RESTORES that mask, unblocking the handler's signal so
 * the SAME fault can be caught again. (A plain setjmp/longjmp out of a handler
 * leaves the signal blocked, so the next fault would kill the process.) */
typedef unsigned long sigjmp_buf[10];   /* [0..7] jmp_buf regs, [8] saved mask, [9] savesigs */
void __sigsetjmp_save(sigjmp_buf buf, int savesigs);   /* helper; use sigsetjmp() */
#define sigsetjmp(buf, savesigs) (__sigsetjmp_save((buf), (savesigs)), setjmp((unsigned long*)(buf)))
void siglongjmp(sigjmp_buf buf, int val) __attribute__((noreturn));

/* float math (v6.4.369): SSE-backed, ~1e-4 accuracy. Userland only (the kernel is
 * -mno-sse). NyxOS's first math library — sinf/cosf/tanf/sqrtf/atan2f/… */
float fabsf(float x);
float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fmodf(float x, float y);
float sinf(float x);
float cosf(float x);
void  sincosf(float x, float* s, float* c);
float tanf(float x);
float atanf(float x);
float atan2f(float y, float x);

#endif
