// ============================================================
// der.c - DER/ASN.1 reader for X.509, v5.9.58
// ============================================================
// First step of the TLS trust model: to verify a server we must read its X.509 certificate,
// which is DER-encoded (nested Tag-Length-Value). This is a small, bounds-checked DER cursor
// plus one navigator, `der_x509_ec_pubkey`, that walks Certificate -> tbsCertificate ->
// subjectPublicKeyInfo -> subjectPublicKey and hands back the server's EC public-key point.
// Later increments add P-256/ECDSA to verify the ServerKeyExchange signature under that key.
// Pinned by `dertest`: TLV parsing, long-form lengths, and extraction from a real P-256 cert.
#include "../core/kernel.h"
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

// ---- certificate-chain helpers (v5.9.65) --------------------------------------------

// Enter the outer Certificate ::= SEQUENCE and return a cursor over its three elements
// (tbsCertificate, signatureAlgorithm, signatureValue).
static int cert_body(const uint8_t* cert, uint32_t clen, der_t* body) {
    der_t top = { cert, cert + clen };
    return der_enter(&top, 0x30, body);
}

int der_x509_tbs(const uint8_t* cert, uint32_t clen, const uint8_t** tbs, uint32_t* tbs_len) {
    der_t body;
    if (cert_body(cert, clen, &body) != 0) return -1;
    const uint8_t* start = body.p;                          // start of the tbsCertificate TLV
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&body, &t, &v, &vl) != 0 || t != 0x30) return -1;
    *tbs = start; *tbs_len = (uint32_t)((v + vl) - start);  // header + content = the signed bytes
    return 0;
}

int der_x509_sig_alg(const uint8_t* cert, uint32_t clen, const uint8_t** oid, uint32_t* oid_len) {
    der_t body, alg;
    if (cert_body(cert, clen, &body) != 0) return -1;
    if (der_skip(&body) != 0) return -1;                    // tbsCertificate
    if (der_enter(&body, 0x30, &alg) != 0) return -1;       // signatureAlgorithm ::= SEQUENCE
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&alg, &t, &v, &vl) != 0 || t != 0x06) return -1;   // algorithm OID
    *oid = v; *oid_len = vl;
    return 0;
}

int der_x509_signature(const uint8_t* cert, uint32_t clen, const uint8_t** sig, uint32_t* sig_len) {
    der_t body;
    if (cert_body(cert, clen, &body) != 0) return -1;
    if (der_skip(&body) != 0) return -1;                    // tbsCertificate
    if (der_skip(&body) != 0) return -1;                    // signatureAlgorithm
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&body, &t, &v, &vl) != 0 || t != 0x03) return -1; // signatureValue BIT STRING
    if (vl < 1 || v[0] != 0x00) return -1;                  // unused-bit count = 0
    *sig = v + 1; *sig_len = vl - 1;
    return 0;
}

static int oid_match(const uint8_t* v, uint32_t vl, const uint8_t* want, uint32_t wl) {
    if (vl != wl) return 0;
    for (uint32_t i = 0; i < vl; i++) if (v[i] != want[i]) return 0;
    return 1;
}

