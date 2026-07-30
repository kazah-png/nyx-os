#ifndef NYX_TCC_STDIO_H
#define NYX_TCC_STDIO_H
#include "libc.h"   /* FILE, fopen/fclose/fread/fwrite/fget[cs]/fput[cs]/fseek/ftell/
                       fflush/feof/ferror, printf/snprintf/vsnprintf/sprintf/fprintf/
                       vfprintf, stdin/stdout/stderr, EOF, NULL, size_t, va_list */
int   sscanf(const char* s, const char* fmt, ...);
FILE* fdopen(int fd, const char* mode);
int   remove(const char* path);
#ifndef BUFSIZ
#define BUFSIZ 1024
#endif
#endif
