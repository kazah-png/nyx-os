#ifndef _NYXOS_LIBC_H
#define _NYXOS_LIBC_H

#include "syscall.h"

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)

typedef unsigned long size_t;

void* malloc(size_t size);
void free(void* ptr);
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

void putchar(int c);
int puts(const char* s);
int printf(const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, size_t size, const char* fmt, ...);

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

/* String / stdlib extras a compiler leans on. */
void* memchr(const void* s, int c, size_t n);
char* strrchr(const char* s, int c);
char* strncat(char* dest, const char* src, size_t n);
char* strdup(const char* s);
void  qsort(void* base, size_t nmemb, size_t size, int (*cmp)(const void*, const void*));

/* Process environment (set by crt0 from execve's envp). getenv reads it. */
extern char** environ;
char* getenv(const char* name);

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

#endif
