#include "libc.h"

/* stat — print a file's size + type via stat(2), and exercise the new POSIX
 * syscalls (v5.8.80): fstat, lseek, getppid.
 *   stat <file>            e.g.  stat /welcome.txt   or   stat /
 * Auto-execs by bare name from the shell (`stat /file`). */

int main(int argc, char** argv) {
    /* getpid() returns long, getppid() returns int — match each conversion to its
     * argument type (%d on a long is a vararg type mismatch). */
    printf("pid=%ld ppid=%d\n", getpid(), getppid());

    if (argc < 2) { printf("usage: stat <path>\n"); return 0; }
    const char* path = argv[1];

    /* Open FIRST, then fstat the descriptor. Doing a path-based stat() and *then*
     * open()ing the same path resolves the name twice, and the file behind it can be
     * replaced in between — the time-of-check/time-of-use race. Holding the fd pins
     * the exact file we report on and seek in, so the metadata always describes what
     * we actually read. A path that can't be opened (e.g. a directory) falls back to
     * a single stat() below: one lookup, with no later use of the name. */
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        struct stat dst;
        if (stat(path, &dst) != 0) { printf("stat: cannot stat '%s'\n", path); return 1; }
        printf("%s: size=%u  type=%s  mode=0x%x\n", path, dst.st_size,
               S_ISDIR(dst.st_mode) ? "dir" : "file", dst.st_mode);
        return 0;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        printf("stat: cannot fstat '%s'\n", path);
        close(fd);
        return 1;
    }
    printf("%s: size=%u  type=%s  mode=0x%x\n", path, st.st_size,
           S_ISDIR(st.st_mode) ? "dir" : "file", st.st_mode);

    if (S_ISDIR(st.st_mode)) { close(fd); return 0; }

    /* Regular file: lseek to the middle and read one byte — all through that same fd. */
    if (st.st_size >= 2) {
        long mid = lseek(fd, (long)(st.st_size / 2), SEEK_SET);
        char c = 0;
        int n = read(fd, &c, 1);
        long cur = lseek(fd, 0, SEEK_CUR);   /* offset advanced by the read */
        printf("lseek(%u)=%ld  read 1 -> '%c' (n=%d)  now at %ld\n",
               st.st_size / 2, mid, (n == 1 && c >= 32 && c < 127) ? c : '?', n, cur);
        long end = lseek(fd, 0, SEEK_END);
        printf("lseek(SEEK_END)=%ld  (== size %u)\n", end, st.st_size);
    }
    close(fd);
    return 0;
}
