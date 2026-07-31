#ifndef NYX_TCC_STDINT_H
#define NYX_TCC_STDINT_H
/* NyxOS tcc-port shim for <stdint.h>. Defines the fixed-width integer types DIRECTLY
 * (not via <stddef.h>, which is not on the tcc.elf GCC build's -I path and there
 * resolves to the host's minimal stddef.h). The int8_t..uint64_t block is guarded by
 * __int8_t_defined -- the SAME guard tcc's own include/stddef.h uses -- so when both
 * are seen (the in-OS self-host build has user/tcc/include on -I) whichever is first
 * wins and the other is a no-op. Keeps the port host-header-free. */
#ifndef __int8_t_defined
#define __int8_t_defined
typedef signed char        int8_t;
typedef signed short int    int16_t;
typedef signed int          int32_t;
#ifdef __LP64__
typedef signed long int     int64_t;
#else
typedef signed long long int int64_t;
#endif
typedef unsigned char       uint8_t;
typedef unsigned short int   uint16_t;
typedef unsigned int         uint32_t;
#ifdef __LP64__
typedef unsigned long int    uint64_t;
#else
typedef unsigned long long int uint64_t;
#endif
#endif

/* Pointer-sized ints via the compiler builtins -- identical to what tcc's stddef.h
 * emits, so the redefinition (when both are seen in-OS) is the same type. */
#ifndef _NYX_INTPTR_DEFINED
#define _NYX_INTPTR_DEFINED
typedef __PTRDIFF_TYPE__ intptr_t;
typedef __SIZE_TYPE__    uintptr_t;
#endif

#ifndef SIZE_MAX
#define SIZE_MAX   ((__SIZE_TYPE__)-1)
#endif
#ifndef INT_MAX
#define INT_MAX    2147483647
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif

#endif
