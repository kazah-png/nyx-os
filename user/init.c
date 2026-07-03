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

    /* Create + write + read-back a file entirely from ring 3. */
    long wfd = open("/tmp/hello.txt", 1, 0644);   /* flags=1 -> O_CREAT */
    if (wfd >= 0) {
        const char* msg = "written from userspace\n";
        long wn = write(wfd, msg, 23);
        close(wfd);
        long rfd = open("/tmp/hello.txt", 0, 0);
        char rb[64];
        long rn = (rfd >= 0) ? read(rfd, rb, sizeof(rb) - 1) : -1;
        if (rn > 0) { rb[rn] = '\0'; printf("wrote %ld, read back %ld: %s", wn, rn, rb); }
        if (rfd >= 0) close(rfd);
    }

    printf("Init complete, exiting.\n");
    return 0;
}
