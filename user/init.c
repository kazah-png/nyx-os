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

    /* execve(): replace a process image. The child forks, then execve()s /hello.elf
     * — its own code is discarded and hello.elf runs in its place (same pid),
     * printing its banner and exit(42); the parent collects that via waitpid. */
    printf("Testing execve() (replace process image)...\n");
    long ecpid = fork();
    if (ecpid == 0) {
        execve("/hello.elf", 0, 0);              /* on success this never returns */
        printf("  execve failed\n");
        exit(1);
    } else if (ecpid > 0) {
        int st = -1;
        waitpid((int)ecpid, &st);
        printf("  [parent] execve'd child exited with code %d (expected 42)\n", st);
    } else {
        printf("  fork() failed\n");
    }

    /* dup2(): redirect stdout into a pipe — the shell-pipeline primitive. The
     * child dup2()s the pipe's write end onto fd 1, so its write(1, ...) lands in
     * the pipe instead of the console; the parent reads the child's stdout back.
     * This is exactly how `a | b` wires processes together. */
    printf("Testing dup2() (stdout -> pipe redirection)...\n");
    int rfd2[2];
    if (pipe(rfd2) == 0) {
        long dpid = fork();
        if (dpid == 0) {
            dup2(rfd2[1], 1);                    /* stdout now writes into the pipe */
            close(rfd2[0]);                      /* drop our copies of the raw ends */
            close(rfd2[1]);
            write(1, "stdout was redirected!", 22);   /* -> pipe, NOT the console */
            exit(0);
        } else if (dpid > 0) {
            close(rfd2[1]);                      /* parent only reads */
            char rb[64];
            long n = read(rfd2[0], rb, sizeof(rb) - 1);  /* blocks for the child */
            if (n >= 0) rb[n] = '\0';
            printf("  [parent] read child's stdout from the pipe: \"%s\" (%ld bytes)\n", rb, n);
            close(rfd2[0]);
            int st;
            waitpid((int)dpid, &st);
        } else {
            printf("  fork() failed\n");
        }
    } else {
        printf("  pipe() failed\n");
    }

    /* execve() argv passing: the child exec's /args.elf with a 4-element argv.
     * The kernel copies the strings onto the new program's entry stack (SysV
     * layout, read by crt0), args.elf prints them and exits with argc — so the
     * waitpid status independently confirms all 4 arguments arrived. */
    printf("Testing execve() argv passing...\n");
    long apid = fork();
    if (apid == 0) {
        char* av[] = { "args", "uno", "dos", "tres", 0 };
        execve("/args.elf", av, 0);
        printf("  execve(/args.elf) failed\n");
        exit(1);
    } else if (apid > 0) {
        int st = -1;
        waitpid((int)apid, &st);
        printf("  [parent] args.elf exited with argc=%d (expected 4)\n", st);
    } else {
        printf("  fork() failed\n");
    }

    /* Lazy sbrk: a multi-page malloc. SYS_SBRK only moved the break; the pages
     * materialise as we write across the buffer (demand-faulted in vm_handle_fault),
     * so a big allocation costs only the pages actually touched. */
    printf("Testing lazy sbrk (demand-paged heap)...\n");
    long brk_before = sbrk(0);
    int hsize = 8000;                              /* spans ~3 heap pages */
    char* big = (char*)malloc(hsize);
    if (big) {
        for (int i = 0; i < hsize; i++) big[i] = (char)(i & 0x7F);   /* touch every page */
        int ok = 1;
        for (int i = 0; i < hsize; i++) if (big[i] != (char)(i & 0x7F)) { ok = 0; break; }
        long brk_after = sbrk(0);
        printf("  malloc(%d): break 0x%lx -> 0x%lx (+%ld bytes lazily), data %s\n",
               hsize, brk_before, brk_after, brk_after - brk_before, ok ? "intact" : "CORRUPT");
        free(big);
    } else {
        printf("  malloc failed\n");
    }

    /* Coreutils via execve: ls, cat, and a cat|wc pipeline — the same /*.elf
     * binaries the shell runs. `ls /` exercises the new getdents() syscall; the
     * pipeline wires cat's stdout into wc's stdin with pipe()+dup2(), so wc should
     * report the welcome file's 2 lines / 8 words / 50 bytes. */
    printf("Testing coreutils (ls, cat, cat|wc via execve)...\n");
    long lpid = fork();
    if (lpid == 0) {
        char* av[] = { "ls", "/", 0 };
        execve("/ls.elf", av, 0);
        exit(1);
    } else if (lpid > 0) {
        int st = 0;
        printf("  $ ls /\n");
        waitpid((int)lpid, &st);
    }
    long ccpid = fork();
    if (ccpid == 0) {
        char* av[] = { "cat", "/home/user/welcome.txt", 0 };
        execve("/cat.elf", av, 0);
        exit(1);
    } else if (ccpid > 0) {
        int st = 0;
        printf("  $ cat /home/user/welcome.txt\n");
        waitpid((int)ccpid, &st);
    }
    int wpfd[2];
    if (pipe(wpfd) == 0) {
        long p1 = fork();
        if (p1 == 0) {                              /* cat -> pipe */
            dup2(wpfd[1], 1); close(wpfd[0]); close(wpfd[1]);
            char* av[] = { "cat", "/home/user/welcome.txt", 0 };
            execve("/cat.elf", av, 0);
            exit(1);
        }
        long p2 = fork();
        if (p2 == 0) {                              /* pipe -> wc */
            dup2(wpfd[0], 0); close(wpfd[0]); close(wpfd[1]);
            char* av[] = { "wc", 0 };
            execve("/wc.elf", av, 0);
            exit(1);
        }
        close(wpfd[0]); close(wpfd[1]);
        int st = 0;
        printf("  $ cat /home/user/welcome.txt | wc   (lines words bytes)\n  ");
        if (p1 > 0) waitpid((int)p1, &st);
        if (p2 > 0) waitpid((int)p2, &st);
    }

    /* waitpid(WNOHANG): the non-blocking reap the shell uses for `&` jobs. Fork a
     * child that spins briefly then exits(7); poll with waitpid3(.., WNOHANG),
     * which returns 0 while the child still runs and its pid once it has exited. */
    printf("Testing waitpid(WNOHANG) (non-blocking reap)...\n");
    long bpid = fork();
    if (bpid == 0) {
        for (volatile long i = 0; i < 2000000; i++) { }
        exit(7);
    } else if (bpid > 0) {
        int polls = 0, st = 0;
        long r;
        for (;;) {
            r = waitpid3((int)bpid, &st, WNOHANG);
            if (r != 0) break;                      /* reaped (pid) or error (-1) */
            polls++;
            for (volatile int j = 0; j < 50000; j++) { }   /* brief pause between polls */
        }
        printf("  polled %d time(s) while running, then reaped pid %ld status %d (expected 7)\n",
               polls, r, st);
    }

    printf("Init complete, exiting.\n");
    return 0;
}
