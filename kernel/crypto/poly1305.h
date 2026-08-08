#ifndef POLY1305_H
#define POLY1305_H

#include "../core/kernel.h"

/* Poly1305 one-time authenticator (RFC 8439). A 32-byte one-time key (r || s)
 * and an arbitrary message produce a 16-byte tag, evaluating a polynomial in the
 * message blocks modulo 2^130-5 and adding s. It is the authentication half of
 * the ChaCha20-Poly1305 AEAD (pairs with the ChaCha20 stream cipher already in
 * the tree). One-time: a given (r,s) key must never authenticate two messages. */
void poly1305_mac(const uint8_t key[32], const uint8_t* msg, uint32_t len, uint8_t tag[16]);

/* Known-answer test over the RFC 8439 §2.5.2 vector and extra cases. 0 = pass. */
int poly1305_selftest(void);

#endif
