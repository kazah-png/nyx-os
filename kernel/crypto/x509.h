#ifndef X509_H
#define X509_H
// X.509 certificate-chain verification against a pinned trust anchor. See x509.c.
#include "../core/kernel.h"

// Chain-verification result codes.
#define X509_OK          0    // every link verified AND the top is a pinned trusted root
#define X509_INCOMPLETE  1    // links checked, but not anchored to a pinned root (unknown root
                              // or an unsupported signature algorithm) — NOT a detected forgery
#define X509_FORGED    (-1)   // a supported-algorithm link failed cryptographic verification

// Verify a certificate chain. certs[0] is the leaf; certs[n-1] is the topmost certificate the
// server sent. Each certificate is verified under the next one's public key, and the top must
// carry a pinned trusted-root public key. `msg` (size msgcap) receives a short status string.
// Returns one of the X509_* codes above.
int x509_verify_chain(const uint8_t* const* certs, const uint32_t* lens, int n,
                      char* msg, int msgcap);

// Does the leaf certificate cover `host`? Matches the subjectAltName dNSName entries
// case-insensitively, honouring a single leading "*." wildcard label. Returns 0 on a match,
// 1 if no dNSName matches, -1 if there is no usable subjectAltName.
int x509_check_host(const uint8_t* leaf, uint32_t leaf_len, const char* host);

// Is the certificate currently within its validity window (per the RTC)? Returns 0 if valid,
// 1 if not yet valid, 2 if expired, -1 if the dates cannot be parsed.
int x509_check_validity(const uint8_t* cert, uint32_t clen);

// Pure accept/reject verdict for a validity window (packed-decimal YYYYMMDDHHMMSS times):
// 0 = valid, 1 = not yet valid, 2 = expired. Bounds inclusive (RFC 5280). Used by
// x509_check_validity; exposed so the boundary decision is independently KAT-tested.
int x509_time_in_window(uint64_t now, uint64_t nb, uint64_t na);

// Known-answer self-test: a real chain (accept), a one-byte-tampered copy (reject as forged),
// and a root-less prefix (reject as not-anchored). Returns 0 if all pass.
int x509_selftest(void);

#endif
