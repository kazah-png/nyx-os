#ifndef DER_H
#define DER_H
// Minimal DER/ASN.1 reader — the encoding X.509 certificates use. See der.c.
#include "kernel.h"

// A cursor over a DER byte range [p, end).
typedef struct { const uint8_t* p; const uint8_t* end; } der_t;

// Read one TLV at the cursor: return its tag, point *val at the value bytes, set *vlen, and
// advance the cursor past the whole element. Returns 0 on success, -1 on a malformed/overrun.
int der_read(der_t* c, uint8_t* tag, const uint8_t** val, uint32_t* vlen);

// Read the next TLV, require tag == expect, and set `inner` to iterate over its value
// (for a constructed type: SEQUENCE 0x30, SET 0x31, context [0] 0xA0). Advances c past it.
int der_enter(der_t* c, uint8_t expect, der_t* inner);

// Skip the next TLV (any tag). Returns 0 on success, -1 on error.
int der_skip(der_t* c);

// Navigate a DER X.509 certificate to subjectPublicKeyInfo and return the raw EC public-key
// point (uncompressed 0x04||X||Y). Returns 0 on success, -1 if the structure doesn't match.
int der_x509_ec_pubkey(const uint8_t* cert, uint32_t clen, const uint8_t** point, uint32_t* plen);

// ---- helpers for certificate-chain verification (v5.9.65) --------------------------------

// Return the raw DER bytes of the tbsCertificate — the exact bytes a certificate signature
// covers (the full TLV, header included). Returns 0 on success, -1 on malformed input.
int der_x509_tbs(const uint8_t* cert, uint32_t clen, const uint8_t** tbs, uint32_t* tbs_len);

// Return the OID value bytes of the outer signatureAlgorithm (which scheme+hash signed the
// certificate). Returns 0 on success, -1 otherwise.
int der_x509_sig_alg(const uint8_t* cert, uint32_t clen, const uint8_t** oid, uint32_t* oid_len);

// Return the signatureValue: the BIT STRING contents (a DER SEQUENCE{r,s} for ECDSA, or the
// raw RSA signature). Returns 0 on success, -1 otherwise.
int der_x509_signature(const uint8_t* cert, uint32_t clen, const uint8_t** sig, uint32_t* sig_len);

// A parsed subjectPublicKeyInfo: either an EC point (with its curve) or an RSA modulus+exponent.
#define DER_KEY_EC        0
#define DER_KEY_RSA       1
#define DER_CURVE_P256    0
#define DER_CURVE_P384    1
#define DER_CURVE_OTHER (-1)
typedef struct {
    int type;                                        // DER_KEY_EC or DER_KEY_RSA
    int curve;                                       // EC only: DER_CURVE_P256 / P384 / OTHER
    const uint8_t* ec_point; uint32_t ec_point_len;  // EC: uncompressed 0x04||X||Y
    const uint8_t* rsa_n;    uint32_t rsa_n_len;     // RSA modulus (big-endian, DER INTEGER)
    const uint8_t* rsa_e;    uint32_t rsa_e_len;     // RSA public exponent
} der_pubkey_t;

// Extract and classify a certificate's subjectPublicKeyInfo. Returns 0 on success, -1 otherwise.
int der_x509_pubkey(const uint8_t* cert, uint32_t clen, der_pubkey_t* out);

// Run the DER reader known-answer tests (TLV, long-form length, real-cert extraction).
int der_selftest(void);

#endif
