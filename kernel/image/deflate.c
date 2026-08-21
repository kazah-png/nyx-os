// ============================================================
// deflate.c - DEFLATE (RFC 1951) + zlib (RFC 1950) compression
// ============================================================
// The encoder half of inflate.c: a fixed-Huffman + greedy LZ77 compressor. One final fixed-
// Huffman block covers the whole input; matches come from a 32K hash-chain window. Output is
// validated the strongest way possible - it round-trips through NyxOS's own inflate.c (see
// deflate_selftest) and is byte-for-byte accepted by stock zlib (host-verified against Python
// zlib.decompress across empty/boundary/incompressible/fuzz inputs before landing).
#include "../core/kernel.h"
#include "deflate.h"
#include "inflate.h"

// ---- fixed-Huffman lit/length code (RFC 1951 3.2.6) ----
static void fixed_litlen(int s, uint32_t* code, int* len) {
    if (s <= 143)      { *len = 8; *code = 0x30 + s; }
    else if (s <= 255) { *len = 9; *code = 0x190 + (s - 144); }
    else if (s <= 279) { *len = 7; *code = 0x00 + (s - 256); }
    else               { *len = 8; *code = 0xC0 + (s - 280); }
}

// length/distance base+extra tables (RFC 1951 3.2.5)
static const int len_base[29]  = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int len_extra[29] = {0,0,0,0,0,0,0,0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0};
static const int dist_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int dist_extra[30]= {0,0,0,0,1,1,2,2,3, 3, 4, 4, 5, 5, 6,  6,  7,  7,  8,  8,   9,   9,  10,  10,  11,  11,  12,   12,   13,   13};

// ---- bit writer: DEFLATE packs bits LSB-first within each byte ----
typedef struct {
    uint8_t* out; uint32_t cap, len;
    uint32_t acc; int nbits;
    int overflow;
} bw_t;

static void bw_put(bw_t* w, uint32_t val, int n) {
    if (n <= 0) return;
    w->acc |= (val & ((n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1))) << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8) {
        if (w->len < w->cap) w->out[w->len] = (uint8_t)(w->acc & 0xFF);
        else w->overflow = 1;
        w->len++;
        w->acc >>= 8;
        w->nbits -= 8;
    }
}
static uint32_t rev_bits(uint32_t v, int n) {
    uint32_t r = 0; for (int i = 0; i < n; i++) { r = (r << 1) | (v & 1); v >>= 1; } return r;
}
// Huffman codes are transmitted most-significant-bit first, so reverse before LSB packing.
static void bw_code(bw_t* w, uint32_t code, int len) { bw_put(w, rev_bits(code, len), len); }
static void bw_flush(bw_t* w) { if (w->nbits > 0) bw_put(w, 0, 8 - w->nbits); }

static void emit_litlen(bw_t* w, int sym) { uint32_t c; int l; fixed_litlen(sym, &c, &l); bw_code(w, c, l); }

static void emit_match(bw_t* w, int len, int dist) {
    int i = 28; while (i > 0 && len_base[i] > len) i--;
    emit_litlen(w, 257 + i);
    if (len_extra[i]) bw_put(w, (uint32_t)(len - len_base[i]), len_extra[i]);
    int j = 29; while (j > 0 && dist_base[j] > dist) j--;
    bw_code(w, (uint32_t)j, 5);                              // fixed 5-bit distance code, MSB-first
    if (dist_extra[j]) bw_put(w, (uint32_t)(dist - dist_base[j]), dist_extra[j]);
}

// ---- greedy LZ77 with a 32K hash-chain window ----
#define WSIZE    32768
#define WMASK    (WSIZE - 1)
#define HBITS    15
#define HSIZE    (1 << HBITS)
#define HMASK    (HSIZE - 1)
#define MINLEN   3
#define MAXLEN   258
#define MAXCHAIN 256

// One compressor runs at a time (command / selftest, never concurrently), so the match window
// lives in .bss rather than the heap: 32K*4 (chain heads) + 32K*4 (prev links) = 256 KB.
static int32_t d_head[HSIZE];
static int32_t d_prev[WSIZE];

static int hash3(const uint8_t* p) {
    return (int)((((uint32_t)p[0] << 10) ^ ((uint32_t)p[1] << 5) ^ (uint32_t)p[2]) & HMASK);
}
static int match_len(const uint8_t* a, const uint8_t* b, int max) {
    int n = 0; while (n < max && a[n] == b[n]) n++; return n;
}

