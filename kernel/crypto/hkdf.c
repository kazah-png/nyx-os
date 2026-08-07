// ============================================================
// HKDF — HMAC-based Extract-and-Expand KDF (RFC 5869) over HMAC-SHA-256.
// ============================================================
// Two steps: Extract concentrates the entropy of an arbitrary input keying
// material (IKM) into a fixed-size pseudorandom key (PRK); Expand stretches the
// PRK, bound to an application-specific `info` label, into as much output keying
// material (OKM) as asked for. Built directly on the existing hmac_sha256().

#include "hkdf.h"
#include "sha256.h"

void hkdf_extract(const uint8_t* salt, uint32_t saltlen,
                  const uint8_t* ikm,  uint32_t ikmlen,
                  uint8_t prk[HKDF_HASHLEN]) {
    static const uint8_t zeros[HKDF_HASHLEN] = { 0 };
    if (salt == 0 || saltlen == 0) { salt = zeros; saltlen = HKDF_HASHLEN; }
    hmac_sha256(salt, saltlen, ikm, ikmlen, prk);       // PRK = HMAC(salt, IKM)
}

int hkdf_expand(const uint8_t prk[HKDF_HASHLEN],
                const uint8_t* info, uint32_t infolen,
                uint8_t* okm, uint32_t okmlen) {
    if (okmlen > 255u * HKDF_HASHLEN) return 1;          // RFC 5869 output limit
    if (infolen > HKDF_MAX_INFO)      return 1;          // working-buffer bound
    if (info == 0) infolen = 0;

    uint8_t  t[HKDF_HASHLEN];
    uint8_t  block[HKDF_HASHLEN + HKDF_MAX_INFO + 1];     // T(i-1) | info | counter
    uint32_t done = 0, tlen = 0;                          // tlen = 0 => T(0) empty
    uint8_t  counter = 0;

    while (done < okmlen) {
        counter++;
        uint32_t off = 0;
        memcpy(block, t, tlen);            off += tlen;   // T(i-1) (nothing on round 1)
        memcpy(block + off, info, infolen); off += infolen;
        block[off++] = counter;                          // i, as a single octet
        hmac_sha256(prk, HKDF_HASHLEN, block, off, t);
        tlen = HKDF_HASHLEN;

        uint32_t n = okmlen - done;
        if (n > HKDF_HASHLEN) n = HKDF_HASHLEN;
        memcpy(okm + done, t, n);
        done += n;
    }
    return 0;
}

int hkdf(const uint8_t* salt, uint32_t saltlen,
         const uint8_t* ikm,  uint32_t ikmlen,
         const uint8_t* info, uint32_t infolen,
         uint8_t* okm, uint32_t okmlen) {
    uint8_t prk[HKDF_HASHLEN];
    hkdf_extract(salt, saltlen, ikm, ikmlen, prk);
    return hkdf_expand(prk, info, infolen, okm, okmlen);
}

