// ============================================================
// types.h - NyxOS freestanding base types (no standard headers)
// ============================================================
// The zero-dependency primitives every kernel translation unit needs: the
// fixed-width integer typedefs, bool, the GCC stdarg built-ins and NULL. Split
// out of the god-header core/kernel.h (v6.4.128) as the FIRST, low-risk step of
// the incremental modular-header direction (see the architecture-modularity
// note): a per-subsystem public header can now `#include "types.h"` to get just
// the base types instead of pulling in all 1200+ lines of kernel.h — and does so
// without a circular include, since this header depends on nothing. kernel.h
// includes it first, so every existing includer is unaffected.
#ifndef NYX_TYPES_H
#define NYX_TYPES_H

// Basic types (without pulling in standard headers)
typedef unsigned long size_t;
typedef long ssize_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
typedef int wchar_t;
typedef unsigned int mode_t;
typedef int32_t pid_t;
#ifndef __bool_true_false_are_defined
typedef _Bool bool;
#define true 1
#define false 0
#endif
#define false 0
#define true 1

// stdarg (using GCC built-ins)
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)

#define NULL ((void*)0)

#endif // NYX_TYPES_H
