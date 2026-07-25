// ============================================================
// x509.c - X.509 certificate-chain verification to a pinned root, v5.9.65
// ============================================================
// The finale of the TLS trust model. Everything before this proved the server holds the private
// key matching its leaf certificate (the ServerKeyExchange signature, v5.9.61) — but NOT that the
// certificate is trustworthy. A man-in-the-middle can present its own perfectly self-consistent
// certificate. This closes that gap: it walks the certificate chain the server sent, verifying
// each certificate's signature under the next one's public key (dispatching by the
// signatureAlgorithm OID to ECDSA-P256/SHA-256, ECDSA-P384/SHA-384, or RSA-PKCS1/SHA-256), and
// requires the topmost certificate to carry a *pinned* trusted-root public key compiled into the
// kernel. A broken link means tampering and is rejected; only a chain that reaches the pinned
// anchor is fully trusted. Pinned by `chaintest` against a real captured chain (accept), a
// tampered copy (reject), and a root-less prefix (not anchored).
#include "kernel.h"
#include "x509.h"
#include "der.h"
#include "p256.h"
#include "p384.h"
#include "rsa.h"
#include "sha256.h"
#include "sha512.h"

// The trust anchor NyxOS pins: the SSL.com TLS ECC Root CA 2022 public key (NIST P-384, the
// uncompressed EC point 0x04||X||Y). A chain is trusted only when its topmost certificate carries
// exactly this key — the key *is* the identity of a root, so it needs no signature check itself.
// (This root anchors example.com and other SSL.com-issued sites; a broader trust store is future
// work. Captured and cross-checked 2026-07-26.)
static const uint8_t PINNED_ROOT_POINT[97] = {
    0x04,0x45,0x29,0x35,0x73,0xfa,0xc2,0xb8,0x23,0xce,0x14,0x7d,
    0xa8,0xb1,0x4d,0xa0,0x5b,0x36,0xee,0x2a,0x2c,0x53,0xc3,0x60,
    0x09,0x35,0xb2,0x24,0x66,0x26,0x69,0xc0,0xb3,0x95,0xd6,0x5d,
    0x92,0x40,0x19,0x0e,0xc6,0xa5,0x13,0x70,0xf4,0xef,0x12,0x51,
    0x28,0x5d,0xe7,0xcc,0xbd,0xf9,0x3c,0x85,0xc1,0xcf,0x94,0x90,
    0xc9,0x2b,0xce,0x92,0x42,0x58,0x59,0x67,0xfd,0x94,0x27,0x10,
    0x64,0x8c,0x4f,0x04,0xb1,0x4d,0x49,0xe4,0x7b,0x4f,0x9b,0xf5,
    0xe7,0x08,0xf8,0x03,0x88,0xf7,0xa7,0xc3,0x92,0x4b,0x19,0x54,
    0x81,
};

// signatureAlgorithm OIDs we can verify (value bytes only).
static const uint8_t OID_ECDSA_SHA256[] = { 0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02 };
static const uint8_t OID_ECDSA_SHA384[] = { 0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x03 };
static const uint8_t OID_RSA_SHA256[]   = { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b };

static int mem_eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static int oid_is(const uint8_t* v, uint32_t vl, const uint8_t* w, uint32_t wl) {
    return vl == wl && mem_eq(v, w, vl);
}
static void set_msg(char* dst, int cap, const char* src) {
    if (!dst || cap <= 0) return;
    int i = 0;
    for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}
// Right-justify a DER INTEGER into a fixed big-endian field (drop leading sign/pad bytes, then
// left-pad with zeros). Matches the r/s and hash conventions of the ECDSA verifiers.
static void int_to_fixed(uint8_t* out, int outlen, const uint8_t* v, uint32_t len) {
    while (len > (uint32_t)outlen) { v++; len--; }
    for (int i = 0; i < outlen; i++) out[i] = 0;
    for (uint32_t i = 0; i < len; i++) out[outlen - (int)len + (int)i] = v[i];
}

