#include "libc.h"

/* seq — print a sequence of integers, like GNU seq's core.
 *   seq LAST                -> 1, 2, ..., LAST
 *   seq FIRST LAST          -> FIRST, ..., LAST (step 1)
 *   seq FIRST INCR LAST     -> FIRST, FIRST+INCR, ... (not past LAST)
 * Options: -s SEP (separator between numbers, default newline); -w (pad all
 * numbers to equal width with leading zeros). Integers only; INCR may be
 * negative to count down. A trailing newline is always printed. */

static long parse_long(const char* s) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

/* Decimal width of v, including a leading '-' for negatives. */
static int numlen(long v) {
    int n = (v < 0) ? 1 : 0;
    unsigned long u = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0) return n + 1;
    while (u) { n++; u /= 10; }
    return n;
}

/* Format v into out, zero-padded on the left to `width` (the sign, if any,
 * leads the zeros). Returns the length written. */
static int fmt_num(char* out, long v, int width) {
    char tmp[24];
    int t = 0, neg = (v < 0);
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    if (u == 0) tmp[t++] = '0';
    while (u) { tmp[t++] = (char)('0' + (u % 10)); u /= 10; }

    int total = t + (neg ? 1 : 0), pad = width - total, o = 0;
    if (pad < 0) pad = 0;
    if (neg) out[o++] = '-';
    for (int i = 0; i < pad; i++) out[o++] = '0';
    while (t > 0) out[o++] = tmp[--t];
    out[o] = '\0';
    return o;
}

int main(int argc, char** argv) {
    const char* sep = "\n";
    int wflag = 0, ai = 1;

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1] && !(argv[ai][1] >= '0' && argv[ai][1] <= '9'); ai++) {
        const char* a = argv[ai];
        if (a[1] == 's')      sep = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : sep);
        else if (a[1] == 'w') wflag = 1;
        else { printf("seq: invalid option %s\n", a); return 1; }
    }

    long first = 1, incr = 1, last;
    int npos = argc - ai;
    if (npos == 1)      { last = parse_long(argv[ai]); }
    else if (npos == 2) { first = parse_long(argv[ai]); last = parse_long(argv[ai + 1]); }
    else if (npos == 3) { first = parse_long(argv[ai]); incr = parse_long(argv[ai + 1]); last = parse_long(argv[ai + 2]); }
    else { printf("usage: seq [-w] [-s SEP] [FIRST [INCR]] LAST\n"); return 1; }
    if (incr == 0) { printf("seq: increment must not be zero\n"); return 1; }

    int width = 0;
    if (wflag) { int w1 = numlen(first), w2 = numlen(last); width = (w1 > w2) ? w1 : w2; }

    char buf[32];
    int count = 0;
    for (long v = first; (incr > 0) ? (v <= last) : (v >= last); v += incr) {
        if (count++) write(1, sep, strlen(sep));
        write(1, buf, fmt_num(buf, v, width));
    }
    if (count) write(1, "\n", 1);
    return 0;
}
