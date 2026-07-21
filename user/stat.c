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

    struct stat st;
    if (stat(path, &st) != 0) { printf("stat: cannot stat '%s'\n", path); return 1; }
    printf("%s: size=%u  type=%s  mode=0x%x\n", path, st.st_size,
           S_ISDIR(st.st_mode) ? "dir" : "file", st.st_mode);

    if (S_ISDIR(st.st_mode)) return 0;

    /* Regular file: fstat it, then lseek to the middle and read one byte. */
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) { printf("stat: cannot open '%s'\n", path); return 1; }

    struct stat fst;
    if (fstat(fd, &fst) == 0)
        printf("fstat(fd=%d): size=%u %s\n", fd, fst.st_size,
               (fst.st_size == st.st_size) ? "(matches stat)" : "(MISMATCH!)");

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
