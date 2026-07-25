#ifndef X509_H
#define X509_H
// X.509 certificate-chain verification against a pinned trust anchor. See x509.c.
#include "kernel.h"

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

// Known-answer self-test: a real chain (accept), a one-byte-tampered copy (reject as forged),
// and a root-less prefix (reject as not-anchored). Returns 0 if all pass.
int x509_selftest(void);

#endif
