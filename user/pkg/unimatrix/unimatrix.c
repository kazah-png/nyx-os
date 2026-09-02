#include "libc.h"

/* unimatrix — the falling "Matrix" glyph rain, for the NyxOS terminal (which speaks
 * ANSI, so the frames overwrite in place via ESC[H). A port of the classic toy for
 * xbm:  `xbm install unimatrix`  then  `unimatrix`.
 *
 * NyxOS adaptation: it renders a BOUNDED number of frames and then exits, so it always
 * terminates cleanly (the shell can't yet Ctrl-C a ring-3 ELF, and it has no reader to
 * close a pipe). The rain is deterministic given a seed, which also makes it testable:
 *
 *   unimatrix [frames] [seed]
 *     frames  how many frames to draw   (default 100)
 *     seed    PRNG seed for a repeatable run (default: 4 bytes from /dev/random)
 *
 * Each column is a downward-falling drop with a short tail; the head glyph is bright,
 * the tail green. Glyphs are drawn from a fixed set with a small LCG. */

#define UM_W    32
#define UM_H    10
#define UM_TAIL 6
static const char* CS = "abcdefghijklmnopqrstuvwxyz0123456789";   /* 36 glyphs */

/* glibc-style LCG; the high bits are the usable random value (0..32767). */
static int lcg(unsigned int* x) {
    *x = *x * 1103515245u + 12345u;
    return (int)((*x >> 16) & 0x7fff);
}

int main(int argc, char** argv) {
    unsigned int frames = 100, seed = 0;
    int have_seed = 0;

    if (argc >= 2) { int v = atoi(argv[1]); if (v > 0) frames = (unsigned int)v; }
    if (argc >= 3) { seed = (unsigned int)strtoul(argv[2], 0, 10); have_seed = 1; }
    if (!have_seed) {
        long fd = open("/dev/random", 0, 0);        /* O_RDONLY */
        if (fd >= 0) {
            unsigned char b[4];
            if (read((int)fd, b, 4) == 4)
                seed = (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
                       ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
            close((int)fd);
        }
        if (seed == 0) seed = 2463534242u;          /* nonzero fallback */
    }

    unsigned int x = seed;
    int head[UM_W];
    for (int c = 0; c < UM_W; c++) head[c] = lcg(&x) % UM_H;

    for (unsigned int f = 0; f < frames; f++) {
        printf("\033[H");                            /* cursor home — overwrite in place */
        for (int r = 0; r < UM_H; r++) {
            for (int c = 0; c < UM_W; c++) {
                int d = (head[c] - r + UM_H) % UM_H; /* rows above/at the head are lit */
                if (d < UM_TAIL) {
                    int g = lcg(&x) % 36;
                    printf(d == 0 ? "\033[97m%c" : "\033[32m%c", CS[g]);
                } else {
                    putchar(' ');
                }
            }
            putchar('\n');
        }
        printf("\033[0m");                           /* reset the color at frame end */
        for (int c = 0; c < UM_W; c++) head[c] = (head[c] + 1) % UM_H;
    }
    return 0;
}
