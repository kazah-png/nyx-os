// ============================================================
// ct.c - constant-time memory comparison (side-channel hardening)
// ============================================================
// See ct.h. The one rule: touch every byte, branch on none of them.
#include "ct.h"

int ct_memcmp(const void* a, const void* b, uint32_t n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    uint8_t diff = 0;
    for (uint32_t i = 0; i < n; i++) diff |= (uint8_t)(x[i] ^ y[i]);
    return diff;                          // 0 iff every one of the n bytes matched
}

// ---- known-answer self-test (`cttest`) ----
int ct_selftest(void) {
    uint8_t a[16], b[16];
    for (int i = 0; i < 16; i++) a[i] = b[i] = (uint8_t)(i * 7 + 1);
    int pass = 0, total = 0;

    total++; if (ct_memcmp(a, b, 16) == 0) { pass++; printf("ct: equal 16 bytes -> 0 PASS\n"); }
             else printf("ct: equal FAIL\n");

    // A difference isolated at the first / a middle / the last byte must all report
    // non-equal — the last-byte case is exactly what a broken early-return compare
    // would race through, so it pins down that the whole buffer is folded in.
    total++; { uint8_t c[16]; for (int i = 0; i < 16; i++) c[i] = b[i]; c[0] ^= 1;
        if (ct_memcmp(a, c, 16) != 0) { pass++; printf("ct: first-byte diff -> !=0 PASS\n"); }
        else printf("ct: first-byte diff FAIL\n"); }

    total++; { uint8_t c[16]; for (int i = 0; i < 16; i++) c[i] = b[i]; c[7] ^= 0xFF;
        if (ct_memcmp(a, c, 16) != 0) { pass++; printf("ct: middle-byte diff -> !=0 PASS\n"); }
        else printf("ct: middle-byte diff FAIL\n"); }

    total++; { uint8_t c[16]; for (int i = 0; i < 16; i++) c[i] = b[i]; c[15] ^= 0x80;
        if (ct_memcmp(a, c, 16) != 0) { pass++; printf("ct: last-byte diff -> !=0 PASS\n"); }
        else printf("ct: last-byte diff FAIL\n"); }

    // n == 0: nothing to compare, so equal (vacuously).
    total++; if (ct_memcmp(a, b, 0) == 0) { pass++; printf("ct: zero length -> 0 PASS\n"); }
             else printf("ct: zero length FAIL\n");

    // Every byte differing is still just one non-zero result.
    total++; { uint8_t z[16], f[16]; for (int i = 0; i < 16; i++) { z[i] = 0x00; f[i] = 0xFF; }
        if (ct_memcmp(z, f, 16) != 0) { pass++; printf("ct: all-bytes diff -> !=0 PASS\n"); }
        else printf("ct: all-bytes diff FAIL\n"); }

    // Only the first n bytes count: a matching prefix compares equal even though the
    // buffers differ past n.
    total++; { uint8_t p[16]; for (int i = 0; i < 16; i++) p[i] = a[i]; p[8] ^= 0x55;
        if (ct_memcmp(a, p, 8) == 0) { pass++; printf("ct: matching prefix (n<len) -> 0 PASS\n"); }
        else printf("ct: matching prefix FAIL\n"); }

    printf("ct: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
