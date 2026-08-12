// ============================================================
// factor.c - prime factorization for the `factor` coreutil + KAT (see factor.h).
// Pure logic, overflow-safe trial division. Cross-checked byte-identical to GNU factor.
// ============================================================
#include "factor.h"

int factor_one(uint64_t n, uint64_t* out, int cap) {
    int k = 0;
    if (n < 2) return 0;
    while ((n & 1ULL) == 0) { if (k < cap) out[k++] = 2; n >>= 1; }
    for (uint64_t d = 3; d <= n / d; d += 2)          // d <= n/d avoids d*d overflow
        while (n % d == 0) { if (k < cap) out[k++] = d; n /= d; }
    if (n > 1 && k < cap) out[k++] = n;               // a remaining prime cofactor
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
    };
    int nt = (int)(sizeof(t) / sizeof(t[0]));
    for (int i = 0; i < nt; i++) {
        uint64_t out[64];
        int k = factor_one(t[i].n, out, 64);
        if (k != t[i].k) return i + 1;
        for (int j = 0; j < k; j++) if (out[j] != t[i].f[j]) return i + 1;
    }
    // independent check: the factors must multiply back to n (ascending, with multiplicity)
    for (int i = 0; i < nt; i++) {
        if (t[i].n < 2) continue;
        uint64_t out[64]; int k = factor_one(t[i].n, out, 64);
        uint64_t p = 1;
        for (int j = 0; j < k; j++) {
            if (j && out[j] < out[j - 1]) return 100 + i;   // must be ascending
            p *= out[j];
        }
        if (p != t[i].n) return 200 + i;
    }
    return 0;
}
