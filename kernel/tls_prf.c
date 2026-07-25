// ============================================================
// tls_prf.c - TLS 1.2 pseudo-random function (P_SHA256), v5.9.50
// ============================================================
// The second crypto step of the https arc. TLS derives every key it uses — the
// master secret from the ECDHE pre-master secret, then the AES keys and IVs from the
// master secret — by running the RFC 5246 §5 PRF. For the modern cipher suites (and
// the 0xc02b that example.com negotiated) that PRF is P_SHA256:
//
//   P_SHA256(secret, seed) = HMAC(secret, A(1)+seed) || HMAC(secret, A(2)+seed) || ...
//   A(0) = seed ;  A(i) = HMAC(secret, A(i-1))
//   PRF(secret, label, seed) = P_SHA256(secret, label || seed)
//
// HMAC-SHA256 already exists in the kernel (sha256.c, used by the password KDF), so
// this is a thin, exact layer on top of it. It is a standalone primitive here: the
// handshake wiring (compute the pre-master secret, then call this to make the master
// secret and key block) is the next increment. Correctness is pinned by the RFC 4231
// HMAC vectors and the canonical TLS-1.2-PRF-SHA256 test vector, via `prftest`.
#include "kernel.h"
#include "sha256.h"
#include "tls_prf.h"

void tls12_prf(const uint8_t* secret, uint32_t seclen,
               const char* label,
               const uint8_t* seed, uint32_t seedlen,
               uint8_t* out, uint32_t outlen) {
    uint32_t lablen = (uint32_t)strlen(label);

    // labelseed = label || seed  (the P_hash "seed"). Labels are short and the TLS
    // seed is two 32-byte randoms, so 160 bytes is comfortably enough.
    uint8_t labelseed[160];
    if (lablen + seedlen > sizeof(labelseed)) return;   // guard; never true for TLS uses
    memcpy(labelseed, label, lablen);
    memcpy(labelseed + lablen, seed, seedlen);
    uint32_t lslen = lablen + seedlen;

    uint8_t a[SHA256_DIGEST_SIZE];                       // A(i)
    hmac_sha256(secret, seclen, labelseed, lslen, a);    // A(1) = HMAC(secret, seed)

    uint8_t buf[SHA256_DIGEST_SIZE + 160];               // A(i) || labelseed
    uint8_t block[SHA256_DIGEST_SIZE];
    uint32_t done = 0;
    while (done < outlen) {
        memcpy(buf, a, SHA256_DIGEST_SIZE);
        memcpy(buf + SHA256_DIGEST_SIZE, labelseed, lslen);
        hmac_sha256(secret, seclen, buf, SHA256_DIGEST_SIZE + lslen, block);  // HMAC(secret, A(i)+seed)

        uint32_t n = outlen - done;
        if (n > SHA256_DIGEST_SIZE) n = SHA256_DIGEST_SIZE;
        memcpy(out + done, block, n);
        done += n;

        hmac_sha256(secret, seclen, a, SHA256_DIGEST_SIZE, a);   // A(i+1) = HMAC(secret, A(i))
    }
}

// ---- known-answer self-test ---------------------------------------------------------

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void hexbytes(uint8_t* out, const char* h, int n) {
    for (int i = 0; i < n; i++) out[i] = (uint8_t)((hexval(h[2 * i]) << 4) | hexval(h[2 * i + 1]));
}
static int eqbytes(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int tls_prf_selftest(void) {
    int pass = 0, total = 0;
    uint8_t got[128], want[128];

    // HMAC-SHA256, RFC 4231 Test Case 1: key = 20 x 0x0b, data = "Hi There".
    uint8_t k1[20];
    for (int i = 0; i < 20; i++) k1[i] = 0x0b;
    hmac_sha256(k1, 20, (const uint8_t*)"Hi There", 8, got);
    hexbytes(want, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
    total++;
    if (eqbytes(got, want, 32)) { pass++; printf("prf: HMAC-SHA256 RFC4231 TC1 PASS\n"); }
    else                          printf("prf: HMAC-SHA256 RFC4231 TC1 FAIL\n");

    // HMAC-SHA256, RFC 4231 Test Case 2: key = "Jefe", data = "what do ya want for nothing?".
    hmac_sha256((const uint8_t*)"Jefe", 4,
                (const uint8_t*)"what do ya want for nothing?", 28, got);
    hexbytes(want, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);
    total++;
    if (eqbytes(got, want, 32)) { pass++; printf("prf: HMAC-SHA256 RFC4231 TC2 PASS\n"); }
    else                          printf("prf: HMAC-SHA256 RFC4231 TC2 FAIL\n");

    // TLS 1.2 PRF (P_SHA256), the canonical mbedTLS/OpenSSL test vector:
    // secret + "test label" + seed -> 100 bytes.
    uint8_t secret[16], seed[16];
    hexbytes(secret, "9bbe436ba940f017b17652849a71db35", 16);
    hexbytes(seed,   "a0ba9f936cda311827a6f796ffd5198c", 16);
    tls12_prf(secret, 16, "test label", seed, 16, got, 100);
    hexbytes(want,
        "e3f229ba727be17b8d122620557cd453c2aab21d07c3d495329b52d4e61edb5a"
        "6b301791e90d35c9c9a46b4e14baf9af0fa022f7077def17abfd3797c0564bab"
        "4fbc91666e9def9b97fce34f796789baa48082d122ee42c5a72e5a5110fff701"
        "87347b66", 100);
    total++;
    if (eqbytes(got, want, 100)) { pass++; printf("prf: TLS 1.2 P_SHA256 (100 bytes) PASS\n"); }
    else                           printf("prf: TLS 1.2 P_SHA256 (100 bytes) FAIL\n");

    printf("prf: self-test %d/%d vectors passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
