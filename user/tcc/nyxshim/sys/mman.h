#ifndef NYX_TCC_SYS_MMAN_H
#define NYX_TCC_SYS_MMAN_H
/* NyxOS tcc-port shim for <sys/mman.h>. tccrun.c (tcc's -run/JIT path, DISABLED in
 * this port -- no TCC_IS_NATIVE) is still pulled into the ONE_SOURCE build and needs
 * these declarations to compile; providing them here keeps the port host-header-free
 * for the in-OS self-host build. */
#include <stddef.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void*)-1)

void* mmap(void* addr, size_t length, int prot, int flags, int fd, long offset);
int   munmap(void* addr, size_t length);
int   mprotect(void* addr, size_t len, int prot);

#endif
