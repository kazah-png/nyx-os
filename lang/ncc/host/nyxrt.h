/* nyxrt.h — HOST TEST SHIM for the N runtime.
 * Same API as the real NyxOS runtime (user/nyxrt.h), but __nyx_syscall6
 * maps NyxOS syscall numbers to Linux x86_64 ones, so N programs compiled
 * by ncc can be RUN on the dev machine for behavioral testing:
 *
 *   NyxOS SYS_WRITE  = 1  -> Linux write  = 1   (identical number and args)
 *   NyxOS SYS_GETPID = 6  -> Linux getpid = 39
 *   NyxOS SYS_EXIT   = 0  -> Linux exit   = 60
 *   NyxOS SYS_OPEN   = 3  -> Linux open   = 2   (path, flags, mode)
 *   NyxOS SYS_READ   = 4  -> Linux read   = 0
 *   NyxOS SYS_CLOSE  = 5  -> Linux close  = 3
 *   NyxOS SYS_SBRK   = 7  -> emulated: Linux brk(2) sets an absolute break
 *                            while NyxOS sbrk(incr) bump-allocates and
 *                            returns the old break, so the shim serves it
 *                            from a static 1 MiB arena instead.
 *
 * This works because the x86_64 `syscall` instruction ABI (RAX = number,
 * RDI/RSI/RDX/R10/R8/R9 = args, RAX = return) is the same on both kernels;
 * only the numbers differ. Everything unmapped returns -1. Error returns
 * differ in DETAIL: NyxOS syscalls report a bare -1, Linux reports -errno —
 * result-enum bindings capture either, but the numeric error value an N
 * program prints under this shim is host-specific.
 * TEST HARNESS ONLY — never ships to NyxOS. */
#ifndef NYXRT_H
#define NYXRT_H
#include <stdint.h>

typedef int8_t   nyx_i8;    typedef uint8_t  nyx_u8;
typedef int16_t  nyx_i16;   typedef uint16_t nyx_u16;
typedef int32_t  nyx_i32;   typedef uint32_t nyx_u32;
typedef int64_t  nyx_i64;   typedef uint64_t nyx_u64;
typedef int64_t  nyx_isize; typedef uint64_t nyx_usize;
typedef uint64_t nyx_addr;
typedef _Bool    nyx_bool;

typedef struct { const char* ptr; nyx_u64 len; } nyx_str;
#define NYX_STR(s) ((nyx_str){ (s), sizeof(s) - 1 })

typedef struct { void* ptr; nyx_u64 len; } nyx_slice;

typedef struct { nyx_bool is_err; nyx_i64 err; nyx_i64 ok; } nyx_result;
#define NYX_OK(v)  ((nyx_result){ 0, 0, (nyx_i64)(v) })
#define NYX_ERR(e) ((nyx_result){ 1, (nyx_i64)(e), 0 })

static inline nyx_i64 __nyx_syscall6(nyx_i64 no, nyx_i64 a1, nyx_i64 a2,
                                     nyx_i64 a3, nyx_i64 a4, nyx_i64 a5, nyx_i64 a6) {
    if (no == 7) {                /* NyxOS sbrk(incr) -> old break: Linux brk
                                   * has different semantics, so serve it from
                                   * a static arena (plenty for tests) */
        static char __shim_heap[1 << 20];
        static nyx_u64 __shim_brk;
        if (a1 < 0 || (nyx_u64)a1 > sizeof(__shim_heap) - __shim_brk) return -1;
        nyx_i64 old = (nyx_i64)(nyx_addr)&__shim_heap[__shim_brk];
        __shim_brk += (nyx_u64)a1;
        return old;
    }
    switch (no) {                 /* NyxOS number -> Linux number */
        case 1: no = 1;  break;   /* write  */
        case 6: no = 39; break;   /* getpid */
        case 0: no = 60; break;   /* exit   */
        case 3: no = 2;  break;   /* open   */
        case 4: no = 0;  break;   /* read   */
        case 5: no = 3;  break;   /* close  */
        case 19: no = 9; break;   /* mmap — same arg order; NyxOS uses the
                                   * POSIX PROT/MAP flag values, so flag
                                   * words pass through unchanged */
        default: return -1;
    }
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

nyx_str __nyx_fmt_begin(char* buf, nyx_u64 cap);
void    __nyx_fmt_str(nyx_str* dst, char* buf, nyx_u64 cap, nyx_str s);
void    __nyx_fmt_i64(nyx_str* dst, char* buf, nyx_u64 cap, nyx_i64 v);

#endif /* NYXRT_H */
