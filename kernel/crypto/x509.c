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
#include "../core/kernel.h"
#include "x509.h"
#include "der.h"
#include "p256.h"
#include "p384.h"
#include "rsa.h"
#include "sha256.h"
#include "sha512.h"
#include "../drivers/misc/rtc.h"

// NyxOS's trust store: the public keys of a small bundle of root/anchor CAs (x509_roots.h). A
// chain is trusted only when its topmost certificate carries one of these keys — the key *is* the
// identity of a trust anchor, so it needs no signature check itself. (Key-pinned; a broader bundle
// is more work but the machinery is the same. Captured and cross-checked 2026-07-26.)
#include "x509_roots.h"

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

// Is a parsed public key one of the bundled trust anchors? Returns the root's name, or NULL.
static const char* trusted_root_name(const der_pubkey_t* k) {
    for (uint32_t i = 0; i < NYX_TRUSTED_ROOTS_N; i++) {
        const nyx_root_t* r = &NYX_TRUSTED_ROOTS[i];
        if (k->type != r->type) continue;
        if (r->type == DER_KEY_EC) {
            if (k->ec_point_len == r->ec_len && mem_eq(k->ec_point, r->ec_point, r->ec_len)) return r->name;
        } else {                                             // RSA: compare N and e (leading zeros stripped)
            const uint8_t* kn = k->rsa_n; uint32_t knl = k->rsa_n_len;
            while (knl > 1 && kn[0] == 0x00) { kn++; knl--; }
            const uint8_t* ke = k->rsa_e; uint32_t kel = k->rsa_e_len;
            while (kel > 1 && ke[0] == 0x00) { ke++; kel--; }
            if (knl == r->rsa_n_len && kel == r->rsa_e_len &&
                mem_eq(kn, r->rsa_n, knl) && mem_eq(ke, r->rsa_e, kel)) return r->name;
        }
    }
    return 0;
}

// Does this certificate assert basicConstraints cA = TRUE — i.e. is it permitted to sign other
// certificates? An end-entity (leaf) certificate sets cA = FALSE or omits basicConstraints, so it
// must NEVER be accepted as an issuer: otherwise a valid leaf could be turned into a signing CA to
// forge certificates for arbitrary names (the classic basicConstraints bypass, CVE-2002-0862).
static int cert_is_ca(const uint8_t* cert, uint32_t clen) {
    static const uint8_t OID_BC[] = { 0x55, 0x1d, 0x13 };   // 2.5.29.19 basicConstraints
    const uint8_t* ext; uint32_t extl;
    if (der_x509_extension(cert, clen, OID_BC, sizeof(OID_BC), &ext, &extl) != 0)
        return 0;                                           // no basicConstraints => not a CA
    der_t wrap = { ext, ext + extl }, bc;
    if (der_enter(&wrap, 0x30, &bc) != 0) return 0;         // BasicConstraints ::= SEQUENCE
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&bc, &t, &v, &vl) != 0) return 0;          // empty SEQUENCE => cA defaults FALSE
    if (t != 0x01) return 0;                                // first element isn't the cA BOOLEAN => FALSE
    return (vl >= 1 && v[0] != 0x00) ? 1 : 0;               // cA TRUE only if the BOOLEAN byte is non-zero
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
        // The signing certificate must be a CA (basicConstraints cA = TRUE). Without this a valid
        // end-entity certificate could sign forged certificates for other names and still chain to
        // the pinned root — the classic basicConstraints bypass. Reject such a chain.
        if (!cert_is_ca(certs[i + 1], lens[i + 1])) {
            set_msg(msg, msgcap, "an issuer certificate is not a CA (basicConstraints)");
            return X509_FORGED;
        }
        int r = verify_signed(certs[i], lens[i], &issuer);
        if (r == X509_FORGED) { set_msg(msg, msgcap, "a certificate signature is invalid"); return X509_FORGED; }
        if (r == X509_INCOMPLETE) incomplete = 1;
    }

    // Anchor: the topmost certificate must carry a public key from the bundled trust store.
    der_pubkey_t top;
    if (der_x509_pubkey(certs[n - 1], lens[n - 1], &top) != 0) {
        set_msg(msg, msgcap, "cannot parse the top certificate's key");
        return X509_INCOMPLETE;
    }
    const char* root = trusted_root_name(&top);
    if (root) {
        if (incomplete) {
            set_msg(msg, msgcap, "anchored to a trusted root, but a link used an unsupported algorithm");
            return X509_INCOMPLETE;
        }
        snprintf(msg, (uint32_t)msgcap, "anchored to trusted root: %s", root);
        return X509_OK;
    }
    set_msg(msg, msgcap, "top certificate is not a trusted root");
    return X509_INCOMPLETE;
}

