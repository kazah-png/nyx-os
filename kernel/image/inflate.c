// ============================================================
// inflate.c - DEFLATE (RFC 1951) + zlib (RFC 1950) decompression
// ============================================================
// The decompression core PNG's IDAT stream needs (first step toward decoding real images in
// Selene). A compact, allocation-free inflater in the style of Mark Adler's public-domain puff.c:
// a bit reader, canonical-Huffman decode, and the three DEFLATE block types (stored / fixed /
// dynamic). zlib_inflate() adds the 2-byte zlib header + trailing Adler-32 framing (verified).
#include "../core/kernel.h"
#include "inflate.h"

#define INF_MAXBITS   15
#define INF_FIXLCODES 288
#define INF_MAXLCODES 286
#define INF_MAXDCODES 30
#define INF_MAXCODES  (INF_MAXLCODES + INF_MAXDCODES)

typedef struct {
    const uint8_t* in; uint32_t inlen; uint32_t incnt;   // input buffer + position
    int bitbuf; int bitcnt;                              // bit accumulator (LSB-first)
    uint8_t* out; uint32_t outcap; uint32_t outcnt;      // output buffer + written count
    int err;                                             // sticky: set when input underflows
} inf_state;

// Return `need` bits from the stream, LSB-first. On underflow, set err and return 0.
static int inf_bits(inf_state* s, int need) {
    int val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) { s->err = -1; return 0; }
        val |= (int)(s->in[s->incnt++]) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return val & ((1 << need) - 1);
}

typedef struct { short* count; short* symbol; } inf_huff;   // canonical Huffman decode table

