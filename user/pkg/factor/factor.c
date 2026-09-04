#include "libc.h"

/* factor — print the prime factorization of each integer, in the style of the classic
 * `factor` coreutil. Each number N is printed as `N: p1 p2 p3 ...` with every prime
 * factor listed as many times as it divides N, in ascending order (so the factors
 * multiply back to N). 0 and 1 have no prime factors and print just `N:`.
 *
 *   factor 12            -> 12: 2 2 3
 *   factor 17            -> 17: 17
 *   factor 1 2 60        -> 1:\n2: 2\n60: 2 2 3 5
 *   echo 90 | factor     -> 90: 2 3 3 5
 *
 * Numbers come from the ARGS, or — when none are given — from stdin (whitespace- or
 * newline-separated). Values are unsigned 64-bit; a token that is not a plain decimal
 * integer (or does not fit in 64 bits) is reported on stderr and skipped. Factoring is
 * trial division up to sqrt(N), so a 64-bit number with a huge prime factor is slow but
 * always correct. Pure argv/stdin -> stdout; built INSIDE the OS by `cc` — installs via
 * `xbm install factor`.
 */

/* Print "N: p1 p2 ..." followed by a newline. */
static void factor_print(unsigned long n) {
    printf("%lu:", n);
    unsigned long m = n;
    while (m % 2 == 0 && m != 0) { printf(" 2"); m /= 2; }
    for (unsigned long d = 3; d <= m / d; d += 2) {
        while (m % d == 0) { printf(" %lu", d); m /= d; }
    }
    if (m > 1) printf(" %lu", m);          /* leftover prime (also the n==prime case) */
    putchar('\n');
}

/* Parse one whitespace-trimmed token as an unsigned decimal integer and factor it.
 * Rejects the empty token, any non-digit, and values that overflow 64 bits. */
static void do_token(const char* tok) {
    if (!tok[0]) return;
    for (const char* p = tok; *p; p++) {
        if (*p < '0' || *p > '9') {
            fprintf(stderr, "factor: '%s' is not a valid positive integer\n", tok);
            return;
        }
    }
    /* Overflow guard: ULONG_MAX is 20 digits; anything longer, or a 20-digit token that
     * strtoul had to clamp, is out of range for the 64-bit factorizer. */
    char* end = 0;
    unsigned long v = strtoul(tok, &end, 10);
    int overflow = (v == 0xFFFFFFFFFFFFFFFFUL && strcmp(tok, "18446744073709551615") != 0);
    if (overflow) {
        fprintf(stderr, "factor: '%s' is out of range\n", tok);
        return;
    }
    factor_print(v);
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        for (int i = 1; i < argc; i++) do_token(argv[i]);
    } else {
        /* No args: read whitespace-separated tokens from stdin. */
        char line[1024];
        while (fgets(line, sizeof(line), stdin)) {
            char tok[64];
            int t = 0;
            for (const char* p = line; ; p++) {
                if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\0') {
                    if (t > 0) { tok[t] = '\0'; do_token(tok); t = 0; }
                    if (*p == '\0') break;
                } else if (t < (int)sizeof(tok) - 1) {
                    tok[t++] = *p;
                }
            }
        }
    }
    return 0;
}
