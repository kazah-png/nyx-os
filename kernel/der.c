// ============================================================
// der.c - DER/ASN.1 reader for X.509, v5.9.58
// ============================================================
// First step of the TLS trust model: to verify a server we must read its X.509 certificate,
// which is DER-encoded (nested Tag-Length-Value). This is a small, bounds-checked DER cursor
// plus one navigator, `der_x509_ec_pubkey`, that walks Certificate -> tbsCertificate ->
// subjectPublicKeyInfo -> subjectPublicKey and hands back the server's EC public-key point.
// Later increments add P-256/ECDSA to verify the ServerKeyExchange signature under that key.
// Pinned by `dertest`: TLV parsing, long-form lengths, and extraction from a real P-256 cert.
#include "kernel.h"
#include "der.h"

int der_read(der_t* c, uint8_t* tag, const uint8_t** val, uint32_t* vlen) {
    if (c->p >= c->end) return -1;
    *tag = *c->p++;
    if (c->p >= c->end) return -1;
    uint8_t l0 = *c->p++;
    uint32_t len;
    if (l0 < 0x80) {
        len = l0;                                   // short form
    } else {
        int nb = l0 & 0x7f;                         // long form: nb length bytes follow
        if (nb == 0 || nb > 4) return -1;           // reject indefinite length and >4 GB
        len = 0;
        for (int i = 0; i < nb; i++) {
            if (c->p >= c->end) return -1;
            len = (len << 8) | *c->p++;
        }
    }
    if ((uint32_t)(c->end - c->p) < len) return -1;
    *val = c->p; *vlen = len;
    c->p += len;
    return 0;
}

int der_enter(der_t* c, uint8_t expect, der_t* inner) {
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(c, &t, &v, &vl) != 0) return -1;
    if (t != expect) return -1;
    inner->p = v; inner->end = v + vl;
    return 0;
}

int der_skip(der_t* c) {
    uint8_t t; const uint8_t* v; uint32_t vl;
    return der_read(c, &t, &v, &vl);
}

int der_x509_ec_pubkey(const uint8_t* cert, uint32_t clen, const uint8_t** point, uint32_t* plen) {
    der_t top = { cert, cert + clen }, certseq, tbs, spki;
    if (der_enter(&top, 0x30, &certseq) != 0) return -1;    // Certificate ::= SEQUENCE
    if (der_enter(&certseq, 0x30, &tbs) != 0) return -1;    // tbsCertificate ::= SEQUENCE

    if (tbs.p < tbs.end && *tbs.p == 0xA0) {                // version [0] EXPLICIT (optional)
        if (der_skip(&tbs) != 0) return -1;
    }
    for (int i = 0; i < 5; i++)                             // serial, signature, issuer, validity, subject
        if (der_skip(&tbs) != 0) return -1;

    if (der_enter(&tbs, 0x30, &spki) != 0) return -1;       // subjectPublicKeyInfo ::= SEQUENCE
    if (der_skip(&spki) != 0) return -1;                    // algorithm (OID + curve)

    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&spki, &t, &v, &vl) != 0 || t != 0x03) return -1;   // subjectPublicKey BIT STRING
    if (vl < 2 || v[0] != 0x00) return -1;                  // leading byte = unused-bit count (0)
    *point = v + 1; *plen = vl - 1;                         // the raw EC point (0x04 || X || Y)
    return 0;
}

// ---- known-answer self-test ---------------------------------------------------------

static int hv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void unhex(uint8_t* out, const char* h, int n) {
    for (int i = 0; i < n; i++) out[i] = (uint8_t)((hv(h[2*i]) << 4) | hv(h[2*i+1]));
}
static int eq(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

// A real self-signed P-256 certificate (286 bytes) and its uncompressed public-key point.
static const char CERT_HEX[] =
    "3082011a3081c1a00302010202051234567890300a06082a8648ce3d0403023015"
    "3113301106035504030c0a6e79786f732d74657374301e170d3236303130313030"
    "303030305a170d3330303130313030303030305a30153113301106035504030c0a"
    "6e79786f732d746573743059301306072a8648ce3d020106082a8648ce3d030107"
    "034200041796af101acda1031c74a1f36f72c628c919048b74c84796fa19a18c2b"
    "ed506845f4e9d052e2023987ec4ef83ee3efd899964c057e1b552dc44f77c83670"
    "0b9a300a06082a8648ce3d040302034800304502204c26b5ecf4442b4a4c96cb02"
    "a0697e9c33646858e26eef55faa313b1331b57e0022100e97d6032ca4fc4f57d46"
    "e62a1a45531e7ede4f97aade7206b4c468bcba8f3976";
static const char POINT_HEX[] =
    "041796af101acda1031c74a1f36f72c628c919048b74c84796fa19a18c2bed5068"
    "45f4e9d052e2023987ec4ef83ee3efd899964c057e1b552dc44f77c836700b9a";

int der_selftest(void) {
    int pass = 0, total = 0;

    // 1) TLV + nesting: SEQUENCE { INTEGER 7, OID {0x2A} }
    {
        static const uint8_t d[] = { 0x30,0x06, 0x02,0x01,0x07, 0x06,0x01,0x2A };
        der_t c = { d, d + sizeof(d) }, s;
        uint8_t t; const uint8_t* v; uint32_t vl; int ok = 1;
        if (der_enter(&c, 0x30, &s) != 0) ok = 0;
        else {
            if (der_read(&s, &t, &v, &vl) != 0 || t != 0x02 || vl != 1 || v[0] != 0x07) ok = 0;
            if (der_read(&s, &t, &v, &vl) != 0 || t != 0x06 || vl != 1 || v[0] != 0x2A) ok = 0;
        }
        total++;
        if (ok) { pass++; printf("der: TLV parse + nesting PASS\n"); }
        else      printf("der: TLV parse + nesting FAIL\n");
    }

    // 2) long-form length: OCTET STRING with a 256-byte value (length 0x82 0x01 0x00)
    {
        static uint8_t d[4 + 256];
        d[0] = 0x04; d[1] = 0x82; d[2] = 0x01; d[3] = 0x00;
        for (int i = 0; i < 256; i++) d[4 + i] = (uint8_t)i;
        der_t c = { d, d + sizeof(d) };
        uint8_t t; const uint8_t* v; uint32_t vl;
        int ok = (der_read(&c, &t, &v, &vl) == 0) && t == 0x04 && vl == 256 && v[0] == 0 && v[255] == 255;
        total++;
        if (ok) { pass++; printf("der: long-form length PASS\n"); }
        else      printf("der: long-form length FAIL\n");
    }

    // 3) real X.509 (P-256): extract the subjectPublicKeyInfo EC point
    {
        static uint8_t cert[286], want[65];
        const uint8_t* pt; uint32_t pl;
        unhex(cert, CERT_HEX, 286);
        unhex(want, POINT_HEX, 65);
        int ok = (der_x509_ec_pubkey(cert, 286, &pt, &pl) == 0) && pl == 65 && eq(pt, want, 65);
        total++;
        if (ok) { pass++; printf("der: X.509 EC public-key extraction PASS (65-byte point)\n"); }
        else      printf("der: X.509 EC public-key extraction FAIL\n");
    }

    printf("der: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