// Parse a DER ECDSA-Sig-Value SEQUENCE { INTEGER r, INTEGER s } into fixed-width r,s. 0 on ok.
static int parse_ecdsa_sig(const uint8_t* sig, uint32_t sigl, uint8_t* r, uint8_t* s, int flen) {
    der_t sd = { sig, sig + sigl }, sq;
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_enter(&sd, 0x30, &sq) != 0) return -1;
    if (der_read(&sq, &t, &v, &vl) != 0 || t != 0x02) return -1;
    int_to_fixed(r, flen, v, vl);
    if (der_read(&sq, &t, &v, &vl) != 0 || t != 0x02) return -1;
    int_to_fixed(s, flen, v, vl);
    return 0;
}

// Verify one certificate's signature under an already-parsed issuer public key. Returns X509_OK,
// X509_FORGED (a supported algorithm that failed), or X509_INCOMPLETE (unsupported alg / key
// mismatch / parse error — signature not checkable, so trust is not claimed).
static int verify_signed(const uint8_t* cert, uint32_t clen, const der_pubkey_t* issuer) {
    const uint8_t *oid, *tbs, *sig; uint32_t oidl, tbsl, sigl;
    if (der_x509_sig_alg(cert, clen, &oid, &oidl) != 0) return X509_INCOMPLETE;
    if (der_x509_tbs(cert, clen, &tbs, &tbsl) != 0) return X509_INCOMPLETE;
    if (der_x509_signature(cert, clen, &sig, &sigl) != 0) return X509_INCOMPLETE;

    if (oid_is(oid, oidl, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)) &&
        issuer->type == DER_KEY_EC && issuer->curve == DER_CURVE_P256) {
        if (issuer->ec_point_len != 65 || issuer->ec_point[0] != 0x04) return X509_INCOMPLETE;
        uint8_t e[32]; sha256_ctx_t hc; sha256_init(&hc); sha256_update(&hc, tbs, tbsl); sha256_final(&hc, e);
        uint8_t r[32], s[32];
        if (parse_ecdsa_sig(sig, sigl, r, s, 32) != 0) return X509_INCOMPLETE;
        return (p256_ecdsa_verify(issuer->ec_point + 1, issuer->ec_point + 33, e, r, s) == 0)
               ? X509_OK : X509_FORGED;
    }
    if (oid_is(oid, oidl, OID_ECDSA_SHA384, sizeof(OID_ECDSA_SHA384)) &&
        issuer->type == DER_KEY_EC && issuer->curve == DER_CURVE_P384) {
        if (issuer->ec_point_len != 97 || issuer->ec_point[0] != 0x04) return X509_INCOMPLETE;
        uint8_t e[48]; sha384(tbs, tbsl, e);
        uint8_t r[48], s[48];
        if (parse_ecdsa_sig(sig, sigl, r, s, 48) != 0) return X509_INCOMPLETE;
        return (p384_ecdsa_verify(issuer->ec_point + 1, issuer->ec_point + 49, e, r, s) == 0)
               ? X509_OK : X509_FORGED;
    }
    if (oid_is(oid, oidl, OID_RSA_SHA256, sizeof(OID_RSA_SHA256)) && issuer->type == DER_KEY_RSA) {
        uint8_t e[32]; sha256_ctx_t hc; sha256_init(&hc); sha256_update(&hc, tbs, tbsl); sha256_final(&hc, e);
        const uint8_t* N = issuer->rsa_n; uint32_t Nl = issuer->rsa_n_len;
        while (Nl > 1 && N[0] == 0x00) { N++; Nl--; }        // strip DER INTEGER leading zero
        uint64_t ev = 0;
        for (uint32_t i = 0; i < issuer->rsa_e_len && i < 8; i++) ev = (ev << 8) | issuer->rsa_e[i];
        return (rsa_pkcs1_sha256_verify(N, Nl, ev, sig, sigl, e) == 0) ? X509_OK : X509_FORGED;
    }
    return X509_INCOMPLETE;                                   // unsupported algorithm / key mismatch
}