int der_x509_pubkey(const uint8_t* cert, uint32_t clen, der_pubkey_t* out) {
    static const uint8_t OID_EC[]   = { 0x2a,0x86,0x48,0xce,0x3d,0x02,0x01 };            // id-ecPublicKey
    static const uint8_t OID_RSA[]  = { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01 };  // rsaEncryption
    static const uint8_t OID_P256[] = { 0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07 };       // prime256v1
    static const uint8_t OID_P384[] = { 0x2b,0x81,0x04,0x00,0x22 };                      // secp384r1

    out->type = -1; out->curve = DER_CURVE_OTHER;
    out->ec_point = 0; out->ec_point_len = 0;
    out->rsa_n = 0; out->rsa_n_len = 0; out->rsa_e = 0; out->rsa_e_len = 0;

    der_t top = { cert, cert + clen }, certseq, tbs, spki, alg;
    if (der_enter(&top, 0x30, &certseq) != 0) return -1;    // Certificate
    if (der_enter(&certseq, 0x30, &tbs) != 0) return -1;    // tbsCertificate
    if (tbs.p < tbs.end && *tbs.p == 0xA0) { if (der_skip(&tbs) != 0) return -1; }  // version [0]
    for (int i = 0; i < 5; i++) if (der_skip(&tbs) != 0) return -1;   // serial..subject
    if (der_enter(&tbs, 0x30, &spki) != 0) return -1;       // subjectPublicKeyInfo
    if (der_enter(&spki, 0x30, &alg) != 0) return -1;       // AlgorithmIdentifier ::= SEQUENCE

    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&alg, &t, &v, &vl) != 0 || t != 0x06) return -1;    // algorithm OID

    if (oid_match(v, vl, OID_EC, sizeof(OID_EC))) {
        out->type = DER_KEY_EC;
        if (der_read(&alg, &t, &v, &vl) == 0 && t == 0x06) {         // namedCurve OID
            if (oid_match(v, vl, OID_P256, sizeof(OID_P256))) out->curve = DER_CURVE_P256;
            else if (oid_match(v, vl, OID_P384, sizeof(OID_P384))) out->curve = DER_CURVE_P384;
        }
        if (der_read(&spki, &t, &v, &vl) != 0 || t != 0x03) return -1;   // subjectPublicKey BIT STRING
        if (vl < 2 || v[0] != 0x00) return -1;
        out->ec_point = v + 1; out->ec_point_len = vl - 1;              // 0x04 || X || Y
        return 0;
    }
    if (oid_match(v, vl, OID_RSA, sizeof(OID_RSA))) {
        out->type = DER_KEY_RSA;
        if (der_read(&spki, &t, &v, &vl) != 0 || t != 0x03) return -1;   // subjectPublicKey BIT STRING
        if (vl < 1 || v[0] != 0x00) return -1;
        der_t rsa_outer = { v + 1, v + vl }, rsaseq;                     // RSAPublicKey ::= SEQUENCE
        if (der_enter(&rsa_outer, 0x30, &rsaseq) != 0) return -1;
        if (der_read(&rsaseq, &t, &v, &vl) != 0 || t != 0x02) return -1; // modulus n
        out->rsa_n = v; out->rsa_n_len = vl;
        if (der_read(&rsaseq, &t, &v, &vl) != 0 || t != 0x02) return -1; // publicExponent e
        out->rsa_e = v; out->rsa_e_len = vl;
        return 0;
    }
    return -1;                                              // unrecognized key algorithm
}

// Enter Certificate -> tbsCertificate, skip the optional version [0], then skip `n` more fields.
// Leaves `tbs` positioned at the (n+1)-th tbsCertificate field. Order is: serialNumber,
// signature, issuer, validity, subject, subjectPublicKeyInfo, [uniqueIDs], extensions.
static int tbs_skip(const uint8_t* cert, uint32_t clen, der_t* tbs, int n) {
    der_t top = { cert, cert + clen }, certseq;
    if (der_enter(&top, 0x30, &certseq) != 0) return -1;
    if (der_enter(&certseq, 0x30, tbs) != 0) return -1;
    if (tbs->p < tbs->end && *tbs->p == 0xA0) { if (der_skip(tbs) != 0) return -1; }  // version [0]
    for (int i = 0; i < n; i++) if (der_skip(tbs) != 0) return -1;
    return 0;
}

