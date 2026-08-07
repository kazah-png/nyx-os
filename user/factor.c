#include "libc.h"

/* factor — print the prime factorisation of each number, like factor(1):
 *   factor [NUMBER...]
 * With no arguments, reads whitespace-separated numbers from standard input.
 * Each line is "N: p1 p2 p3 …" with factors in ascending order and repeated to
 * their multiplicity (e.g. "360: 2 2 2 3 3 5"); N < 2 prints just "N:".
 * Works for any 64-bit unsigned N: small primes are stripped by trial division,
 * then the remaining cofactor is split with Pollard's rho, using a deterministic
 * Miller-Rabin primality test as the base case — so even large semiprimes finish
 * quickly. Non-numeric or overflowing tokens are reported on stderr and skipped. */

typedef unsigned long long u64;

static const u64 SMALL[] = { 2,3,5,7,11,13,17,19,23,29,31,37 };
#define NSMALL 12

/* a+b mod m, overflow-safe for a,b < m (m may be as large as 2^64-1). */
static u64 addmod(u64 a, u64 b, u64 m) { return (a >= m - b) ? a - (m - b) : a + b; }

/* (a*b) mod m via binary double-and-add — 64-bit only, so the freestanding
 * userland link needs no libgcc 128-bit division helper (__umodti3/__udivti3). */
static u64 mulmod(u64 a, u64 b, u64 m) {
    u64 r = 0; a %= m;
    while (b) { if (b & 1) r = addmod(r, a, m); a = addmod(a, a, m); b >>= 1; }
    return r;
}

static u64 powmod(u64 a, u64 e, u64 m) {
    u64 r = 1 % m; a %= m;
    while (e) { if (e & 1) r = mulmod(r, a, m); a = mulmod(a, a, m); e >>= 1; }
    return r;
}

/* Deterministic Miller-Rabin: the 12 SMALL bases are a proven witness set for
 * all 64-bit n, so this is exact (no probabilistic error) over uint64. */
static int is_prime(u64 n) {
    if (n < 2) return 0;
    for (int i = 0; i < NSMALL; i++) { if (n % SMALL[i] == 0) return n == SMALL[i]; }
    u64 d = n - 1; int s = 0;
    while (!(d & 1)) { d >>= 1; s++; }
    for (int i = 0; i < NSMALL; i++) {
        u64 a = SMALL[i] % n; if (a == 0) continue;
        u64 x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        int composite = 1;
        for (int r = 1; r < s; r++) { x = mulmod(x, x, n); if (x == n - 1) { composite = 0; break; } }
        if (composite) return 0;
    }
    return 1;
}

static u64 gcd_u(u64 a, u64 b) { while (b) { u64 t = a % b; a = b; b = t; } return a; }

/* Pollard's rho (Floyd cycle finding); retries with a fresh constant c if a run
 * collapses to n. n is always odd and composite when this is called. */
static u64 pollard_rho(u64 n) {
    if ((n & 1) == 0) return 2;
    for (u64 c = 1; ; c++) {
        u64 x = 2, y = 2, d = 1;
        do {
            x = (mulmod(x, x, n) + c) % n;
            y = (mulmod(y, y, n) + c) % n;
            y = (mulmod(y, y, n) + c) % n;
            u64 diff = x > y ? x - y : y - x;
            d = gcd_u(diff, n);
        } while (d == 1);
        if (d != n) return d;   /* found a non-trivial factor */
    }
}

static u64 g_factors[70];   /* n < 2^64 has at most 64 prime factors */
static int g_nf;

static void factorize(u64 n) {
    for (int i = 0; i < NSMALL; i++) while (n % SMALL[i] == 0) { g_factors[g_nf++] = SMALL[i]; n /= SMALL[i]; }
    if (n == 1) return;
    if (is_prime(n)) { g_factors[g_nf++] = n; return; }
    u64 d = pollard_rho(n);
    factorize(d);
    factorize(n / d);
}

/* Parse a decimal token into u64, rejecting empty / non-digit / overflow. */
static int parse_u64(const char* s, u64* out) {
    if (!*s) return 0;
    u64 v = 0;
    for (const char* p = s; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        if (v > (~(u64)0) / 10) return 0;
        u64 nv = v * 10 + (u64)(*p - '0');
        if (nv < v) return 0;
        v = nv;
    }
    *out = v;
    return 1;
}

/* buffered stdout */
static char wbuf[8192]; static int wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wputc(char c) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = c; }
static void wputu(u64 v) { char t[24]; int i = 0; if (!v) { wputc('0'); return; } while (v) { t[i++] = (char)('0' + (int)(v % 10)); v /= 10; } while (i) wputc(t[--i]); }

static void do_one(const char* s) {
    u64 n;
    if (!parse_u64(s, &n)) {
        static const char msg[] = "factor: invalid or out-of-range number\n";
        write(2, msg, (int)sizeof(msg) - 1);
        return;
    }
    g_nf = 0;
    if (n >= 2) factorize(n);
    for (int i = 1; i < g_nf; i++) {          /* insertion sort ascending */
        u64 k = g_factors[i]; int j = i - 1;
        while (j >= 0 && g_factors[j] > k) { g_factors[j + 1] = g_factors[j]; j--; }
        g_factors[j + 1] = k;
    }
    wputu(n); wputc(':');
    for (int i = 0; i < g_nf; i++) { wputc(' '); wputu(g_factors[i]); }
    wputc('\n');
}

int main(int argc, char** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) do_one(argv[i]);
    } else {
        static char in[65536];
        int total = 0, r;
        while (total < (int)sizeof(in) && (r = (int)read(0, in + total, sizeof(in) - total)) > 0) total += r;
        int i = 0;
        while (i < total) {
            while (i < total && (in[i] == ' ' || in[i] == '\n' || in[i] == '\t' || in[i] == '\r')) i++;
            int s = i;
            while (i < total && in[i] != ' ' && in[i] != '\n' && in[i] != '\t' && in[i] != '\r') i++;
            if (i > s) { char save = in[i]; in[i] = 0; do_one(in + s); in[i] = save; }
        }
    }
    wflush();
    return 0;
}