int x509_verify_chain(const uint8_t* const* certs, const uint32_t* lens, int n,
                      char* msg, int msgcap) {
    if (n < 1) { set_msg(msg, msgcap, "empty chain"); return X509_INCOMPLETE; }

    int incomplete = 0;                                      // a link we could not check (unsupported)
    for (int i = 0; i + 1 < n; i++) {
        der_pubkey_t issuer;
        if (der_x509_pubkey(certs[i + 1], lens[i + 1], &issuer) != 0) {
            set_msg(msg, msgcap, "cannot parse an issuer public key");
            return X509_INCOMPLETE;
        }
        int r = verify_signed(certs[i], lens[i], &issuer);
        if (r == X509_FORGED) { set_msg(msg, msgcap, "a certificate signature is invalid"); return X509_FORGED; }
        if (r == X509_INCOMPLETE) incomplete = 1;
    }

    // Anchor: the topmost certificate must carry a pinned trusted-root public key.
    der_pubkey_t top;
    if (der_x509_pubkey(certs[n - 1], lens[n - 1], &top) != 0) {
        set_msg(msg, msgcap, "cannot parse the top certificate's key");
        return X509_INCOMPLETE;
    }
    if (top.type == DER_KEY_EC && top.ec_point_len == 97 && mem_eq(top.ec_point, PINNED_ROOT_POINT, 97)) {
        if (incomplete) {
            set_msg(msg, msgcap, "anchored to pinned root, but a link used an unsupported algorithm");
            return X509_INCOMPLETE;
        }
        set_msg(msg, msgcap, "anchored to the pinned SSL.com TLS ECC Root CA 2022");
        return X509_OK;
    }
    set_msg(msg, msgcap, "top certificate is not a pinned trusted root");
    return X509_INCOMPLETE;
}

// ---- known-answer self-test ----------------------------------------------------------
#include "x509_testchain.h"

int x509_selftest(void) {
    int pass = 0, total = 0;
    char msg[96];

    // 1) The real captured chain must verify and anchor to the pinned root.
    total++;
    {
        int r = x509_verify_chain(TESTCHAIN, TESTCHAIN_LEN, 4, msg, sizeof(msg));
        if (r == X509_OK) { pass++; printf("x509: real 4-cert chain -> TRUSTED (%s) PASS\n", msg); }
        else               printf("x509: real chain FAIL (code %d: %s)\n", r, msg);
    }

    // 2) Flip one byte inside the leaf's tbsCertificate: the leaf link must be detected as forged.
    total++;
    {
        static uint8_t bad_leaf[1003];
        for (int i = 0; i < (int)TESTCHAIN_LEN[0]; i++) bad_leaf[i] = TESTCHAIN_0[i];
        bad_leaf[400] ^= 0x01;                               // a byte well inside the tbsCertificate
        const uint8_t* cc[4] = { bad_leaf, TESTCHAIN_1, TESTCHAIN_2, TESTCHAIN_3 };
        int r = x509_verify_chain(cc, TESTCHAIN_LEN, 4, msg, sizeof(msg));
        if (r == X509_FORGED) { pass++; printf("x509: tampered leaf -> REJECTED (%s) PASS\n", msg); }
        else                    printf("x509: tampered leaf NOT rejected (code %d) FAIL\n", r);
    }

    // 3) A chain that stops before the root (leaf..transit): links verify, but the top is not the
    //    pinned root, so it must NOT be trusted.
    total++;
    {
        int r = x509_verify_chain(TESTCHAIN, TESTCHAIN_LEN, 3, msg, sizeof(msg));
        if (r == X509_INCOMPLETE) { pass++; printf("x509: chain without root -> NOT anchored (%s) PASS\n", msg); }
        else                        printf("x509: root-less chain wrongly returned %d FAIL\n", r);
    }

    printf("x509: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
