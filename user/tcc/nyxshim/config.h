/* NyxOS build config for the vendored TinyCC 0.9.27 (Phase 2 in-OS toolchain).
 * Replaces the ./configure-generated config.h. x86_64/ELF target; no -run/JIT
 * (TCC_IS_NATIVE undefined), no backtrace/bounds-checker. The compiler is
 * cross-compiled with host GCC against the NyxOS libc via the shim headers in
 * this dir; the OS calls are provided with POSIX signatures by nyxshim.c. */
#ifndef NYX_TCC_CONFIG_H
#define NYX_TCC_CONFIG_H
#define TCC_VERSION       "0.9.27"
#define TCC_TARGET_X86_64 1
#define TCC_TARGET_ELF    1
#define CONFIG_TCC_STATIC 1        /* no dlfcn; tcc supplies its own dl* stubs */
#define CONFIG_TCCDIR     "/usr/lib/tcc"
#define NYX_LIBC_NO_SYSCALL 1      /* pulled-in libc.h gives C-lib decls only */
/* Actually make TCC_IS_NATIVE undefined (tcc.h would otherwise auto-define it on an
 * x86_64 host/target, pulling the -run/JIT + backtrace path's host headers
 * <sys/mman.h>/<signal.h>/<sys/ucontext.h> — which don't exist in-OS). The in-OS tcc
 * never JITs, so disabling the whole native path keeps the port host-header-free and
 * lets tcc compile tcc.c itself in-OS (v6.4.8). */
#define TCC_NO_NATIVE     1
#endif
