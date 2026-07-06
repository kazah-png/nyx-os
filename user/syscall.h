#ifndef _NYXOS_SYSCALL_H
#define _NYXOS_SYSCALL_H

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_PRINT   2
#define SYS_OPEN    3
#define SYS_READ    4
#define SYS_CLOSE   5
#define SYS_GETPID  6
#define SYS_SBRK    7
#define SYS_FSIZE   8
#define SYS_EXEC    9
#define SYS_FORK    10

/* x86_64 syscall ABI:
 *   RAX = syscall number
 *   RDI = a1, RSI = a2, RDX = a3, R10 = a4, R8 = a5, R9 = a6
 *   Clobbers: RCX (return RIP), R11 (saved RFLAGS)
 *   Return: RAX
 */
static inline long syscall6(long no, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(no), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall5(long no, long a1, long a2, long a3, long a4, long a5) {
    return syscall6(no, a1, a2, a3, a4, a5, 0);
}

static inline long syscall4(long no, long a1, long a2, long a3, long a4) {
    return syscall6(no, a1, a2, a3, a4, 0, 0);
}

static inline long syscall3(long no, long a1, long a2, long a3) {
    return syscall6(no, a1, a2, a3, 0, 0, 0);
}

static inline long syscall2(long no, long a1, long a2) {
    return syscall6(no, a1, a2, 0, 0, 0, 0);
}

static inline long syscall1(long no, long a1) {
    return syscall6(no, a1, 0, 0, 0, 0, 0);
}

static inline void exit(int status) {
    syscall1(SYS_EXIT, status);
    for (;;);
}

static inline long write(int fd, const void* buf, long len) {
    return syscall3(SYS_WRITE, fd, (long)buf, len);
}

static inline void print(const char* s) {
    syscall1(SYS_PRINT, (long)s);
}

static inline long open(const char* path, int flags, int mode) {
    return syscall3(SYS_OPEN, (long)path, flags, mode);
}

static inline long read(int fd, void* buf, long count) {
    return syscall3(SYS_READ, fd, (long)buf, count);
}

static inline long close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static inline long getpid(void) {
    return syscall1(SYS_GETPID, 0);
}

static inline long sbrk(long increment) {
    return syscall1(SYS_SBRK, increment);
}

static inline long fsize(int fd) {
    return syscall1(SYS_FSIZE, fd);
}

static inline long exec(const char* path) {
    return syscall1(SYS_EXEC, (long)path);
}

/* Copy-on-write fork: returns the child's pid in the parent, 0 in the child, and
 * -1 on failure. Both processes then run preemptively, time-sliced by the kernel. */
static inline long fork(void) {
    return syscall1(SYS_FORK, 0);
}

#endif
