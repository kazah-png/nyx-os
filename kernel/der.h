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

// Run the DER reader known-answer tests (TLV, long-form length, real-cert extraction).
int der_selftest(void);

#endif
