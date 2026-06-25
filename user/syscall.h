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

static inline int syscall3(int no, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(no), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int no, int a1) {
    return syscall3(no, a1, 0, 0);
}

static inline int syscall2(int no, int a1, int a2) {
    return syscall3(no, a1, a2, 0);
}

static inline void exit(int status) {
    syscall1(SYS_EXIT, status);
    for (;;);
}

static inline int write(int fd, const void* buf, int len) {
    return syscall3(SYS_WRITE, fd, (int)buf, len);
}

static inline void print(const char* s) {
    syscall1(SYS_PRINT, (int)s);
}

static inline int open(const char* path, int flags, int mode) {
    return syscall3(SYS_OPEN, (int)path, flags, mode);
}

static inline int read(int fd, void* buf, int count) {
    return syscall3(SYS_READ, fd, (int)buf, count);
}

static inline int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static inline int getpid(void) {
    return syscall1(SYS_GETPID, 0);
}

static inline int sbrk(int increment) {
    return syscall1(SYS_SBRK, increment);
}

static inline int fsize(int fd) {
    return syscall1(SYS_FSIZE, fd);
}

static inline int exec(const char* path) {
    return syscall1(SYS_EXEC, (int)path);
}

#endif