// Parse a DER Time (UTCTime YYMMDDHHMMSSZ or GeneralizedTime YYYYMMDDHHMMSSZ) into a packed
// decimal YYYYMMDDHHMMSS. Returns 0 on a malformed value. Untrusted input (from a cert), so
// EVERY date position must be an actual digit: the old check allowed a 'Z' in any of the first
// 14 bytes, which let a malformed Time with 'Z' in a date slot parse to a bogus (but bounded)
// date instead of being rejected. Digit `d2()` reads two ASCII digits with `unsigned` arithmetic,
// so nothing here converts a possibly-negative `int` into `uint64_t` (was 8 -Wsign-conversion hits).
static uint64_t d2(const uint8_t* p) { return (uint64_t)(unsigned)(p[0]-'0')*10u + (unsigned)(p[1]-'0'); }
static uint64_t parse_time(uint8_t tag, const uint8_t* p, uint32_t vl) {
    uint32_t ndig = (tag == 0x17) ? 12u : (tag == 0x18) ? 14u : 0u;   // UTCTime: 12 digits, Gen: 14
    if (ndig == 0 || vl < ndig + 1) return 0;                         // +1 for the trailing 'Z'/marker byte
    for (uint32_t i = 0; i < ndig; i++) if (p[i] < '0' || p[i] > '9') return 0;
    uint64_t year;
    if (tag == 0x17) {                                     // UTCTime: 2-digit year, RFC 5280 sliding window
        uint64_t yy = d2(p);
        year = (yy < 50) ? 2000 + yy : 1900 + yy;
        p += 2;
    } else {                                               // GeneralizedTime: 4-digit year
        year = d2(p) * 100 + d2(p + 2);
        p += 4;
    }
    uint64_t mo = d2(p), da = d2(p + 2), hh = d2(p + 4), mi = d2(p + 6), se = d2(p + 8);
    return ((((year*100 + mo)*100 + da)*100 + hh)*100 + mi)*100 + se;
}

int der_x509_validity(const uint8_t* cert, uint32_t clen, uint64_t* not_before, uint64_t* not_after) {
    der_t tbs, val;
    if (tbs_skip(cert, clen, &tbs, 3) != 0) return -1;     // skip serial, signature, issuer
    if (der_enter(&tbs, 0x30, &val) != 0) return -1;       // validity ::= SEQUENCE { nb, na }
    uint8_t t; const uint8_t* v; uint32_t vl;
    if (der_read(&val, &t, &v, &vl) != 0) return -1;
    uint64_t nb = parse_time(t, v, vl);
    if (der_read(&val, &t, &v, &vl) != 0) return -1;
    uint64_t na = parse_time(t, v, vl);
    if (nb == 0 || na == 0) return -1;
    *not_before = nb; *not_after = na;
    return 0;
}

