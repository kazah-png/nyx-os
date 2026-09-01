/* nyxrt.h — the N runtime for NyxOS x86_64 userspace.
 * Linked with every N program. Freestanding, no libc. */
#ifndef NYXRT_H
#define NYXRT_H

/* --- primitive types: N -> C ---
 * Under the in-OS TinyCC (milestone M2/M3: N programs are compiled inside
 * NyxOS) there is no <stdint.h> on the default include path, so the
 * fixed-width types are spelled directly — the target is LP64 x86_64, where
 * char/short/int/long are exactly 8/16/32/64 bits. Hosted builds keep using
 * <stdint.h>, which is authoritative when cross-compiling. */
#ifdef __TINYC__
typedef signed char        nyx_i8;    typedef unsigned char      nyx_u8;
typedef short              nyx_i16;   typedef unsigned short     nyx_u16;
typedef int                nyx_i32;   typedef unsigned int       nyx_u32;
typedef long               nyx_i64;   typedef unsigned long      nyx_u64;
#else
#include <stdint.h>
typedef int8_t   nyx_i8;    typedef uint8_t  nyx_u8;
typedef int16_t  nyx_i16;   typedef uint16_t nyx_u16;
typedef int32_t  nyx_i32;   typedef uint32_t nyx_u32;
typedef int64_t  nyx_i64;   typedef uint64_t nyx_u64;
#endif
typedef nyx_i64  nyx_isize; typedef nyx_u64  nyx_usize;
typedef nyx_u64  nyx_addr;
typedef _Bool    nyx_bool;

/* str: pointer + length (non-owning). Literals use NYX_STR. */
typedef struct { const char* ptr; nyx_u64 len; } nyx_str;
#define NYX_STR(s) ((nyx_str){ (s), sizeof(s) - 1 })

/* v0.23: program arguments. ncc emits main(nyx_i64 __argc, nyx_u8** __argv)
 * and stashes the SysV frame here as its first act; the arg_count()/arg(i)
 * builtins lower to the readers. Header-static on purpose: the emitted main
 * and the accessors always live in the same translation unit. */
static nyx_i64  __nyx_argc;
static nyx_u8** __nyx_argv;
static inline void __nyx_args_set(nyx_i64 c, nyx_u8** v) {
    __nyx_argc = c;
    __nyx_argv = v;
}
static inline nyx_i64 nyx_arg_count(void) { return __nyx_argc; }
static inline nyx_str nyx_arg(nyx_i64 i) {
    nyx_str s = { "", 0 };
    if (i < 0 || i >= __nyx_argc || !__nyx_argv || !__nyx_argv[i]) return s;
    s.ptr = (const char*)__nyx_argv[i];
    { nyx_u64 n = 0; while (s.ptr[n]) n++; s.len = n; }
    return s;
}

/* slice []T (type-erased in the MVP; the checker monomorphizes later). */
typedef struct { void* ptr; nyx_u64 len; } nyx_slice;

/* Universal Result: ok/err fit in a register (i64 or pointer) — enough for the
 * MVP examples. The real backend monomorphizes Result<T,E> per type. */
typedef struct { nyx_bool is_err; nyx_i64 err; nyx_i64 ok; } nyx_result;
#define NYX_OK(v)  ((nyx_result){ 0, 0, (nyx_i64)(v) })
#define NYX_ERR(e) ((nyx_result){ 1, (nyx_i64)(e), 0 })

/* --- NyxOS x86_64 syscall ABI (identical to user/syscall.h) ---
 *   RAX=no, RDI/RSI/RDX/R10/R8/R9=args, clobbers RCX/R11, returns RAX. */
/* Two asm spellings, one ABI. TinyCC's register allocator cannot satisfy six
 * simultaneously-bound registers ("asm constraint could not be satisfied"),
 * so the in-OS build loads the ABI-mandated r10/r8/r9 explicitly inside the
 * asm from memory operands — the exact pattern the NyxOS libc's own syscall6
 * uses to self-compile with tcc. The GCC path keeps register binding. */
#ifdef __TINYC__
static inline nyx_i64 __nyx_syscall6(nyx_i64 no, nyx_i64 a1, nyx_i64 a2,
                                     nyx_i64 a3, nyx_i64 a4, nyx_i64 a5, nyx_i64 a6) {
    nyx_i64 ret;
    __asm__ volatile (
        "movq %5, %%r10\n\t"
        "movq %6, %%r8\n\t"
        "movq %7, %%r9\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(no), "D"(a1), "S"(a2), "d"(a3), "m"(a4), "m"(a5), "m"(a6)
        : "rcx", "r11", "r10", "r8", "r9", "memory");
    return ret;
}
#else
static inline nyx_i64 __nyx_syscall6(nyx_i64 no, nyx_i64 a1, nyx_i64 a2,
                                     nyx_i64 a3, nyx_i64 a4, nyx_i64 a5, nyx_i64 a6) {
    nyx_i64 ret;
    register nyx_i64 r10 __asm__("r10") = a4;
    register nyx_i64 r8  __asm__("r8")  = a5;
    register nyx_i64 r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(no), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}
#endif

/* --- string-interpolation helpers (lowering of EX_STR_INTERP) ---
 * The caller hoists a char buf[] into function scope so the built nyx_str
 * does not dangle. See codegen note in the accompanying message. */
nyx_str __nyx_fmt_begin(char* buf, nyx_u64 cap);
void    __nyx_fmt_str(nyx_str* dst, char* buf, nyx_u64 cap, nyx_str s);
void    __nyx_fmt_i64(nyx_str* dst, char* buf, nyx_u64 cap, nyx_i64 v);
void    __nyx_fmt_hex(nyx_str* dst, char* buf, nyx_u64 cap, nyx_u64 v, int upper);
void    __nyx_fmt_num(nyx_str* dst, char* buf, nyx_u64 cap, nyx_i64 v,
                      int hex, int upper, int width, int zero);

#endif /* NYXRT_H */
