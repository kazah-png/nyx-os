#include "libc.h"

/* cp SRC DST — copy a file. Streams SRC into a freshly created (truncated) DST in
 * 4 KB chunks. The buffer is a .bss array (resident) so read()/write() never touch
 * an unfaulted lazy-heap page. */

static char buf[4096];

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: cp SRC DST\n"); return 1; }
    long in = open(argv[1], O_RDONLY, 0);
    if (in < 0) { printf("cp: %s: cannot open\n", argv[1]); return 1; }
    long out = open(argv[2], O_CREAT | O_TRUNC, 0644);
    if (out < 0) { printf("cp: %s: cannot create\n", argv[2]); close((int)in); return 1; }

    long n;
    while ((n = read((int)in, buf, sizeof(buf))) > 0) {
        long w = 0;
        while (w < n) {
            long k = write((int)out, buf + w, n - w);
            if (k <= 0) { printf("cp: write error\n"); close((int)in); close((int)out); return 1; }
            w += k;
        }
    }
    close((int)in);
    close((int)out);
    return 0;
}