// ---- Known-answer test: RFC 5869 Appendix A, the three SHA-256 cases ----
static int hkdf_eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int hkdf_selftest(void) {
    uint8_t prk[HKDF_HASHLEN], okm[82];

    // A.1 — basic (13-byte salt, 10-byte info, L=42)
    // A2/A3 vectors below were emitted from a reference HKDF and match RFC 5869.
    static const uint8_t A1_PRK[32] = {
        0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf, 0x0d, 0xdc, 0x3f, 0x0d,
        0xc4, 0x7b, 0xba, 0x63, 0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31,
        0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5,
    };
    static const uint8_t A1_OKM[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64,
        0xd0, 0x36, 0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08,
        0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65,
    };
    {
        uint8_t ikm[22], salt[13], info[10];
        for (int i = 0; i < 22; i++) ikm[i]  = 0x0b;
        for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
        for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
        hkdf_extract(salt, 13, ikm, 22, prk);
        if (!hkdf_eq(prk, A1_PRK, 32)) return 1;
        if (hkdf(salt, 13, ikm, 22, info, 10, okm, 42)) return 2;
        if (!hkdf_eq(okm, A1_OKM, 42)) return 3;
    }

    // A.2 — longer inputs and output (80-byte salt/IKM/info, L=82)
    static const uint8_t A2_PRK[32] = {
        0x06, 0xa6, 0xb8, 0x8c, 0x58, 0x53, 0x36, 0x1a, 0x06, 0x10, 0x4c, 0x9c,
        0xeb, 0x35, 0xb4, 0x5c, 0xef, 0x76, 0x00, 0x14, 0x90, 0x46, 0x71, 0x01,
        0x4a, 0x19, 0x3f, 0x40, 0xc1, 0x5f, 0xc2, 0x44,
    };
    static const uint8_t A2_OKM[82] = {
        0xb1, 0x1e, 0x39, 0x8d, 0xc8, 0x03, 0x27, 0xa1, 0xc8, 0xe7, 0xf7, 0x8c,
        0x59, 0x6a, 0x49, 0x34, 0x4f, 0x01, 0x2e, 0xda, 0x2d, 0x4e, 0xfa, 0xd8,
        0xa0, 0x50, 0xcc, 0x4c, 0x19, 0xaf, 0xa9, 0x7c, 0x59, 0x04, 0x5a, 0x99,
        0xca, 0xc7, 0x82, 0x72, 0x71, 0xcb, 0x41, 0xc6, 0x5e, 0x59, 0x0e, 0x09,
        0xda, 0x32, 0x75, 0x60, 0x0c, 0x2f, 0x09, 0xb8, 0x36, 0x77, 0x93, 0xa9,
        0xac, 0xa3, 0xdb, 0x71, 0xcc, 0x30, 0xc5, 0x81, 0x79, 0xec, 0x3e, 0x87,
        0xc1, 0x4c, 0x01, 0xd5, 0xc1, 0xf3, 0x43, 0x4f, 0x1d, 0x87,
    };
    {
        uint8_t ikm[80], salt[80], info[80];
        for (int i = 0; i < 80; i++) ikm[i]  = (uint8_t)i;
        for (int i = 0; i < 80; i++) salt[i] = (uint8_t)(0x60 + i);
        for (int i = 0; i < 80; i++) info[i] = (uint8_t)(0xb0 + i);
        hkdf_extract(salt, 80, ikm, 80, prk);
        if (!hkdf_eq(prk, A2_PRK, 32)) return 4;
        if (hkdf(salt, 80, ikm, 80, info, 80, okm, 82)) return 5;
        if (!hkdf_eq(okm, A2_OKM, 82)) return 6;
    }

    // A.3 — zero-length salt and info (salt defaults to 32 zero bytes)
    static const uint8_t A3_PRK[32] = {
        0x19, 0xef, 0x24, 0xa3, 0x2c, 0x71, 0x7b, 0x16, 0x7f, 0x33, 0xa9, 0x1d,
        0x6f, 0x64, 0x8b, 0xdf, 0x96, 0x59, 0x67, 0x76, 0xaf, 0xdb, 0x63, 0x77,
        0xac, 0x43, 0x4c, 0x1c, 0x29, 0x3c, 0xcb, 0x04,
    };
    static const uint8_t A3_OKM[42] = {
        0x8d, 0xa4, 0xe7, 0x75, 0xa5, 0x63, 0xc1, 0x8f, 0x71, 0x5f, 0x80, 0x2a,
        0x06, 0x3c, 0x5a, 0x31, 0xb8, 0xa1, 0x1f, 0x5c, 0x5e, 0xe1, 0x87, 0x9e,
        0xc3, 0x45, 0x4e, 0x5f, 0x3c, 0x73, 0x8d, 0x2d, 0x9d, 0x20, 0x13, 0x95,
        0xfa, 0xa4, 0xb6, 0x1a, 0x96, 0xc8,
    };
    {
        uint8_t ikm[22];
        for (int i = 0; i < 22; i++) ikm[i] = 0x0b;
        hkdf_extract(0, 0, ikm, 22, prk);
        if (!hkdf_eq(prk, A3_PRK, 32)) return 7;
        if (hkdf(0, 0, ikm, 22, 0, 0, okm, 42)) return 8;
        if (!hkdf_eq(okm, A3_OKM, 42)) return 9;
    }

    // Sensitivity: the info label and the salt must each change the OKM — guards
    // against a stub that ignores an argument.
    {
        uint8_t ikm[22], a[32], b[32];
        for (int i = 0; i < 22; i++) ikm[i] = 0x0b;
        hkdf(0, 0, ikm, 22, (const uint8_t*)"A", 1, a, 32);
        hkdf(0, 0, ikm, 22, (const uint8_t*)"B", 1, b, 32);
        if (hkdf_eq(a, b, 32)) return 10;                 // info matters
        hkdf((const uint8_t*)"s1", 2, ikm, 22, (const uint8_t*)"A", 1, a, 32);
        hkdf((const uint8_t*)"s2", 2, ikm, 22, (const uint8_t*)"A", 1, b, 32);
        if (hkdf_eq(a, b, 32)) return 11;                 // salt matters
    }
    return 0;
}