int deflate_raw(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen) {
    bw_t w; w.out = dst; w.cap = dstcap; w.len = 0; w.acc = 0; w.nbits = 0; w.overflow = 0;
    for (int i = 0; i < HSIZE; i++) d_head[i] = -1;

    bw_put(&w, 1, 1);        // BFINAL = 1
    bw_put(&w, 1, 2);        // BTYPE  = 01 (fixed Huffman)

    uint32_t pos = 0;
    while (pos < srclen) {
        int best_len = 0, best_dist = 0;
        if (pos + MINLEN <= srclen) {
            int h = hash3(src + pos);
            int cand = d_head[h];
            int chain = 0;
            int max = (int)(srclen - pos); if (max > MAXLEN) max = MAXLEN;
            while (cand >= 0 && (int)(pos - (uint32_t)cand) <= WSIZE && chain < MAXCHAIN) {
                int l = match_len(src + cand, src + pos, max);
                if (l > best_len) { best_len = l; best_dist = (int)(pos - (uint32_t)cand); if (l >= max) break; }
                cand = d_prev[cand & WMASK];
                chain++;
            }
            d_prev[pos & WMASK] = d_head[h];
            d_head[h] = (int)pos;
        }
        if (best_len >= MINLEN) {
            emit_match(&w, best_len, best_dist);
            for (int k = 1; k < best_len; k++) {            // register the covered positions
                uint32_t p = pos + (uint32_t)k;
                if (p + MINLEN <= srclen) {
                    int h = hash3(src + p);
                    d_prev[p & WMASK] = d_head[h];
                    d_head[h] = (int)p;
                }
            }
            pos += (uint32_t)best_len;
        } else {
            emit_litlen(&w, src[pos]);
            pos++;
        }
    }
    emit_litlen(&w, 256);    // end of block
    bw_flush(&w);
    if (w.overflow) return -1;
    *outlen = w.len;
    return 0;
}

static uint32_t d_adler32(const uint8_t* d, uint32_t n) {
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

int zlib_deflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen) {
    if (dstcap < 6) return -1;
    dst[0] = 0x78; dst[1] = 0x01;                          // CMF/FLG (%31==0, 32K window, FLEVEL=0)
    uint32_t body = 0;
    int r = deflate_raw(src, srclen, dst + 2, dstcap - 6, &body);
    if (r != 0) return r;
    uint32_t ad = d_adler32(src, srclen);
    uint32_t o = 2 + body;
    dst[o+0] = (uint8_t)(ad >> 24); dst[o+1] = (uint8_t)(ad >> 16);
    dst[o+2] = (uint8_t)(ad >> 8);  dst[o+3] = (uint8_t)(ad);
    *outlen = o + 4;
    return 0;
}

// gzip framing (RFC 1952) over the raw DEFLATE body: 10-byte header + CRC-32 + ISIZE trailer. The
// output is exactly what stock `gunzip` / Python gzip.decompress read (host-verified against both).
// crc32_calc is the standard reflected CRC-32 (poly 0xEDB88320) — the same one PNG uses and the
// crc32 KAT validates.
extern uint32_t crc32_calc(const uint8_t* data, uint32_t len);
int gzip_deflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen) {
    if (dstcap < 18) return -1;                                   // 10 header + >=0 body + 8 trailer
    dst[0] = 0x1f; dst[1] = 0x8b; dst[2] = 8; dst[3] = 0;         // magic, CM=deflate, FLG=0
    dst[4] = dst[5] = dst[6] = dst[7] = 0;                        // MTIME=0
    dst[8] = 0; dst[9] = 0xFF;                                    // XFL=0, OS=unknown
    uint32_t body = 0;
    int r = deflate_raw(src, srclen, dst + 10, dstcap - 18, &body);
    if (r != 0) return r;
    uint32_t crc = crc32_calc(src, srclen), o = 10 + body;
    dst[o+0] = (uint8_t)crc;    dst[o+1] = (uint8_t)(crc >> 8);    dst[o+2] = (uint8_t)(crc >> 16);    dst[o+3] = (uint8_t)(crc >> 24);
    dst[o+4] = (uint8_t)srclen; dst[o+5] = (uint8_t)(srclen >> 8); dst[o+6] = (uint8_t)(srclen >> 16); dst[o+7] = (uint8_t)(srclen >> 24);
    *outlen = o + 8;
    return 0;
}