// ---- leaf checks: hostname (subjectAltName) and validity dates (v5.9.66) ------------

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Match a presented dNSName pattern (length patlen, not NUL-terminated) against `host`,
// case-insensitively, honouring a single leading "*." wildcard that covers exactly one label.
static int host_match(const char* pat, uint32_t patlen, const char* host) {
    uint32_t hlen = 0; while (host[hlen]) hlen++;
    if (patlen >= 2 && pat[0] == '*' && pat[1] == '.') {
        uint32_t dot = 0; while (dot < hlen && host[dot] != '.') dot++;
        if (dot == 0 || dot >= hlen) return 0;             // need a non-empty first label + a dot
        const char* prest = pat + 2;  uint32_t prestlen = patlen - 2;
        const char* hrest = host + dot + 1; uint32_t hrestlen = hlen - dot - 1;
        if (prestlen != hrestlen) return 0;                // remaining labels must match exactly
        for (uint32_t i = 0; i < prestlen; i++) if (lower(prest[i]) != lower(hrest[i])) return 0;
        return 1;
    }
    if (patlen != hlen) return 0;
    for (uint32_t i = 0; i < hlen; i++) if (lower(pat[i]) != lower(host[i])) return 0;
    return 1;
}

int x509_check_host(const uint8_t* leaf, uint32_t leaf_len, const char* host) {
    static const uint8_t OID_SAN[] = { 0x55, 0x1d, 0x11 };  // 2.5.29.17 subjectAltName
    const uint8_t* ext; uint32_t extl;
    if (der_x509_extension(leaf, leaf_len, OID_SAN, sizeof(OID_SAN), &ext, &extl) != 0) return -1;
    der_t wrap = { ext, ext + extl }, san;
    if (der_enter(&wrap, 0x30, &san) != 0) return -1;      // SubjectAltName ::= SEQUENCE OF GeneralName
    int saw_dns = 0;
    while (san.p < san.end) {
        uint8_t t; const uint8_t* v; uint32_t vl;
        if (der_read(&san, &t, &v, &vl) != 0) break;
        if (t == 0x82) {                                   // dNSName [2] IMPLICIT IA5String
            saw_dns = 1;
            if (host_match((const char*)v, vl, host)) return 0;
        }
    }
    return saw_dns ? 1 : -1;
}

// Pure: is packed-decimal `now` (YYYYMMDDHHMMSS) inside the INCLUSIVE validity window [nb, na]?
// 0 = valid, 1 = not yet valid (now < notBefore), 2 = expired (now > notAfter). Both bounds are
// inclusive per RFC 5280 §4.1.2.5 (a cert is valid AT its notBefore and notAfter seconds). Split
// out from x509_check_validity so the accept/reject verdict is testable without the hardware RTC.
int x509_time_in_window(uint64_t now, uint64_t nb, uint64_t na) {
    if (now < nb) return 1;                                // not yet valid
    if (now > na) return 2;                                // expired
    return 0;                                              // within the validity window
}

