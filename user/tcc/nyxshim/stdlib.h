#ifndef NYX_TCC_STDLIB_H
#define NYX_TCC_STDLIB_H
#include <stdint.h>   /* GCC freestanding: (u)int{8,16,32,64}_t etc. tcc relies on */
#include "libc.h"     /* malloc/free/realloc/calloc, qsort, getenv, atoi, strtol/strtoul */
void   abort(void);
void   exit(int status);
double strtod(const char* nptr, char** endptr);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
long long          strtoll(const char* nptr, char** endptr, int base);
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif
#endif
