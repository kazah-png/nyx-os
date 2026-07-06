#include "libc.h"

int main(void) {
    printf("\n*** NyxOS Userspace Init (x86_64) ***\n");
    printf("PID: %ld\n", getpid());
    printf("Welcome to NyxOS userspace!\n");
    printf("Testing printf formats: int=%d hex=%x str=\"%s\" char='%c' ptr=%p\n",
           42, 0xdead, "hello", 'X', (unsigned long)0x12345678);

    char *buf = (char*)malloc(64);
    if (buf) {
        int n = snprintf(buf, 64, "malloc+snprintf: %d + %d = %d\n", 10, 20, 30);
        write(1, buf, n);
        free(buf);
    }

    /* File I/O through the VFS. fd must be a small integer (not a kernel
     * pointer) — the kernel translates it via a per-open descriptor table. */
    long fd = open("/home/user/welcome.txt", 0, 0);
    if (fd >= 0) {
        long sz = fsize(fd);
        printf("open(welcome.txt) -> fd=%ld, size=%ld\n", fd, sz);
        char fbuf[128];
        long n = read(fd, fbuf, sizeof(fbuf) - 1);
        if (n > 0) {
            fbuf[n] = '\0';
            printf("read %ld bytes: %s", n, fbuf);
        }
        close(fd);
    } else {
        printf("open(welcome.txt) failed: %ld\n", fd);
    }

    /* Opening a missing file must fail cleanly (negative fd). */
    long bad = open("/no/such/file", 0, 0);
    printf("open(missing) -> %ld (expected < 0)\n", bad);

    /* Create a file and write it in two chunks — offsets must advance so the
     * second write appends rather than overwriting. */
    long wfd = open("/tmp/hello.txt", 1, 0644);   /* flags=1 -> O_CREAT */
    if (wfd >= 0) {
        write(wfd, "Hello, ", 7);
        write(wfd, "userspace file I/O!\n", 20);   /* appends at offset 7 */
        close(wfd);

        /* Read it back in two streaming reads — offsets advance across calls. */
        long rfd = open("/tmp/hello.txt", 0, 0);
        if (rfd >= 0) {
            char rb[64];
            long a = read(rfd, rb, 7);              /* first 7 bytes */
            long b = read(rfd, rb + a, sizeof(rb) - 1 - a); /* the rest */
            rb[a + b] = '\0';
            printf("streamed %ld+%ld bytes: %s", a, b, rb);
            close(rfd);
        }
    }

    /* fork() + waitpid(): copy-on-write address-space clone, then reap the child.
     * The child gets an independent copy of memory — writing `cow_test` in the
     * child faults in a private page and leaves the parent's value untouched
     * (same virtual address, different physical page after the COW). The parent
     * then waitpid()s to collect the child's exit code. */
    printf("Testing fork() + waitpid() (COW clone + child reaping)...\n");
    volatile int cow_test = 100;
    long pid = fork();
    if (pid == 0) {
        cow_test = 200;                         /* private copy — parent won't see this */
        printf("  [child]  fork()=0, getpid=%ld, cow_test=%d (own copy)\n",
               getpid(), cow_test);
        /* Hammer the syscall path while the parent is blocked in waitpid(). With
         * the old single shared syscall stack these calls would clobber the
         * parent's parked frame; per-process syscall stacks keep it intact. */
        for (int i = 1; i <= 3; i++)
            printf("  [child]  working %d/3 (parent is blocked in waitpid)\n", i);
        exit(123);                              /* exit code collected by the parent */
    } else if (pid > 0) {
        printf("  [parent] fork()=%ld (child pid), getpid=%ld, cow_test=%d (unchanged)\n",
               pid, getpid(), cow_test);
        int status = -1;
        long w = waitpid((int)pid, &status);    /* truly blocks until the child exits */
        printf("  [parent] waitpid(%ld) -> reaped pid %ld, child exit code = %d\n",
               pid, w, status);
    } else {
        printf("  fork() failed: %ld\n", pid);
    }

    /* pipe() + fork(): a blocking IPC channel between parent and child. The child
     * read()s the pipe (blocking until data arrives); the parent write()s a message
     * and the child wakes with it — cross-process byte transfer through the kernel. */
    printf("Testing pipe() + blocking read across fork()...\n");
    int pfd[2];
    if (pipe(pfd) == 0) {
        long cpid = fork();
        if (cpid == 0) {
            close(pfd[1]);                       /* child only reads */
            char buf[64];
            long n = read(pfd[0], buf, sizeof(buf) - 1);   /* blocks until parent writes */
            if (n >= 0) buf[n] = '\0';
            printf("  [child]  read %ld bytes from pipe: \"%s\"\n", n, buf);
            close(pfd[0]);
            exit(0);
        } else if (cpid > 0) {
            close(pfd[0]);                        /* parent only writes */
            const char* msg = "hello from parent via pipe!";
            long w = write(pfd[1], msg, 27);
            printf("  [parent] wrote %ld bytes to the pipe\n", w);
            close(pfd[1]);
            int st;
            waitpid((int)cpid, &st);
            printf("  [parent] child finished\n");
        } else {
            printf("  fork() failed\n");
        }
    } else {
        printf("  pipe() failed\n");
    }

    printf("Init complete, exiting.\n");
    return 0;
}