int x509_check_validity(const uint8_t* cert, uint32_t clen) {
    uint64_t nb, na;
    if (der_x509_validity(cert, clen, &nb, &na) != 0) return -1;
    rtc_time_t t; rtc_read_time(&t);
    uint64_t now = ((((( (uint64_t)t.year *100 + t.month)*100 + t.day)*100 + t.hour)*100 + t.minute)*100 + t.second);
    return x509_time_in_window(now, nb, na);
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
        // Copy the leaf so a byte can be flipped. The buffer was sized (1003) to the CURRENT
        // pinned example.com leaf and the copy was unbounded — but the pinned chain is
        // re-captured periodically (certs rotate ~quarterly), and the next capture with a
        // larger leaf would silently overflow this static buffer. Size it generously and
        // bound the copy so no future test cert can overrun it; guard the flip index too.
        static uint8_t bad_leaf[2048];
        uint32_t l0 = TESTCHAIN_LEN[0];
        if (l0 > sizeof(bad_leaf) || l0 <= 400) {
            printf("x509: tampered-leaf test can't run (leaf %u B vs %u B buffer) FAIL\n",
                   (unsigned)l0, (unsigned)sizeof(bad_leaf));
        } else {
            for (uint32_t i = 0; i < l0; i++) bad_leaf[i] = TESTCHAIN_0[i];
            bad_leaf[400] ^= 0x01;                           // a byte well inside the tbsCertificate
            const uint8_t* cc[4] = { bad_leaf, TESTCHAIN_1, TESTCHAIN_2, TESTCHAIN_3 };
            int r = x509_verify_chain(cc, TESTCHAIN_LEN, 4, msg, sizeof(msg));
            if (r == X509_FORGED) { pass++; printf("x509: tampered leaf -> REJECTED (%s) PASS\n", msg); }
            else                    printf("x509: tampered leaf NOT rejected (code %d) FAIL\n", r);
        }
    }

    // 3) A chain that stops before the root (leaf..transit): links verify, but the top is not the
    //    pinned root, so it must NOT be trusted.
    total++;
    {
        int r = x509_verify_chain(TESTCHAIN, TESTCHAIN_LEN, 3, msg, sizeof(msg));
        if (r == X509_INCOMPLETE) { pass++; printf("x509: chain without root -> NOT anchored (%s) PASS\n", msg); }
        else                        printf("x509: root-less chain wrongly returned %d FAIL\n", r);
    }

    // 4) Hostname (subjectAltName) matching on the real leaf (SAN: example.com, *.example.com).
    total++;
    {
        const uint8_t* L = TESTCHAIN_0; uint32_t Ln = TESTCHAIN_LEN[0];
        int ok = (x509_check_host(L, Ln, "example.com")     == 0) &&   // exact
                 (x509_check_host(L, Ln, "www.example.com")  == 0) &&   // wildcard *.example.com
                 (x509_check_host(L, Ln, "attacker.com")     == 1) &&   // different domain
                 (x509_check_host(L, Ln, "example.org")      == 1) &&   // different TLD
                 (x509_check_host(L, Ln, "a.b.example.com")  == 1);     // wildcard is one label only
        if (ok) { pass++; printf("x509: hostname (SAN) match PASS (example.com + *.example.com)\n"); }
        else      printf("x509: hostname (SAN) match FAIL\n");
    }

    // 5) Validity-date parsing on the real leaf (notBefore 2026-05-31, notAfter 2026-08-29).
    total++;
    {
        uint64_t nb = 0, na = 0;
        int ok = (der_x509_validity(TESTCHAIN_0, TESTCHAIN_LEN[0], &nb, &na) == 0) &&
                 nb == 20260531213912ULL && na == 20260829214126ULL;
        if (ok) { pass++; printf("x509: validity dates parsed PASS (nb=%llu na=%llu)\n",
                                 (unsigned long long)nb, (unsigned long long)na); }
        else      printf("x509: validity dates parse FAIL (nb=%llu na=%llu)\n",
                         (unsigned long long)nb, (unsigned long long)na);
    }

    // 6) basicConstraints CA check: the intermediates and root ARE CAs, the leaf is NOT. This is
    //    what stops a valid leaf from being used to sign forged certificates (issue #49).
    total++;
    {
        int leaf = cert_is_ca(TESTCHAIN_0, TESTCHAIN_LEN[0]);
        int mid  = cert_is_ca(TESTCHAIN_1, TESTCHAIN_LEN[1]);
        int root = cert_is_ca(TESTCHAIN_3, TESTCHAIN_LEN[3]);
        if (leaf == 0 && mid == 1 && root == 1) {
            pass++; printf("x509: basicConstraints CA check PASS (leaf!=CA, issuers=CA)\n");
        } else {
            printf("x509: basicConstraints CA check FAIL (leaf=%d mid=%d root=%d)\n", leaf, mid, root);
        }
    }

    // 7) Validity-window accept/reject verdict (x509_time_in_window) — the in-date decision, split
    //    out from the RTC read so its boundaries are pinned. Bounds are INCLUSIVE (RFC 5280): a cert
    //    is valid AT the exact notBefore and notAfter seconds, one second outside is not.
    total++;
    {
        const uint64_t nb = 20260531213912ULL, na = 20260829214126ULL;   // the real leaf's window
        int ok = x509_time_in_window(20260715000000ULL, nb, na) == 0 &&   // mid-window     -> valid
                 x509_time_in_window(nb,                 nb, na) == 0 &&   // exactly nb     -> valid (inclusive)
                 x509_time_in_window(na,                 nb, na) == 0 &&   // exactly na     -> valid (inclusive)
                 x509_time_in_window(nb - 1,             nb, na) == 1 &&   // one second early -> not yet valid
                 x509_time_in_window(na + 1,             nb, na) == 2;     // one second late  -> expired
        if (ok) { pass++; printf("x509: validity-window verdict PASS (inclusive bounds)\n"); }
        else      printf("x509: validity-window verdict FAIL\n");
    }

    printf("x509: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