// Build a Huffman table from a list of code lengths. Returns 0 for a complete code, >0 for an
// incomplete code (caller decides if that's allowed), <0 for an over-subscribed (invalid) code.
static int inf_construct(inf_huff* h, const short* length, int n) {
    int symbol, len, left;
    short offs[INF_MAXBITS + 1];
    for (len = 0; len <= INF_MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;                         // no codes at all
    left = 1;
    for (len = 1; len <= INF_MAXBITS; len++) { left <<= 1; left -= h->count[len]; if (left < 0) return left; }
    offs[1] = 0;
    for (len = 1; len < INF_MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++) if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = symbol;
    return left;
}

// Decode one symbol from the stream using Huffman table h.
static int inf_decode(inf_state* s, const inf_huff* h) {
    int len, code = 0, first = 0, count, index = 0;
    for (len = 1; len <= INF_MAXBITS; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    return -9;
}

// Length/distance base values and extra-bit counts (RFC 1951 §3.2.5).
static const short inf_lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const short inf_lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const short inf_dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const short inf_dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

// Decode the LZ77 literal/length + distance codes of a compressed block into the output.
static int inf_codes(inf_state* s, const inf_huff* lc, const inf_huff* dc) {
    int symbol, len; unsigned dist;
    do {
        symbol = inf_decode(s, lc);
        if (symbol < 0) return symbol;
        if (symbol < 256) {                                 // literal byte
            if (s->outcnt >= s->outcap) return -2;
            s->out[s->outcnt++] = (uint8_t)symbol;
        } else if (symbol > 256) {                          // length + distance back-reference
            symbol -= 257;
            if (symbol >= 29) return -3;
            len = inf_lens[symbol] + inf_bits(s, inf_lext[symbol]);
            if (s->err) return -1;
            symbol = inf_decode(s, dc);
            if (symbol < 0) return symbol;
            dist = (unsigned)inf_dists[symbol] + (unsigned)inf_bits(s, inf_dext[symbol]);
            if (s->err) return -1;
            if (dist > s->outcnt) return -4;                // reference before start of output
            if (s->outcnt + (uint32_t)len > s->outcap) return -2;
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    } while (symbol != 256);                                 // 256 = end of block
    return 0;
}

// A stored (uncompressed) block: LEN, ~LEN, then LEN raw bytes.
static int inf_stored(inf_state* s) {
    unsigned len;
    s->bitbuf = 0; s->bitcnt = 0;                            // discard bits to a byte boundary
    if (s->incnt + 4 > s->inlen) return -5;
    len = s->in[s->incnt++]; len |= (unsigned)s->in[s->incnt++] << 8;
    if (s->in[s->incnt++] != ((~len) & 0xff)) return -6;    // NLEN = one's complement of LEN
    if (s->in[s->incnt++] != (((~len) >> 8) & 0xff)) return -6;
    if (s->incnt + len > s->inlen) return -5;
    if (s->outcnt + len > s->outcap) return -2;
    while (len--) s->out[s->outcnt++] = s->in[s->incnt++];
    return 0;
}

// A block using the fixed (predefined) Huffman codes of RFC 1951 §3.2.6.
static int inf_fixed(inf_state* s) {
    short lengths[INF_FIXLCODES];
    short lcnt[INF_MAXBITS + 1], lsym[INF_FIXLCODES];
    short dcnt[INF_MAXBITS + 1], dsym[INF_MAXDCODES];
    inf_huff lc = { lcnt, lsym }, dc = { dcnt, dsym };
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    inf_construct(&lc, lengths, INF_FIXLCODES);
    for (i = 0; i < INF_MAXDCODES; i++) lengths[i] = 5;
    inf_construct(&dc, lengths, INF_MAXDCODES);
    return inf_codes(s, &lc, &dc);
}

// A block using dynamic Huffman codes: a code-length code describes the lit/len + distance codes.
static int inf_dynamic(inf_state* s) {
    static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    short lengths[INF_MAXCODES];
    short lcnt[INF_MAXBITS + 1], lsym[INF_MAXLCODES];
    short dcnt[INF_MAXBITS + 1], dsym[INF_MAXDCODES];
    inf_huff lc = { lcnt, lsym }, dc = { dcnt, dsym };
    int nlen, ndist, ncode, index, err;

    nlen  = inf_bits(s, 5) + 257;
    ndist = inf_bits(s, 5) + 1;
    ncode = inf_bits(s, 4) + 4;
    if (s->err) return -1;
    if (nlen > INF_MAXLCODES || ndist > INF_MAXDCODES) return -7;

    for (index = 0; index < ncode; index++) lengths[order[index]] = (short)inf_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return -1;
    if (inf_construct(&lc, lengths, 19) != 0) return -8;     // code-length code must be complete

    index = 0;
    while (index < nlen + ndist) {
        int symbol = inf_decode(s, &lc);
        if (symbol < 0) return symbol;
        if (symbol < 16) lengths[index++] = (short)symbol;
        else {
            int len = 0, repeat;
            if (symbol == 16) { if (index == 0) return -9; len = lengths[index - 1]; repeat = 3 + inf_bits(s, 2); }
            else if (symbol == 17) repeat = 3 + inf_bits(s, 3);
            else repeat = 11 + inf_bits(s, 7);
            if (s->err) return -1;
            if (index + repeat > nlen + ndist) return -10;
            while (repeat--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) return -11;                       // must have an end-of-block code

    err = inf_construct(&lc, lengths, nlen);
    if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) return -12;
    err = inf_construct(&dc, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) return -13;
    return inf_codes(s, &lc, &dc);
}

int inflate_raw(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen) {
    inf_state s;
    int last, type, err = 0;
    s.in = src; s.inlen = srclen; s.incnt = 0;
    s.bitbuf = 0; s.bitcnt = 0;
    s.out = dst; s.outcap = dstcap; s.outcnt = 0; s.err = 0;
    do {
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.err) { err = -1; break; }
        if      (type == 0) err = inf_stored(&s);
        else if (type == 1) err = inf_fixed(&s);
        else if (type == 2) err = inf_dynamic(&s);
        else                err = -14;                       // reserved block type
        if (err != 0) break;
    } while (!last);
    if (outlen) *outlen = s.outcnt;
    return err;
}

// Adler-32 checksum (RFC 1950) — the trailer zlib streams carry.
static uint32_t inf_adler32(const uint8_t* d, uint32_t n) {
    uint32_t a = 1, b = 0, i;
    for (i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

int zlib_inflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen) {
    uint32_t hdr = 2, olen = 0, want; int err;
    if (srclen < 6) return -20;
    if ((src[0] & 0x0f) != 8) return -21;                    // CM field must be 8 (DEFLATE)
    if ((((uint32_t)src[0] << 8) | src[1]) % 31 != 0) return -22;   // header check bits
    if (src[1] & 0x20) hdr += 4;                             // FDICT set: skip the 4-byte dict id
    if (srclen < hdr + 4) return -20;
    err = inflate_raw(src + hdr, srclen - hdr - 4, dst, dstcap, &olen);
    if (outlen) *outlen = olen;
    if (err != 0) return err;
    want = ((uint32_t)src[srclen - 4] << 24) | ((uint32_t)src[srclen - 3] << 16) |
           ((uint32_t)src[srclen - 2] << 8) | src[srclen - 1];
    if (inf_adler32(dst, olen) != want) return -23;          // integrity check failed
    return 0;
}

// ---- known-answer self-test (`deflatetest`) ----
// Vectors generated by scratchpad/gen_inflate.py (Python zlib): a compressed blob + its expected
// plaintext for each DEFLATE block type, plus a zlib-wrapped (Adler-32) case.
static const uint8_t INF_DYN_C[49] = {
    0x0b,0xc9,0x48,0x55,0x28,0x2c,0xcd,0x4c,0xce,0x56,0x48,0x2a,
    0xca,0x2f,0xcf,0x53,0x48,0xcb,0xaf,0x50,0xc8,0x2a,0xcd,0x2d,
    0x28,0x56,0xc8,0x2f,0x4b,0x2d,0x52,0x28,0x01,0x4a,0xe7,0x24,
    0x56,0x55,0x2a,0xa4,0xe4,0xa7,0xeb,0x29,0x84,0x0c,0x77,0xc5,
    0x00,
};
static const uint8_t INF_DYN_P[270] = {
    0x54,0x68,0x65,0x20,0x71,0x75,0x69,0x63,0x6b,0x20,0x62,0x72,
    0x6f,0x77,0x6e,0x20,0x66,0x6f,0x78,0x20,0x6a,0x75,0x6d,0x70,
    0x73,0x20,0x6f,0x76,0x65,0x72,0x20,0x74,0x68,0x65,0x20,0x6c,
    0x61,0x7a,0x79,0x20,0x64,0x6f,0x67,0x2e,0x20,0x54,0x68,0x65,
    0x20,0x71,0x75,0x69,0x63,0x6b,0x20,0x62,0x72,0x6f,0x77,0x6e,
    0x20,0x66,0x6f,0x78,0x20,0x6a,0x75,0x6d,0x70,0x73,0x20,0x6f,
    0x76,0x65,0x72,0x20,0x74,0x68,0x65,0x20,0x6c,0x61,0x7a,0x79,
    0x20,0x64,0x6f,0x67,0x2e,0x20,0x54,0x68,0x65,0x20,0x71,0x75,
    0x69,0x63,0x6b,0x20,0x62,0x72,0x6f,0x77,0x6e,0x20,0x66,0x6f,
    0x78,0x20,0x6a,0x75,0x6d,0x70,0x73,0x20,0x6f,0x76,0x65,0x72,
    0x20,0x74,0x68,0x65,0x20,0x6c,0x61,0x7a,0x79,0x20,0x64,0x6f,
    0x67,0x2e,0x20,0x54,0x68,0x65,0x20,0x71,0x75,0x69,0x63,0x6b,
    0x20,0x62,0x72,0x6f,0x77,0x6e,0x20,0x66,0x6f,0x78,0x20,0x6a,
    0x75,0x6d,0x70,0x73,0x20,0x6f,0x76,0x65,0x72,0x20,0x74,0x68,
    0x65,0x20,0x6c,0x61,0x7a,0x79,0x20,0x64,0x6f,0x67,0x2e,0x20,
    0x54,0x68,0x65,0x20,0x71,0x75,0x69,0x63,0x6b,0x20,0x62,0x72,
    0x6f,0x77,0x6e,0x20,0x66,0x6f,0x78,0x20,0x6a,0x75,0x6d,0x70,
    0x73,0x20,0x6f,0x76,0x65,0x72,0x20,0x74,0x68,0x65,0x20,0x6c,
    0x61,0x7a,0x79,0x20,0x64,0x6f,0x67,0x2e,0x20,0x54,0x68,0x65,
    0x20,0x71,0x75,0x69,0x63,0x6b,0x20,0x62,0x72,0x6f,0x77,0x6e,
    0x20,0x66,0x6f,0x78,0x20,0x6a,0x75,0x6d,0x70,0x73,0x20,0x6f,
    0x76,0x65,0x72,0x20,0x74,0x68,0x65,0x20,0x6c,0x61,0x7a,0x79,
    0x20,0x64,0x6f,0x67,0x2e,0x20,
};
static const uint8_t INF_FIX_C[66] = {
    0xf3,0xab,0xac,0xf0,0x0f,0x56,0xc8,0xcc,0x4b,0xcb,0x49,0x2c,
    0x49,0xb5,0x52,0x48,0x54,0x48,0xcb,0xac,0x48,0x4d,0xd1,0xf5,
    0x28,0x4d,0x4b,0xcb,0x4d,0xcc,0x53,0x70,0x71,0x75,0xf3,0x71,
    0x0c,0x71,0x55,0x48,0xca,0xc9,0x4f,0xce,0xd6,0x51,0x48,0x49,
    0x4d,0xce,0x4f,0x49,0x4d,0x51,0x48,0xaa,0x2c,0x49,0x55,0x48,
    0xcb,0x2f,0x02,0x33,0xf4,0x00,
};
static const uint8_t INF_FIX_P[68] = {
    0x4e,0x79,0x78,0x4f,0x53,0x20,0x69,0x6e,0x66,0x6c,0x61,0x74,
    0x65,0x3a,0x20,0x61,0x20,0x66,0x69,0x78,0x65,0x64,0x2d,0x48,
    0x75,0x66,0x66,0x6d,0x61,0x6e,0x20,0x44,0x45,0x46,0x4c,0x41,
    0x54,0x45,0x20,0x62,0x6c,0x6f,0x63,0x6b,0x2c,0x20,0x64,0x65,
    0x63,0x6f,0x64,0x65,0x64,0x20,0x62,0x79,0x74,0x65,0x20,0x66,
    0x6f,0x72,0x20,0x62,0x79,0x74,0x65,0x2e,
};
static const uint8_t INF_STO_C[85] = {
    0x01,0x50,0x00,0xaf,0xff,0x0b,0x30,0x55,0x7a,0x9f,0xc4,0xe9,
    0x0e,0x33,0x58,0x7d,0xa2,0xc7,0xec,0x11,0x36,0x5b,0x80,0xa5,
    0xca,0xef,0x14,0x39,0x5e,0x83,0xa8,0xcd,0xf2,0x17,0x3c,0x61,
    0x86,0xab,0xd0,0xf5,0x1a,0x3f,0x64,0x89,0xae,0xd3,0xf8,0x1d,
    0x42,0x67,0x8c,0xb1,0xd6,0xfb,0x20,0x45,0x6a,0x8f,0xb4,0xd9,
    0xfe,0x23,0x48,0x6d,0x92,0xb7,0xdc,0x01,0x26,0x4b,0x70,0x95,
    0xba,0xdf,0x04,0x29,0x4e,0x73,0x98,0xbd,0xe2,0x07,0x2c,0x51,
    0x76,
};
static const uint8_t INF_STO_P[80] = {
    0x0b,0x30,0x55,0x7a,0x9f,0xc4,0xe9,0x0e,0x33,0x58,0x7d,0xa2,
    0xc7,0xec,0x11,0x36,0x5b,0x80,0xa5,0xca,0xef,0x14,0x39,0x5e,
    0x83,0xa8,0xcd,0xf2,0x17,0x3c,0x61,0x86,0xab,0xd0,0xf5,0x1a,
    0x3f,0x64,0x89,0xae,0xd3,0xf8,0x1d,0x42,0x67,0x8c,0xb1,0xd6,
    0xfb,0x20,0x45,0x6a,0x8f,0xb4,0xd9,0xfe,0x23,0x48,0x6d,0x92,
    0xb7,0xdc,0x01,0x26,0x4b,0x70,0x95,0xba,0xdf,0x04,0x29,0x4e,
    0x73,0x98,0xbd,0xe2,0x07,0x2c,0x51,0x76,
};
static const uint8_t INF_ZLIB_C[73] = {
    0x78,0xda,0xab,0xca,0xc9,0x4c,0xd2,0x2d,0x2f,0x4a,0x2c,0x28,
    0x48,0x4d,0x51,0x70,0x71,0x75,0xf3,0x71,0x0c,0x71,0x55,0x28,
    0xcf,0x2c,0xc9,0x50,0x48,0xcc,0x53,0x70,0x4c,0xc9,0x49,0x2d,
    0xd2,0x35,0x36,0x52,0x48,0xce,0x48,0x4d,0xce,0x2e,0x2e,0xcd,
    0xd5,0x51,0x48,0x2c,0x56,0x08,0xf0,0x73,0x57,0xf0,0x74,0x71,
    0x0c,0x51,0x28,0x2d,0x4e,0x2d,0xd6,0x03,0x00,0xd3,0x0e,0x15,
    0x34,
};
static const uint8_t INF_ZLIB_P[65] = {
    0x7a,0x6c,0x69,0x62,0x2d,0x77,0x72,0x61,0x70,0x70,0x65,0x64,
    0x20,0x44,0x45,0x46,0x4c,0x41,0x54,0x45,0x20,0x77,0x69,0x74,
    0x68,0x20,0x61,0x6e,0x20,0x41,0x64,0x6c,0x65,0x72,0x2d,0x33,
    0x32,0x20,0x63,0x68,0x65,0x63,0x6b,0x73,0x75,0x6d,0x2c,0x20,
    0x61,0x73,0x20,0x50,0x4e,0x47,0x20,0x49,0x44,0x41,0x54,0x20,
    0x75,0x73,0x65,0x73,0x2e,
};

static int inf_bufeq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int inflate_selftest(void) {
    uint8_t buf[512]; uint32_t olen; int pass = 0, total = 0, err;

    total++; err = inflate_raw(INF_DYN_C, sizeof(INF_DYN_C), buf, sizeof(buf), &olen);
    if (err == 0 && olen == sizeof(INF_DYN_P) && inf_bufeq(buf, INF_DYN_P, olen))
        { pass++; printf("inflate: dynamic-Huffman block PASS (%d bytes)\n", (int)olen); }
    else printf("inflate: dynamic FAIL (err=%d olen=%d)\n", err, (int)olen);

    total++; err = inflate_raw(INF_FIX_C, sizeof(INF_FIX_C), buf, sizeof(buf), &olen);
    if (err == 0 && olen == sizeof(INF_FIX_P) && inf_bufeq(buf, INF_FIX_P, olen))
        { pass++; printf("inflate: fixed-Huffman block PASS (%d bytes)\n", (int)olen); }
    else printf("inflate: fixed FAIL (err=%d olen=%d)\n", err, (int)olen);

    total++; err = inflate_raw(INF_STO_C, sizeof(INF_STO_C), buf, sizeof(buf), &olen);
    if (err == 0 && olen == sizeof(INF_STO_P) && inf_bufeq(buf, INF_STO_P, olen))
        { pass++; printf("inflate: stored block PASS (%d bytes)\n", (int)olen); }
    else printf("inflate: stored FAIL (err=%d olen=%d)\n", err, (int)olen);

    total++; err = zlib_inflate(INF_ZLIB_C, sizeof(INF_ZLIB_C), buf, sizeof(buf), &olen);
    if (err == 0 && olen == sizeof(INF_ZLIB_P) && inf_bufeq(buf, INF_ZLIB_P, olen))
        { pass++; printf("inflate: zlib-wrapped + Adler-32 PASS (%d bytes)\n", (int)olen); }
    else printf("inflate: zlib FAIL (err=%d olen=%d)\n", err, (int)olen);

    total++; {                                               // a corrupted Adler-32 must be rejected
        uint8_t bad[128]; uint32_t i;
        for (i = 0; i < sizeof(INF_ZLIB_C); i++) bad[i] = INF_ZLIB_C[i];
        bad[sizeof(INF_ZLIB_C) - 1] ^= 0x01;
        err = zlib_inflate(bad, sizeof(INF_ZLIB_C), buf, sizeof(buf), &olen);
        if (err != 0) { pass++; printf("inflate: corrupt Adler-32 rejected PASS (err=%d)\n", err); }
        else printf("inflate: corrupt Adler-32 NOT rejected FAIL\n");
    }

    printf("inflate: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
