#include "libc.h"

/* SIGUSR1 handler for the signals demo — runs in ring 3 when the signal is
 * delivered, then returns (through the crt0 trampoline / SYS_SIGRETURN) to wherever
 * the process was interrupted. printf here is fine: it just does write() syscalls,
 * and the signal is blocked while its own handler runs. */
static volatile int sigusr1_count = 0;
static void on_sigusr1(int sig) {
    sigusr1_count++;
    printf("  [handler] caught signal %d (SIGUSR1), count now %d\n", sig, sigusr1_count);
}

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

    /* Signals: (1) install a handler, raise it, and confirm the handler ran AND
     * control returned to main (the kernel diverts into the handler, then SYS_SIGRETURN
     * restores this context); (2) SIG_IGN drops a signal; (3) a cross-process kill with
     * the default action terminates the target — waitpid reports 128+signo. */
    printf("Testing signals (handler + SIG_IGN + kill/default-terminate)...\n");
    signal(SIGUSR1, on_sigusr1);
    printf("  installed SIGUSR1 handler; raising SIGUSR1...\n");
    raise(SIGUSR1);
    printf("  returned to main after the handler; ran %d time(s) (expect 1)\n", sigusr1_count);

    signal(SIGUSR2, SIG_IGN);
    raise(SIGUSR2);
    printf("  raised SIGUSR2 with SIG_IGN -> ignored, still running\n");

    long spid = fork();
    if (spid == 0) {
        for (;;) getpid();                  /* busy via syscalls so the signal can deliver */
    } else if (spid > 0) {
        kill((int)spid, SIGTERM);           /* default action: terminate the child */
        int st = 0;
        waitpid((int)spid, &st);
        printf("  kill(child %ld, SIGTERM) -> waitpid status %d (expect %d = 128+SIGTERM)\n",
               spid, st, 128 + SIGTERM);
    }

    /* mmap: anonymous demand-zero memory. Map 3 pages; read them first (they
     * fault in as zero), then write a pattern across all of them and read it back
     * (each page materialised on first touch), then munmap to release the region. */
    printf("Testing mmap (anonymous demand-zero pages)...\n");
    unsigned long msize = 12288;                 /* 3 pages */
    char* m = (char*)mmap(0, msize, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        printf("  mmap failed\n");
    } else {
        int zero = 1;
        for (unsigned long i = 0; i < msize; i += 1024)   /* touch each page: reads 0 */
            if (m[i] != 0) { zero = 0; break; }
        for (unsigned long i = 0; i < msize; i++) m[i] = (char)((i * 7) & 0x7F);
        int ok = 1;
        for (unsigned long i = 0; i < msize; i++)
            if (m[i] != (char)((i * 7) & 0x7F)) { ok = 0; break; }
        printf("  mmap(%lu) -> %p, demand-zero=%s, r/w across 3 pages %s\n",
               msize, m, zero ? "yes" : "NO", ok ? "intact" : "CORRUPT");
        munmap(m, msize);
        printf("  munmap released the region\n");
    }

    /* File redirection primitives (what the shell's >, >>, < are built on): a child
     * opens /tmp/rd.txt (O_CREAT|O_TRUNC), dup2's it onto fd 1 and execve's echo, so
     * echo's stdout lands in the file; a second child appends with O_APPEND; the
     * parent reads the file back. Exercises VFS dup2 (move semantics) + O_TRUNC/O_APPEND. */
    printf("Testing file redirection (open O_TRUNC/O_APPEND + dup2)...\n");
    long r1 = fork();
    if (r1 == 0) {
        long fd = open("/tmp/rd.txt", O_CREAT | O_TRUNC, 0644);
        if (fd < 0) exit(1);
        dup2((int)fd, 1); close((int)fd);           /* stdout -> the file (truncated) */
        char* av[] = { "echo", "first line", 0 };
        execve("/echo.elf", av, 0);
        exit(1);
    } else if (r1 > 0) {
        int st; waitpid((int)r1, &st);
    }
    long r2 = fork();
    if (r2 == 0) {
        long fd = open("/tmp/rd.txt", O_CREAT | O_APPEND, 0644);
        if (fd < 0) exit(1);
        dup2((int)fd, 1); close((int)fd);           /* stdout -> appended to the file */
        char* av[] = { "echo", "second line", 0 };
        execve("/echo.elf", av, 0);
        exit(1);
    } else if (r2 > 0) {
        int st; waitpid((int)r2, &st);
    }
    long rfd = open("/tmp/rd.txt", O_RDONLY, 0);
    if (rfd >= 0) {
        char rb[128];
        long got = read((int)rfd, rb, sizeof(rb) - 1);
        if (got > 0) rb[got] = '\0';
        printf("  /tmp/rd.txt = %ld bytes (expect 2 lines):\n%s", got, got > 0 ? rb : "");
        close((int)rfd);
    }

    /* Per-process working directory: getcwd, chdir into /home/user, then open a
     * RELATIVE path ("welcome.txt") that the kernel resolves against the cwd, and
     * chdir("..") to exercise path normalization. */
    printf("Testing chdir/getcwd + relative open...\n");
    char cwd[128];
    getcwd(cwd, sizeof(cwd));
    printf("  cwd at start: %s\n", cwd);
    if (chdir("/home/user") == 0) {
        getcwd(cwd, sizeof(cwd));
        printf("  after chdir(/home/user): %s\n", cwd);
        long wfd = open("welcome.txt", O_RDONLY, 0);   /* relative to the cwd */
        if (wfd >= 0) {
            char b[64];
            long got = read((int)wfd, b, sizeof(b) - 1);
            if (got > 0) b[got] = '\0';
            printf("  open(\"welcome.txt\") [relative] -> %ld bytes: %s", got, got > 0 ? b : "\n");
            close((int)wfd);
        } else {
            printf("  relative open failed\n");
        }
        chdir("..");
        getcwd(cwd, sizeof(cwd));
        printf("  after chdir(\"..\"): %s (expect /home)\n", cwd);
    }

    /* mkdir/unlink from ring 3: make a directory, create a file in it, remove the
     * file (a re-open must then fail), remove the directory, and confirm mkdir
     * under a missing parent is rejected. */
    printf("Testing mkdir/unlink...\n");
    int mk = (int)mkdir("/tmp/mkd", 0755);
    long tfd = open("/tmp/mkd/f.txt", O_CREAT, 0644);
    if (tfd >= 0) close((int)tfd);
    int un = (int)unlink("/tmp/mkd/f.txt");
    long gone = open("/tmp/mkd/f.txt", O_RDONLY, 0);
    int und = (int)unlink("/tmp/mkd");
    int mkbad = (int)mkdir("/no/such/parent/dir", 0755);
    printf("  mkdir=%d create+unlink=%d reopen=%ld (expect <0) rmdir=%d bad-mkdir=%d (expect -1)\n",
           mk, un, gone, und, mkbad);

    /* File-backed mmap: map /home/user/welcome.txt read-only and read its contents
     * straight out of memory (pages fault in filled from the file's snapshot). */
    printf("Testing file-backed mmap + mprotect...\n");
    long mfd = open("/home/user/welcome.txt", O_RDONLY, 0);
    if (mfd >= 0) {
        long msz = fsize((int)mfd);
        char* fmap = (char*)mmap(0, msz, PROT_READ, MAP_PRIVATE, (int)mfd, 0);
        if (fmap != MAP_FAILED) {
            printf("  mmap(welcome.txt, %ld) -> %p, contents: %s", msz, fmap, fmap);
            munmap(fmap, msz);
        } else {
            printf("  file mmap failed\n");
        }
        /* offset mmap (proves the 6th syscall arg is plumbed): map from byte 5 of
         * "Type 'help' for commands.\n" -> the mapping should start at "help'..." */
        char* omap = (char*)mmap(0, msz - 5, PROT_READ, MAP_PRIVATE, (int)mfd, 5);
        if (omap != MAP_FAILED) {
            printf("  mmap(welcome.txt, offset 5) -> starts at: %s", omap);
            munmap(omap, msz - 5);
        }
        close((int)mfd);
    }
    /* mprotect: an anonymous page mapped READ-ONLY reads back zero; mprotect to
     * READ|WRITE then makes it writable (a write with no mprotect would fault and
     * panic — this proves mprotect flipped the page to writable). */
    char* pm = (char*)mmap(0, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pm != MAP_FAILED) {
        int was_zero = (pm[0] == 0 && pm[200] == 0);      /* RO, demand-zero (touch it) */
        long mp = mprotect(pm, 4096, PROT_READ | PROT_WRITE);
        pm[0] = 'N'; pm[200] = 'X';                        /* now writable */
        int wrote = (pm[0] == 'N' && pm[200] == 'X');
        printf("  mprotect: RO page zero=%d, after RO->RW write ok=%d (mprotect=%ld)\n",
               was_zero, wrote, mp);
        munmap(pm, 4096);
    }

    /* /dev special files: /dev/zero reads endless zeros, /dev/null EOFs on read and
     * swallows writes, /dev/random yields pseudo-random bytes. */
    printf("Testing /dev special files (null/zero/random)...\n");
    char db[16];
    long zfd = open("/dev/zero", O_RDONLY, 0);
    if (zfd >= 0) {
        for (int i = 0; i < 16; i++) db[i] = (char)0xAA;   /* poison, then read over it */
        long n = read((int)zfd, db, 16);
        int allz = (n == 16);
        for (long i = 0; i < n; i++) if (db[i] != 0) { allz = 0; break; }
        printf("  /dev/zero  -> read %ld bytes, all-zero=%d\n", n, allz);
        close((int)zfd);
    }
    long nfd = open("/dev/null", O_RDONLY, 0);
    if (nfd >= 0) {
        long rn = read((int)nfd, db, 8);
        long wn = write((int)nfd, "discard me", 10);
        printf("  /dev/null  -> read=%ld (want 0), write=%ld (want 10, discarded)\n", rn, wn);
        close((int)nfd);
    }
    long rndfd = open("/dev/random", O_RDONLY, 0);
    if (rndfd >= 0) {
        long n = read((int)rndfd, db, 8);
        int varied = 0;
        for (long i = 1; i < n; i++) if (db[i] != db[0]) { varied = 1; break; }
        printf("  /dev/random-> read %ld bytes, varied=%d, first=0x%x\n",
               n, varied, (unsigned)(unsigned char)db[0]);
        close((int)rndfd);
    }

    /* Process enumeration (SYS_GETPROCS, the primitive behind `ps`): snapshot the
     * table and confirm we can see ourselves. We must appear (running our own comm)
     * with a sensible ppid; the count should be at least 1. */
    printf("Testing process enumeration (getprocs)...\n");
    static nyx_procinfo_t plist[64];
    long np = getprocs(plist, 64);
    int self_seen = 0;
    long mypid = getpid();
    for (long i = 0; i < np; i++)
        if (plist[i].pid == (unsigned)mypid) self_seen = 1;
    printf("  getprocs -> %ld processes, self(pid %ld) visible=%d\n", np, mypid, self_seen);
    for (long i = 0; i < np && i < 6; i++)
        printf("    pid=%u ppid=%u state=%u comm=%s\n",
               plist[i].pid, plist[i].ppid, plist[i].state, plist[i].comm);

    /* /proc pseudo-filesystem (generated VFS nodes — no new syscalls). Read a few
     * static files, then our OWN /proc/<pid>/status — proving per-pid dirs appear
     * on demand and reflect live process state. */
    printf("Testing /proc filesystem...\n");
    static char pbuf[640];   /* large enough for /proc/<pid>/maps */
    const char* pfiles[3];
    pfiles[0] = "/proc/version"; pfiles[1] = "/proc/meminfo"; pfiles[2] = "/proc/uptime";
    for (int i = 0; i < 3; i++) {
        long pfd = open(pfiles[i], O_RDONLY, 0);
        if (pfd >= 0) {
            long n = read((int)pfd, pbuf, sizeof(pbuf) - 1);
            if (n < 0) n = 0;
            pbuf[n] = '\0';
            printf("  %s (%ld B): %s", pfiles[i], n, pbuf);
            close((int)pfd);
        } else {
            printf("  %s: open failed\n", pfiles[i]);
        }
    }
    char spath[40];
    sprintf(spath, "/proc/%d/status", (int)mypid);   /* user sprintf has no %l */
    long stfd = open(spath, O_RDONLY, 0);
    if (stfd >= 0) {
        long n = read((int)stfd, pbuf, sizeof(pbuf) - 1);
        if (n < 0) n = 0;
        pbuf[n] = '\0';
        printf("  %s:\n%s", spath, pbuf);
        close((int)stfd);
    } else {
        printf("  %s: open failed\n", spath);
    }
    /* /proc/<pid>/maps: our mapped memory regions (program, shared libc, stack). */
    char mpath[40];
    sprintf(mpath, "/proc/%d/maps", (int)mypid);
    long mpfd = open(mpath, O_RDONLY, 0);
    if (mpfd >= 0) {
        long n = read((int)mpfd, pbuf, sizeof(pbuf) - 1);
        if (n < 0) n = 0;
        pbuf[n] = '\0';
        printf("  %s:\n%s", mpath, pbuf);
        close((int)mpfd);
    }

    /* Timed key read (SYS_READKEY, the primitive behind `top`): with no key
     * pressed it must block ~the timeout and return 0. */
    printf("Testing readkey timeout (no key -> 0)...\n");
    long rk = readkey(150);
    printf("  readkey(150ms) -> %ld (expect 0)\n", rk);

    printf("Init complete, exiting.\n");
    return 0;
}
