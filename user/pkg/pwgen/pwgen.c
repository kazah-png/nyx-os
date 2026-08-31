#include "libc.h"

/* pwgen — generate strong random credential strings from NyxOS's CSPRNG.
 *
 * Reads bytes from /dev/random (the kernel's HMAC-DRBG, seeded from RDSEED/RDRAND)
 * and maps them onto a character set with REJECTION SAMPLING, so every character is
 * uniform over the set — no modulo bias. A daily-use tool that shows off the OS's
 * real crypto: `xbm install pwgen`, then `/mnt/bin/pwgen 20 3`.
 *
 * Usage: pwgen [-a] [length] [count]
 *   -a        alphanumeric only (drop the symbols)
 *   length    characters per line (default 16, max 256)
 *   count     how many to print   (default 1,  max 100)
 */

static const char* CS_FULL  =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+";
static const char* CS_ALNUM =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

int main(int argc, char** argv) {
    const char* cs = CS_FULL;
    int len = 16, count = 1, argi = 1;

    if (argi < argc && argv[argi][0] == '-' && argv[argi][1] == 'a') { cs = CS_ALNUM; argi++; }
    if (argi < argc) { int v = atoi(argv[argi]); if (v > 0) len = v; argi++; }
    if (argi < argc) { int v = atoi(argv[argi]); if (v > 0) count = v; argi++; }
    if (len > 256) len = 256;
    if (count > 100) count = 100;

    int cslen = (int)strlen(cs);
    /* Largest multiple of cslen that fits in a byte: bytes >= lim are rejected so the
     * remaining ones map uniformly (0..lim-1 covers each character equally often). */
    int lim = 256 - (256 % cslen);

    long fd = open("/dev/random", 0, 0);   /* O_RDONLY */
    if (fd < 0) { printf("pwgen: cannot open /dev/random\n"); return 1; }

    for (int c = 0; c < count; c++) {
        char out[257];
        int n = 0;
        while (n < len) {
            unsigned char b;
            if (read((int)fd, &b, 1) != 1) { printf("pwgen: read error\n"); close((int)fd); return 1; }
            if ((int)b >= lim) continue;       /* reject to stay unbiased */
            out[n++] = cs[b % cslen];
        }
        out[len] = '\0';
        printf("%s\n", out);
    }
    close((int)fd);
    return 0;
}
