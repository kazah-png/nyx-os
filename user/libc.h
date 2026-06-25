#ifndef _NYXOS_LIBC_H
#define _NYXOS_LIBC_H

#include "syscall.h"

// va_list for variadic functions
typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, t)   __builtin_va_arg(v, t)

// Memory
void* malloc(unsigned int size);
void free(void* ptr);
void* memset(void* s, int c, unsigned int n);
void* memcpy(void* dest, const void* src, unsigned int n);
int memcmp(const void* s1, const void* s2, unsigned int n);

// String
unsigned int strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, unsigned int n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, unsigned int n);
char* strcat(char* dest, const char* src);
char* strchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);

// Stdio
void putchar(int c);
int puts(const char* s);
int printf(const char* fmt, ...);
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, unsigned int size, const char* fmt, ...);

// Stdlib
int atoi(const char* s);
int abs(int x);

#endif
