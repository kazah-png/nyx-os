#ifndef FAKE_STDLIB_H
#define FAKE_STDLIB_H

#ifndef size_t
typedef unsigned long size_t;
#endif
#ifndef ssize_t
typedef long ssize_t;
#endif
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
#ifndef intptr_t
typedef long intptr_t;
#endif
#ifndef uintptr_t
typedef unsigned long uintptr_t;
#endif
typedef int wchar_t;

#define NULL ((void*)0)

// Límites estándar
#define INT_MAX 2147483647
#define INT_MIN (-2147483648)
#define SHRT_MAX 32767
#define SHRT_MIN (-32768)
#define CHAR_MAX 127
#define CHAR_MIN (-128)
#define SCHAR_MAX 127
#define SCHAR_MIN (-128)
#define UCHAR_MAX 255
#define USHRT_MAX 65535
#define UINT_MAX 4294967295U

// stdarg
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)

// FILE
typedef struct { int dummy; } FILE;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
extern FILE *stderr;
extern FILE *stdout;
extern FILE *stdin;

// errno
extern int errno;
#define EISDIR 21
#define ENOENT 2
#define EACCES 13
#define EIO 5
#define ENOMEM 12

// string.h
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strlen(const char *s);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

// stdio.h
int sscanf(const char *str, const char *format, ...);

// ctype
#define isalpha(c) 1
#define isdigit(c) 1
#define isalnum(c) 1
#define isspace(c) 1
#define isupper(c) 1
#define islower(c) 1
#define toupper(c) (c)
#define tolower(c) (c)

// Declaraciones de funciones (implementadas en doom_utils.c o en kernel)
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void abort(void);
int abs(int j);
long int labs(long int j);
int atoi(const char *s);
double atof(const char *s);
long int strtol(const char *s, char **end, int base);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strdup(const char *s);
void exit(int status);

double sin(double x);
double cos(double x);
double sqrt(double x);
double fabs(double x);
double pow(double x, double y);
double floor(double x);
double ceil(double x);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list args);
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list args);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list args);
int fflush(FILE *stream);
int putchar(int c);
int puts(const char *s);
int remove(const char *pathname);
int system(const char *command);
int rename(const char *oldpath, const char *newpath);

#endif