// Decompress a gzip stream (.gz, RFC 1952) — the complement of gzip_deflate, so NyxOS can now
// READ the .gz files it (and stock gzip) write. Parses the 10-byte header plus any optional
// FEXTRA/FNAME/FCOMMENT/FHCRC fields, inflates the raw DEFLATE body via inflate.c, then verifies
// the CRC-32 and ISIZE trailer. Returns 0 and sets *outlen on success; a distinct negative code
// on a short/mangled header, non-DEFLATE method, truncation, inflate failure, or a size/CRC
// mismatch — so a corrupted or non-gzip file is REJECTED, never silently mis-decoded.
int gzip_inflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen) {
    if (srclen < 18) return -1;                                   // 10 header + >=0 body + 8 trailer
    if (src[0] != 0x1f || src[1] != 0x8b) return -2;              // bad magic -> not a gzip file
    if (src[2] != 8) return -3;                                   // only CM=8 (DEFLATE) is defined
    uint8_t flg = src[3];
    uint32_t p = 10;                                              // past the fixed 10-byte header
    if (flg & 0x04) {                                             // FEXTRA: XLEN(2) + XLEN bytes
        if (p + 2 > srclen) return -4;
        uint32_t xlen = (uint32_t)src[p] | ((uint32_t)src[p + 1] << 8);
        p += 2 + xlen;
        if (p > srclen) return -4;
    }
    if (flg & 0x08) { while (p < srclen && src[p]) p++; if (p >= srclen) return -5; p++; }  // FNAME (NUL-term)
    if (flg & 0x10) { while (p < srclen && src[p]) p++; if (p >= srclen) return -5; p++; }  // FCOMMENT (NUL-term)
    if (flg & 0x02) { p += 2; if (p > srclen) return -4; }        // FHCRC: 2-byte header CRC16
    if (p + 8 > srclen) return -6;                                // no room for a body + trailer
    uint32_t bodylen = srclen - p - 8;                            // DEFLATE body sits between header and trailer
    uint32_t olen = 0;
    if (inflate_raw(src + p, bodylen, dst, dstcap, &olen) != 0) return -7;
    uint32_t tcrc  = (uint32_t)src[srclen - 8] | ((uint32_t)src[srclen - 7] << 8) |
                     ((uint32_t)src[srclen - 6] << 16) | ((uint32_t)src[srclen - 5] << 24);
    uint32_t tsize = (uint32_t)src[srclen - 4] | ((uint32_t)src[srclen - 3] << 8) |
                     ((uint32_t)src[srclen - 2] << 16) | ((uint32_t)src[srclen - 1] << 24);
    if (olen != tsize) return -8;                                 // ISIZE (uncompressed length) mismatch
    if (crc32_calc(dst, olen) != tcrc) return -9;                 // CRC-32 mismatch -> corrupted data
    if (outlen) *outlen = olen;
    return 0;
}

// ---- known-answer self-test: compress, then decompress with NyxOS's own inflate.c ----
static int d_bytes_equal(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int deflate_selftest(void) {
    static uint8_t in[2048], comp[4096], back[2048];
    // A spread: empty, tiny, a long run (very compressible), and structured text.
    for (int t = 0; t < 4; t++) {
        uint32_t n = 0;
        int compressible = 0;
        if (t == 0) { n = 0; }
        else if (t == 1) { const char* s = "abc"; while (in[n] = (uint8_t)s[n], s[n]) n++; }
        else if (t == 2) { n = 1500; for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t)('A' + (i % 3)); compressible = 1; }
        else { const char* s = "the quick brown fox jumps over the lazy dog. ";
               for (int r = 0; r < 30; r++) for (int k = 0; s[k]; k++) in[n++] = (uint8_t)s[k];
               compressible = 1; }

        // raw DEFLATE round-trip through inflate_raw
        uint32_t clen = 0, blen = 0;
        if (deflate_raw(in, n, comp, sizeof comp, &clen) != 0) return 10 + t;
        if (inflate_raw(comp, clen, back, sizeof back, &blen) != 0) return 20 + t;
        if (blen != n || !d_bytes_equal(in, back, n)) return 30 + t;

        // zlib framing round-trip through zlib_inflate (also verifies the Adler-32 trailer)
        if (zlib_deflate(in, n, comp, sizeof comp, &clen) != 0) return 40 + t;
        if (zlib_inflate(comp, clen, back, sizeof back, &blen) != 0) return 50 + t;
        if (blen != n || !d_bytes_equal(in, back, n)) return 60 + t;

        // compressible inputs must actually shrink (zlib output smaller than the input)
        if (compressible && clen >= n) return 70 + t;
    }
    return 0;
}

