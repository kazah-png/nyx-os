// ============================================================
// factor.c - prime factorization for the `factor` coreutil + KAT (see factor.h).
// Pure integer logic, float-free and dependency-free (no __int128 / libgcc).
//
// Small factors are peeled by trial division; the remaining cofactor is split with
// deterministic Miller-Rabin (is_prime_u64) + Pollard's rho, so `factor` handles any
// 64-bit number FAST — a product of two 31-bit primes that trial division would grind
// through ~2^31 divisions for is found in a few tens of thousands of rho steps.
// ============================================================
#include "factor.h"

// ---- modular arithmetic (overflow-safe, no 128-bit intermediate) -------------
// a+b mod m without overflowing 64 bits (a,b reduced first; a+b>=m -> a-(m-b)).
static uint64_t addmod64(uint64_t a, uint64_t b, uint64_t m) {
    a %= m; b %= m;
    if (a >= m - b) return a - (m - b);
    return a + b;
}
// a*b mod m by binary (shift-add) doubling — never needs a 128-bit product, so no
// __int128 / libgcc __umodti3 the freestanding kernel can't link.
static uint64_t mulmod64(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t r = 0;
    a %= m;
    while (b) {
        if (b & 1) r = addmod64(r, a, m);
        a = addmod64(a, a, m);
        b >>= 1;
    }
    return r;
}
static uint64_t powmod64(uint64_t base, uint64_t e, uint64_t m) {
    uint64_t r = 1 % m;
    base %= m;
    while (e) {
        if (e & 1) r = mulmod64(r, base, m);
        base = mulmod64(base, base, m);
        e >>= 1;
    }
    return r;
}

// Deterministic Miller-Rabin: for 64-bit n the twelve witnesses 2..37 give a proven-
// correct answer (they suffice past 3.3e24), so this is EXACT, not probabilistic — it
// even rejects Carmichael numbers that fool a naive Fermat test. (Used by the `factor`
// engine below and by kernel.c's `mathx` KAT via factor.h.)
int is_prime_u64(uint64_t n) {
    static const uint64_t W[12] = {2,3,5,7,11,13,17,19,23,29,31,37};
    if (n < 2) return 0;
    for (int i = 0; i < 12; i++) {
        if (n == W[i]) return 1;
        if (n % W[i] == 0) return 0;
    }
    uint64_t d = n - 1; int s = 0;                    // n-1 = d * 2^s, d odd
    while ((d & 1) == 0) { d >>= 1; s++; }
    for (int i = 0; i < 12; i++) {
        uint64_t x = powmod64(W[i], d, n);
        if (x == 1 || x == n - 1) continue;
        int composite = 1;
        for (int r = 1; r < s; r++) {
            x = mulmod64(x, x, n);
            if (x == n - 1) { composite = 0; break; }
        }
        if (composite) return 0;
    }
    return 1;
}

static uint64_t gcd64(uint64_t a, uint64_t b) {
    while (b) { uint64_t t = a % b; a = b; b = t; }
    return a;
}

// Pollard's rho (Floyd cycle detection), f(x)=x^2+c mod n. Retries a different c on a
// degenerate cycle, bounds each attempt, and falls back to trial division so it ALWAYS
// returns a real non-trivial factor. n MUST be an odd composite — callers strip 2s and
// check is_prime_u64 first.
static uint64_t pollard_rho(uint64_t n) {
    for (uint64_t c = 1; c < 64; c++) {
        uint64_t x = 2, y = 2, d = 1, iter = 0;
        do {
            x = addmod64(mulmod64(x, x, n), c, n);        // tortoise: 1 step
            y = addmod64(mulmod64(y, y, n), c, n);        // hare: 2 steps
            y = addmod64(mulmod64(y, y, n), c, n);
            uint64_t diff = (x >= y) ? x - y : y - x;     // |x - y|
            d = gcd64(diff, n);                           // diff==0 -> gcd=n -> retry c
        } while (d == 1 && ++iter < 4000000ull);
        if (d != 1 && d != n) return d;                   // a non-trivial factor
    }
    for (uint64_t d = 3; d <= n / d; d += 2)              // guaranteed fallback
        if (n % d == 0) return d;
    return n;
}

