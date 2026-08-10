// ============================================================
// ChaCha20-Poly1305 AEAD (RFC 8439 §2.8).
// ============================================================
// Encrypt-then-MAC composed from the existing primitives: the one-time Poly1305
// key is the first 32 bytes of ChaCha20 block 0; the plaintext is XORed with the
// ChaCha20 keystream from block 1; the Poly1305 tag authenticates the AAD and
// ciphertext (each zero-padded to a 16-byte boundary) followed by their little-
// endian 64-bit lengths.

#include "chacha20poly1305.h"
#include "chacha20.h"
#include "poly1305.h"
#include "ct.h"

static void poly_key_gen(const uint8_t key[32], const uint8_t nonce[12], uint8_t otk[32]) {
    uint8_t block[64];
    chacha20_block(key, 0, nonce, block);      // counter 0
    memcpy(otk, block, 32);
}

// Poly1305 over aad ‖ pad16 ‖ ct ‖ pad16 ‖ le64(aad_len) ‖ le64(ct_len).
static int compute_tag(const uint8_t otk[32], const uint8_t* aad, uint32_t aad_len,
                       const uint8_t* ct, uint32_t ct_len, uint8_t tag[16]) {
    uint32_t ap = (16 - (aad_len & 15)) & 15;
    uint32_t cp = (16 - (ct_len  & 15)) & 15;
    uint32_t total = aad_len + ap + ct_len + cp + 16;
    uint8_t* m = (uint8_t*)kmalloc(total);
    if (!m) return -1;

    uint32_t o = 0;
    if (aad_len) { memcpy(m, aad, aad_len); o = aad_len; }
    for (uint32_t i = 0; i < ap; i++) m[o++] = 0;         // pad AAD to a 16-byte boundary
    if (ct_len)  { memcpy(m + o, ct, ct_len); o += ct_len; }
    for (uint32_t i = 0; i < cp; i++) m[o++] = 0;         // pad ciphertext likewise
    uint64_t al = aad_len, cl = ct_len;
    for (int i = 0; i < 8; i++) m[o++] = (uint8_t)(al >> (8 * i));
    for (int i = 0; i < 8; i++) m[o++] = (uint8_t)(cl >> (8 * i));

    poly1305_mac(otk, m, total, tag);
    kfree(m);
    return 0;
}

int chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                             const uint8_t* aad, uint32_t aad_len,
                             const uint8_t* pt, uint32_t pt_len,
                             uint8_t* ct, uint8_t tag[16]) {
    uint8_t otk[32];
    poly_key_gen(key, nonce, otk);
    chacha20_xor(key, 1, nonce, pt, ct, pt_len);        // encrypt from block 1
    return compute_tag(otk, aad, aad_len, ct, pt_len, tag);
}

int chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                             const uint8_t* aad, uint32_t aad_len,
                             const uint8_t* ct, uint32_t ct_len,
                             const uint8_t tag[16], uint8_t* pt) {
    uint8_t otk[32], expect[16];
    poly_key_gen(key, nonce, otk);
    if (compute_tag(otk, aad, aad_len, ct, ct_len, expect) != 0) return -1;
    if (ct_memcmp(expect, tag, 16) != 0) return 1;      // authentication failed (constant-time)
    chacha20_xor(key, 1, nonce, ct, pt, ct_len);        // only decrypt once authentic
    return 0;
}

// ---------------- Known-answer test (RFC 8439 §2.8.2) ----------------
int chacha20poly1305_selftest(void) {
    uint8_t key[32]; for (int i = 0; i < 32; i++) key[i] = (uint8_t)(0x80 + i);
    uint8_t nonce[12] = { 0x07,0x00,0x00,0x00, 0x40,0x41,0x42,0x43, 0x44,0x45,0x46,0x47 };
    static const uint8_t aad[12] = { 0x50,0x51,0x52,0x53, 0xc0,0xc1,0xc2,0xc3, 0xc4,0xc5,0xc6,0xc7 };
    const char* pt = "Ladies and Gentlemen of the class of '99: If I could offer you "
                     "only one tip for the future, sunscreen would be it.";
    static const uint8_t CT[114] = {
        0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb, 0x7b, 0x86, 0xaf, 0xbc,
        0x53, 0xef, 0x7e, 0xc2, 0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe,
        0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6, 0x3d, 0xbe, 0xa4, 0x5e,
        0x8c, 0xa9, 0x67, 0x12, 0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
        0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29, 0x05, 0xd6, 0xa5, 0xb6,
        0x7e, 0xcd, 0x3b, 0x36, 0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c,
        0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58, 0xfa, 0xb3, 0x24, 0xe4,
        0xfa, 0xd6, 0x75, 0x94, 0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
        0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d, 0xe5, 0x76, 0xd2, 0x65,
        0x86, 0xce, 0xc6, 0x4b, 0x61, 0x16,
    };
    static const uint8_t TAG[16] = {
        0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb,
        0xd0, 0x60, 0x06, 0x91,
    };

    uint8_t ct[114], tag[16], back[114];
    if (chacha20poly1305_encrypt(key, nonce, aad, 12, (const uint8_t*)pt, 114, ct, tag) != 0) return 1;
    for (int i = 0; i < 114; i++) if (ct[i]  != CT[i])  return 2;
    for (int i = 0; i < 16;  i++) if (tag[i] != TAG[i]) return 3;

    // decrypt the RFC ciphertext: must authenticate and recover the plaintext
    if (chacha20poly1305_decrypt(key, nonce, aad, 12, CT, 114, TAG, back) != 0) return 4;
    for (int i = 0; i < 114; i++) if (back[i] != (uint8_t)pt[i]) return 5;

    // tamper the ciphertext / the AAD: authentication must fail (nonzero)
    uint8_t bad[114]; for (int i = 0; i < 114; i++) bad[i] = CT[i]; bad[0] ^= 1;
    if (chacha20poly1305_decrypt(key, nonce, aad, 12, bad, 114, TAG, back) == 0) return 6;
    uint8_t badaad[12]; for (int i = 0; i < 12; i++) badaad[i] = aad[i]; badaad[0] ^= 0x80;
    if (chacha20poly1305_decrypt(key, nonce, badaad, 12, CT, 114, TAG, back) == 0) return 7;
    return 0;
}