// gzip KAT: build a .gz, check the RFC-1952 framing + CRC-32 + ISIZE trailer, and inflate the
// DEFLATE body back byte-exact via NyxOS's own inflate_raw. (Host-verified that the same output
// decompresses under stock gunzip / Python gzip.)
int gzip_selftest(void) {
    static uint8_t in[2048], comp[4096], back[2048];
    for (int t = 0; t < 3; t++) {
        uint32_t n = 0;
        if (t == 0) { const char* s = "abc"; while (in[n] = (uint8_t)s[n], s[n]) n++; }
        else if (t == 1) { n = 1500; for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t)('A' + (i % 3)); }
        else { const char* s = "the quick brown fox jumps over the lazy dog. ";
               for (int r = 0; r < 30; r++) for (int k = 0; s[k]; k++) in[n++] = (uint8_t)s[k]; }
        uint32_t glen = 0;
        if (gzip_deflate(in, n, comp, sizeof comp, &glen) != 0) return 10 + t;
        if (glen < 18 || comp[0] != 0x1f || comp[1] != 0x8b || comp[2] != 8) return 20 + t;
        uint32_t crc = crc32_calc(in, n);
        uint32_t tc = comp[glen-8] | (comp[glen-7] << 8) | (comp[glen-6] << 16) | ((uint32_t)comp[glen-5] << 24);
        if (tc != crc) return 30 + t;                                     // CRC-32 trailer (LE)
        uint32_t is = comp[glen-4] | (comp[glen-3] << 8) | (comp[glen-2] << 16) | ((uint32_t)comp[glen-1] << 24);
        if (is != n) return 40 + t;                                       // ISIZE trailer (LE)
        uint32_t blen = 0;
        if (inflate_raw(comp + 10, glen - 18, back, sizeof back, &blen) != 0) return 50 + t;
        if (blen != n || !d_bytes_equal(in, back, n)) return 60 + t;       // body round-trips
    }
    return 0;
}

// gzip_inflate KAT: exercise the full DECODE path (header parse + trailer verify), not just the
// body. Round-trips 3 inputs through gzip_deflate -> gzip_inflate, and requires rejection of a
// non-gzip file (bad magic) and of a stream with a corrupted CRC-32 trailer. 0 = pass.
int gzip_inflate_selftest(void) {
    static uint8_t in[2048], gz[4096], back[2048];
    for (int t = 0; t < 3; t++) {
        uint32_t n = 0;
        if (t == 0) { const char* s = "hello gzip world\n"; while (in[n] = (uint8_t)s[n], s[n]) n++; }
        else if (t == 1) { n = 1000; for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t)('a' + (i % 7)); }
        else { n = 700; for (uint32_t i = 0; i < n; i++) in[i] = (uint8_t)(i * 31 + 7); }  // low-compressibility
        uint32_t glen = 0, blen = 0;
        if (gzip_deflate(in, n, gz, sizeof gz, &glen) != 0) return 10 + t;
        if (gzip_inflate(gz, glen, back, sizeof back, &blen) != 0) return 20 + t;
        if (blen != n || !d_bytes_equal(in, back, n)) return 30 + t;
    }
    // must REJECT a non-gzip file (bad magic)
    static uint8_t junk[32];
    for (int i = 0; i < 32; i++) junk[i] = (uint8_t)i;
    uint32_t jl = 0;
    if (gzip_inflate(junk, sizeof junk, back, sizeof back, &jl) == 0) return 50;
    // must REJECT a stream with a corrupted CRC-32 trailer
    uint32_t clen = 0, dlen = 0, m = 0;
    const char* s = "abcabcabcabc";
    while (in[m] = (uint8_t)s[m], s[m]) m++;
    if (gzip_deflate(in, m, gz, sizeof gz, &clen) != 0) return 60;
    gz[clen - 8] ^= 0xFF;                                             // flip a CRC-32 byte
    if (gzip_inflate(gz, clen, back, sizeof back, &dlen) == 0) return 61;
    return 0;
}