int der_x509_extension(const uint8_t* cert, uint32_t clen, const uint8_t* oid, uint32_t oid_len,
                       const uint8_t** val, uint32_t* val_len) {
    der_t tbs;
    if (tbs_skip(cert, clen, &tbs, 6) != 0) return -1;     // skip serial..subjectPublicKeyInfo
    while (tbs.p < tbs.end && (*tbs.p == 0x81 || *tbs.p == 0x82)) {   // issuer/subject uniqueID
        if (der_skip(&tbs) != 0) return -1;
    }
    der_t extwrap, extlist;
    if (der_enter(&tbs, 0xA3, &extwrap) != 0) return -1;   // extensions [3] EXPLICIT
    if (der_enter(&extwrap, 0x30, &extlist) != 0) return -1;          // SEQUENCE OF Extension
    while (extlist.p < extlist.end) {
        der_t ext;
        if (der_enter(&extlist, 0x30, &ext) != 0) return -1;         // Extension ::= SEQUENCE
        uint8_t t; const uint8_t* v; uint32_t vl;
        if (der_read(&ext, &t, &v, &vl) != 0 || t != 0x06) continue; // extnID OID
        const uint8_t* eid = v; uint32_t eidl = vl;
        if (ext.p < ext.end && *ext.p == 0x01) { if (der_skip(&ext) != 0) continue; }  // critical
        if (der_read(&ext, &t, &v, &vl) != 0 || t != 0x04) continue; // extnValue OCTET STRING
        if (eidl == oid_len && oid_match(eid, eidl, oid, oid_len)) { *val = v; *val_len = vl; return 0; }
    }
    return -1;                                              // extension not present
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

    // 4) ADVERSARIAL / malformed inputs — der_read must REJECT every one (return -1) and
    //    never read past the buffer. The DER cursor is the TLS trust root (it parses the
    //    server's X.509 cert), so these pin its bounds checks: a future edit that drops a
    //    check reintroduces an over-read and this case turns red. der_read is correct today.
    {
        int ok = 1;
        uint8_t t; const uint8_t* v; uint32_t vl;
        // empty buffer: no tag byte
        { der_t c = { (const uint8_t*)"", (const uint8_t*)"" }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // tag present but no length byte
        { static const uint8_t d[] = {0x02}; der_t c = { d, d + sizeof(d) }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // short-form length 5 but only 2 value bytes present
        { static const uint8_t d[] = {0x04,0x05,0x01,0x02}; der_t c = { d, d + sizeof(d) }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // indefinite length (0x80, nb==0) — rejected
        { static const uint8_t d[] = {0x30,0x80}; der_t c = { d, d + sizeof(d) }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // long-form nb==5 (>4, would overflow a 32-bit length) — rejected
        { static const uint8_t d[] = {0x02,0x85,0,0,0,0,0}; der_t c = { d, d + sizeof(d) }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // long-form claims 2 length bytes but only 1 is present
        { static const uint8_t d[] = {0x04,0x82,0x01}; der_t c = { d, d + sizeof(d) }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // long-form length 0xFFFF but a tiny buffer
        { static const uint8_t d[] = {0x04,0x82,0xFF,0xFF,0x00}; der_t c = { d, d + sizeof(d) }; if (der_read(&c, &t, &v, &vl) != -1) ok = 0; }
        // maximal-but-VALID long form (nb==4, len==3) IS accepted — pins the nb<=4 boundary
        { static const uint8_t d[] = {0x04,0x84,0,0,0,3, 0xAA,0xBB,0xCC}; der_t c = { d, d + sizeof(d) };
          if (der_read(&c, &t, &v, &vl) != 0 || t != 0x04 || vl != 3 || v[2] != 0xCC) ok = 0; }
        // der_enter with the wrong expected tag rejects (it is 0x30, not 0x02)
        { static const uint8_t d[] = {0x30,0x03,0x02,0x01,0x07}; der_t c = { d, d + sizeof(d) }, inner;
          if (der_enter(&c, 0x02, &inner) != -1) ok = 0; }
        // the X.509 navigator on a truncated cert fails cleanly (no OOB): SEQUENCE claims 256 bytes
        { static const uint8_t junk[] = {0x30,0x82,0x01,0x00,0x30}; const uint8_t* pt; uint32_t pl;
          if (der_x509_ec_pubkey(junk, sizeof(junk), &pt, &pl) != -1) ok = 0; }
        total++;
        if (ok) { pass++; printf("der: adversarial/malformed rejection PASS\n"); }
        else      printf("der: adversarial/malformed rejection FAIL\n");
    }

    // 5) DER Time decode (parse_time) — the certificate-validity trust surface. Pins the RFC 5280
    //    UTCTime 2-digit-year sliding window (<50 -> 20xx, >=50 -> 19xx), the GeneralizedTime
    //    4-digit-year path, and rejection of malformed times. This is what decides whether a TLS
    //    cert is in date; the window pivot and the malformed rejection were previously unpinned.
    {
        int ok = 1;
        // UTCTime (tag 0x17): the sliding-window pivot, both sides of 50.
        if (parse_time(0x17, (const uint8_t*)"490101000000Z", 13u) != 20490101000000ULL) ok = 0;  // 49 -> 2049
        if (parse_time(0x17, (const uint8_t*)"500101000000Z", 13u) != 19500101000000ULL) ok = 0;  // 50 -> 1950
        if (parse_time(0x17, (const uint8_t*)"260815143000Z", 13u) != 20260815143000ULL) ok = 0;
        // GeneralizedTime (tag 0x18): explicit 4-digit year.
        if (parse_time(0x18, (const uint8_t*)"20260815143000Z", 15u) != 20260815143000ULL) ok = 0;
        // Malformed: a non-digit in a date slot must be REJECTED (return 0), not coerced.
        if (parse_time(0x17, (const uint8_t*)"2608X5143000Z", 13u) != 0) ok = 0;
        // Malformed: value too short for the required digit count -> rejected.
        if (parse_time(0x17, (const uint8_t*)"260815", 6u) != 0) ok = 0;
        // Unknown tag (neither UTCTime nor GeneralizedTime) -> rejected.
        if (parse_time(0x16, (const uint8_t*)"260815143000Z", 13u) != 0) ok = 0;
        total++;
        if (ok) { pass++; printf("der: DER Time (UTCTime window + malformed reject) PASS\n"); }
        else      printf("der: DER Time decode FAIL\n");
    }

    printf("der: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
