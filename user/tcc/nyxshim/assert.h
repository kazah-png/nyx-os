#ifndef NYX_TCC_ASSERT_H
#define NYX_TCC_ASSERT_H
/* NyxOS tcc-port shim for <assert.h>. x86_64-gen.c and the other backends use
 * assert(); __assert_fail is provided by nyxshim.c (prints to fd 2 + aborts). Keeps
 * the port host-header-free for the in-OS self-host build. */
#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
extern void __assert_fail(const char* expr, const char* file, unsigned int line, const char* func);
#define assert(expr) ((expr) ? (void)0 \
    : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif
#endif
