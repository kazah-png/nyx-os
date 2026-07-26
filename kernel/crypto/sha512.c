// ============================================================
// sha512.c - SHA-512 and SHA-384 (FIPS 180-4), v5.9.63
// ============================================================
// The CA links in real TLS certificate chains (example.com's, for one) are signed with
// ECDSA-P384 / SHA-384, so verifying a chain to a trusted root needs SHA-384 alongside the
// SHA-256 the kernel already has. SHA-384 is SHA-512 with a different initial state and a
// truncated output, so both live here. Pinned by `sha512test` against the FIPS 180-4 vectors.
#include "../core/kernel.h"
#include "sha512.h"

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static uint64_t ror(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

static void sha512_transform(uint64_t H[8], const uint8_t p[128]) {
    uint64_t W[80];
    for (int i = 0; i < 16; i++)
        W[i] = ((uint64_t)p[i*8] << 56) | ((uint64_t)p[i*8+1] << 48) | ((uint64_t)p[i*8+2] << 40)
             | ((uint64_t)p[i*8+3] << 32) | ((uint64_t)p[i*8+4] << 24) | ((uint64_t)p[i*8+5] << 16)
             | ((uint64_t)p[i*8+6] << 8) | (uint64_t)p[i*8+7];
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ror(W[i-15], 1) ^ ror(W[i-15], 8) ^ (W[i-15] >> 7);
        uint64_t s1 = ror(W[i-2], 19) ^ ror(W[i-2], 61) ^ (W[i-2] >> 6);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }
    uint64_t a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = ror(e,14) ^ ror(e,18) ^ ror(e,41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = h + S1 + ch + K512[i] + W[i];
        uint64_t S0 = ror(a,28) ^ ror(a,34) ^ ror(a,39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    H[0]+=a; H[1]+=b; H[2]+=c; H[3]+=d; H[4]+=e; H[5]+=f; H[6]+=g; H[7]+=h;
}

void sha512_init(sha512_ctx_t* c) {
    static const uint64_t IV[8] = {
        0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL };
    for (int i = 0; i < 8; i++) c->state[i] = IV[i];
    c->buflen = 0; c->total = 0;
}
void sha384_init(sha512_ctx_t* c) {
    static const uint64_t IV[8] = {
        0xcbbb9d5dc1059ed8ULL,0x629a292a367cd507ULL,0x9159015a3070dd17ULL,0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL,0x8eb44a8768581511ULL,0xdb0c2e0d64f98fa7ULL,0x47b5481dbefa4fa4ULL };
    for (int i = 0; i < 8; i++) c->state[i] = IV[i];
    c->buflen = 0; c->total = 0;
}

void sha512_update(sha512_ctx_t* c, const uint8_t* data, uint32_t len) {
    c->total += len;
    while (len) {
        uint32_t n = 128 - c->buflen; if (n > len) n = len;
        for (uint32_t i = 0; i < n; i++) c->buf[c->buflen + i] = data[i];
        c->buflen += n; data += n; len -= n;
        if (c->buflen == 128) { sha512_transform(c->state, c->buf); c->buflen = 0; }
    }
}

// Append the 0x80/zero padding and the 128-bit length, then run the final block(s), leaving
// the digest in c->state.
static void sha512_finish(sha512_ctx_t* c) {
    uint64_t bits = c->total * 8;
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 112) {                       // no room for the length in this block
        while (c->buflen < 128) c->buf[c->buflen++] = 0;
        sha512_transform(c->state, c->buf); c->buflen = 0;
    }
    while (c->buflen < 112) c->buf[c->buflen++] = 0;
    for (int i = 0; i < 8; i++) c->buf[112 + i] = 0;                        // high 64 bits = 0
    for (int i = 0; i < 8; i++) c->buf[120 + i] = (uint8_t)(bits >> (8 * (7 - i)));
    sha512_transform(c->state, c->buf);
}

void sha512_final(sha512_ctx_t* c, uint8_t out[64]) {
    sha512_finish(c);
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) out[i*8 + j] = (uint8_t)(c->state[i] >> (8 * (7 - j)));
}
void sha384_final(sha512_ctx_t* c, uint8_t out[48]) {
    sha512_finish(c);
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 8; j++) out[i*8 + j] = (uint8_t)(c->state[i] >> (8 * (7 - j)));
}

void sha512(const uint8_t* d, uint32_t n, uint8_t out[64]) {
    sha512_ctx_t c; sha512_init(&c); sha512_update(&c, d, n); sha512_final(&c, out);
}
void sha384(const uint8_t* d, uint32_t n, uint8_t out[48]) {
    sha512_ctx_t c; sha384_init(&c); sha512_update(&c, d, n); sha384_final(&c, out);
}

// ---- known-answer self-test (FIPS 180-4) --------------------------------------------
static int hv(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return 0;
}
static int eqhex(const uint8_t* got, const char* hex, int n) {
    for (int i = 0; i < n; i++)
        if (got[i] != (uint8_t)((hv(hex[2*i]) << 4) | hv(hex[2*i+1]))) return 0;
    return 1;
}

int sha512_selftest(void) {
    int pass = 0, total = 0;
    uint8_t d5[64], d3[48];

    // "abc"
    sha512((const uint8_t*)"abc", 3, d5); total++;
    if (eqhex(d5, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                  "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", 64)) {
        pass++; printf("sha512: SHA-512(\"abc\") PASS\n");
    } else printf("sha512: SHA-512(\"abc\") FAIL\n");

    sha384((const uint8_t*)"abc", 3, d3); total++;
    if (eqhex(d3, "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
                  "8086072ba1e7cc2358baeca134c825a7", 48)) {
        pass++; printf("sha512: SHA-384(\"abc\") PASS\n");
    } else printf("sha512: SHA-384(\"abc\") FAIL\n");

    // empty
    sha512((const uint8_t*)"", 0, d5); total++;
    if (eqhex(d5, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
                  "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", 64)) {
        pass++; printf("sha512: SHA-512(\"\") PASS\n");
    } else printf("sha512: SHA-512(\"\") FAIL\n");

    // a 168-byte multi-block message (56-char pattern x3)
    static const char L[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
                            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
                            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha512((const uint8_t*)L, 168, d5); total++;
    if (eqhex(d5, "33dd4ac487bcba1462df408fd383ebd5bfb2bfa4e7dd3572d752887d1ae78852"
                  "5f53b12fd1fd9a6a61e6607823e9a30028be4845276bd0e93411b42d3d084cf5", 64)) {
        pass++; printf("sha512: SHA-512(168-byte, multi-block) PASS\n");
    } else printf("sha512: SHA-512(168-byte, multi-block) FAIL\n");

    sha384((const uint8_t*)L, 168, d3); total++;
    if (eqhex(d3, "d1ec278f9b7f2f8c2ca965b3950117d978e5a60d36253cc1addd76644199a081"
                  "df40d41b893bdca795979108ab57ad7b", 48)) {
        pass++; printf("sha512: SHA-384(168-byte, multi-block) PASS\n");
    } else printf("sha512: SHA-384(168-byte, multi-block) FAIL\n");

    printf("sha512: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
