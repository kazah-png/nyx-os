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
#define SYS_WAITPID 11
#define SYS_PIPE    12
#define SYS_EXECVE  13
#define SYS_DUP2    14
#define SYS_GETDENTS 15

#define WNOHANG     1   /* waitpid option: don't block if no child is ready */

/* One directory entry as returned by getdents(). Layout MUST match the kernel's
 * record in SYS_GETDENTS (syscall.c): 64-byte name + u32 type = 68 bytes, no
 * padding. type is the VFS node type (1 = directory, else regular file). */
typedef struct {
    char name[64];
    unsigned int type;
} nyx_dirent_t;

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

/* Wait for a child to exit and collect its status. Returns the child's pid (with
 * its exit code written to *status) or -1 if there is no such child. The kernel
 * blocks the caller until a child exits (pid <= 0 waits for any child). */
static inline long waitpid(int pid, int* status) {
    return syscall2(SYS_WAITPID, pid, (long)status);
}

/* waitpid with options. waitpid3(pid, &st, WNOHANG) returns the pid if that child
 * has already exited (status in *status), 0 if it is still running, or -1 if there
 * is no such child — the non-blocking reap the shell uses for `&` background jobs. */
static inline long waitpid3(int pid, int* status, int options) {
    return syscall3(SYS_WAITPID, pid, (long)status, options);
}

/* Enumerate directory `path` into up to `max` nyx_dirent_t records at `buf`.
 * Returns the number of entries written, or -1 on error. The caller must ensure
 * every page of `buf` is resident before the call (the kernel copies into it and
 * cannot fault a lazy-sbrk heap page in) — memset the buffer first. */
static inline long getdents(const char* path, nyx_dirent_t* buf, int max) {
    return syscall3(SYS_GETDENTS, (long)path, (long)buf, max);
}

/* Create a pipe: fds[0] is the read end, fds[1] the write end. Returns 0, or -1.
 * A read() on the read end blocks until a writer writes (or all writers close,
 * which yields EOF = 0). Survives fork(), so it connects a parent and child. */
static inline long pipe(int fds[2]) {
    return syscall1(SYS_PIPE, (long)fds);
}

/* Replace the current process image with the program at `path`. On success it does
 * not return (the new program runs with the same pid and open fds); returns -1 only
 * on failure. argv/envp are accepted for signature compatibility but not yet passed
 * to the new program (crt0 uses argc=0). */
static inline long execve(const char* path, char* const argv[], char* const envp[]) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

/* Duplicate oldfd onto newfd (closing newfd first if open) — the redirection
 * primitive: dup2(pipefd[1], 1) makes a process's stdout flow into the pipe.
 * Pipe fds only for now (their ends are reference-counted). Returns newfd or -1. */
static inline long dup2(int oldfd, int newfd) {
    return syscall2(SYS_DUP2, oldfd, newfd);
}

#endif