int factor_one(uint64_t n, uint64_t* out, int cap) {
    int k = 0;
    if (n < 2) return 0;
    while ((n & 1ULL) == 0) { if (k < cap) out[k++] = 2; n >>= 1; }   // peel 2s
    for (uint64_t d = 3; d < 1000 && d <= n / d; d += 2)              // peel small odd primes
        while (n % d == 0) { if (k < cap) out[k++] = d; n /= d; }
    // The cofactor now has only factors >= 1000 (or is 1). Split it with a small
    // off-stack worklist: a prime piece is a factor, a composite piece is rho-split.
    uint64_t work[80]; int sp = 0;
    if (n > 1) work[sp++] = n;
    while (sp > 0 && k < cap) {
        uint64_t m = work[--sp];
        if (m <= 1) continue;
        if (is_prime_u64(m)) { out[k++] = m; continue; }
        uint64_t f = pollard_rho(m);
        if (f <= 1 || f == m || sp + 2 > 80) { out[k++] = m; continue; }   // safety
        work[sp++] = f;
        work[sp++] = m / f;
    }
    for (int i = 1; i < k; i++) {                                    // insertion sort ascending
        uint64_t v = out[i]; int j = i - 1;
        while (j >= 0 && out[j] > v) { out[j + 1] = out[j]; j--; }
        out[j + 1] = v;
    }
    return k;
}

// ---- known-answer self-test (`factor`) ----
int factor_selftest(void) {
    struct { uint64_t n; int k; uint64_t f[8]; } t[] = {
        { 0,             0, {0} },                     // n < 2 -> no factors
        { 1,             0, {0} },
        { 2,             1, {2} },
        { 12,            3, {2, 2, 3} },
        { 90,            4, {2, 3, 3, 5} },
        { 97,            1, {97} },                    // prime
        { 30030,         6, {2, 3, 5, 7, 11, 13} },    // 2*3*5*7*11*13 (primorial)
        { 1000003,       1, {1000003} },               // prime
        { 4294967297ULL, 2, {641, 6700417} },          // 2^32 + 1 = 641 * 6700417
        { 1000000007ULL, 1, {1000000007ULL} },         // a 10-digit prime
        // large cases that need Pollard-rho (trial division would be far too slow):
        { 1000000016000000063ULL, 2, {1000000007ULL, 1000000009ULL} },      // 1000000007 * 1000000009
        { 2000000032000000126ULL, 3, {2, 1000000007ULL, 1000000009ULL} },   // 2 * that semiprime
        { 999999999989ULL,        1, {999999999989ULL} },                   // a 12-digit prime
        { 9223372036854775783ULL, 1, {9223372036854775783ULL} },            // largest prime < 2^63
    };
    int nt = (int)(sizeof(t) / sizeof(t[0]));
    for (int i = 0; i < nt; i++) {
        uint64_t out[64];
        int k = factor_one(t[i].n, out, 64);
        if (k != t[i].k) return i + 1;
        for (int j = 0; j < k; j++) if (out[j] != t[i].f[j]) return i + 1;
    }
    // independent check: the factors must multiply back to n (ascending, all prime)
    for (int i = 0; i < nt; i++) {
        if (t[i].n < 2) continue;
        uint64_t out[64]; int k = factor_one(t[i].n, out, 64);
        uint64_t p = 1;
        for (int j = 0; j < k; j++) {
            if (j && out[j] < out[j - 1]) return 100 + i;   // must be ascending
            if (!is_prime_u64(out[j]))    return 150 + i;   // every factor must be prime
            p *= out[j];
        }
        if (p != t[i].n) return 200 + i;
    }
    return 0;
}
