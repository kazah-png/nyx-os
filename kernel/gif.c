// ============================================================
// gif.c - a minimal GIF (87a/89a) image decoder
// ============================================================
// Decodes the FIRST image of a GIF: header + Logical Screen Descriptor, an optional Global Color
// Table, a Graphic Control Extension (for the transparent color index), and the first Image
// Descriptor — LZW-decompressing its indices, de-interlacing if needed, and mapping through the
// palette to RGBA. Animation (later frames), and features beyond the first frame, are ignored.
#include "kernel.h"
#include "gif.h"

#define GIF_MAXCODES 4096
#define GIF_MAX_DIM  4096
#define GIF_MAX_PX   (1u << 20)

// LSB-first bit reader over a contiguous LZW byte buffer (the concatenated image sub-blocks).
typedef struct { const uint8_t* d; uint32_t len, pos; int bitbuf, bitcnt; } gif_bits;
static int gif_getcode(gif_bits* b, int nbits) {
    while (b->bitcnt < nbits) {
        if (b->pos >= b->len) return -1;
        b->bitbuf |= (int)b->d[b->pos++] << b->bitcnt;
        b->bitcnt += 8;
    }
    int v = b->bitbuf & ((1 << nbits) - 1);
    b->bitbuf >>= nbits; b->bitcnt -= nbits;
    return v;
}

// Decode an LZW-compressed GIF image-data stream into `out` (outcap palette indices). 0 on success.
static int gif_lzw(const uint8_t* data, uint32_t dlen, int mcs, uint8_t* out, uint32_t outcap) {
    gif_bits b = { data, dlen, 0, 0, 0 };
    uint16_t prefix[GIF_MAXCODES];
    uint8_t  suffix[GIF_MAXCODES];
    uint8_t  stack[GIF_MAXCODES];
    int clear = 1 << mcs, eoi = clear + 1;
    int codesize = mcs + 1, freecode = clear + 2, prev = -1, first = 0;
    uint32_t o = 0, i;
    for (i = 0; i < (uint32_t)clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
    for (;;) {
        int code = gif_getcode(&b, codesize);
        if (code < 0) break;
        if (code == eoi) break;
        if (code == clear) { codesize = mcs + 1; freecode = clear + 2; prev = -1; continue; }
        if (prev == -1) {                                   // first code after a clear = a literal
            if (code >= clear) return -1;
            first = code;
            if (o < outcap) out[o++] = (uint8_t)code;
            prev = code; continue;
        }
        int sp = 0, c = code;
        if (c >= freecode) { stack[sp++] = (uint8_t)first; c = prev; }   // KwKwK special case
        while (c >= clear) { if (sp >= GIF_MAXCODES) return -2; stack[sp++] = suffix[c]; c = prefix[c]; }
        first = suffix[c];
        stack[sp++] = (uint8_t)first;
        while (sp > 0) { uint8_t v = stack[--sp]; if (o < outcap) out[o++] = v; }
        if (freecode < GIF_MAXCODES) { prefix[freecode] = (uint16_t)prev; suffix[freecode] = (uint8_t)first; freecode++; }
        if (freecode == (1 << codesize) && codesize < 12) codesize++;
        prev = code;
    }
    return (o == outcap) ? 0 : -3;
}

int gif_decode(const uint8_t* src, uint32_t srclen, image_t* img) {
    if (srclen < 13) return -1;
    if (src[0] != 'G' || src[1] != 'I' || src[2] != 'F') return -2;
    uint8_t packed = src[10];
    int gct_flag = packed >> 7;
    int gct_n = 2 << (packed & 7);                          // 2^(size+1) entries
    uint32_t p = 13;
    const uint8_t* gct = 0;
    if (gct_flag) { if (p + (uint32_t)gct_n * 3 > srclen) return -3; gct = src + p; p += gct_n * 3; }
    int transparent = -1;

    while (p < srclen) {
        uint8_t blk = src[p++];
        if (blk == 0x3B) break;                             // trailer
        if (blk == 0x21) {                                  // extension
            if (p >= srclen) return -4;
            uint8_t label = src[p++];
            if (label == 0xF9 && p + 5 <= srclen && src[p] == 4) {      // Graphic Control Extension
                if (src[p + 1] & 1) transparent = src[p + 4];           // transparency flag -> color index
            }
            while (p < srclen) { uint8_t sz = src[p++]; if (sz == 0) break; p += sz; }   // skip sub-blocks
            continue;
        }
        if (blk != 0x2C) return -5;                         // expected an image descriptor
        if (p + 9 > srclen) return -6;
        uint32_t fw = src[p + 4] | ((uint32_t)src[p + 5] << 8);
        uint32_t fh = src[p + 6] | ((uint32_t)src[p + 7] << 8);
        uint8_t ip = src[p + 8];
        p += 9;
        int lct_flag = ip >> 7, interlace = (ip >> 6) & 1, lct_n = 2 << (ip & 7);
        const uint8_t* ct = gct; int ctn = gct_n;
        if (lct_flag) { if (p + (uint32_t)lct_n * 3 > srclen) return -7; ct = src + p; ctn = lct_n; p += lct_n * 3; }
        if (!ct) return -8;
        if (fw == 0 || fh == 0 || fw > GIF_MAX_DIM || fh > GIF_MAX_DIM || (uint64_t)fw * fh > GIF_MAX_PX) return -9;
        if (p >= srclen) return -10;
        int mcs = src[p++];
        if (mcs < 2 || mcs > 8) return -11;

        uint32_t total = 0, q = p;                          // measure the LZW sub-block chain
        while (q < srclen) { uint8_t sz = src[q++]; if (sz == 0) break; total += sz; q += sz; }
        uint8_t* lzw = (uint8_t*)kmalloc(total ? total : 1);
        uint8_t* idx = (uint8_t*)kmalloc((uint64_t)fw * fh);
        uint8_t* px  = (uint8_t*)kmalloc((uint64_t)fw * fh * 4);
        if (!lzw || !idx || !px) { if (lzw) kfree(lzw); if (idx) kfree(idx); if (px) kfree(px); return -12; }
        { uint32_t o = 0, r = p; while (r < srclen) { uint8_t sz = src[r++]; if (sz == 0) break;
              uint32_t k; for (k = 0; k < sz && r < srclen; k++) lzw[o++] = src[r++]; } }
        int rc = gif_lzw(lzw, total, mcs, idx, fw * fh);
        kfree(lzw);
        if (rc != 0) { kfree(idx); kfree(px); return -13; }

        // Map palette indices to RGBA, de-interlacing the row order if the image is interlaced.
        static const int starts[4] = {0, 4, 2, 1}, steps[4] = {8, 8, 4, 2};
        uint32_t dy = 0, pass, y, x;
        for (pass = 0; pass < (interlace ? 4u : 1u); pass++) {
            for (y = interlace ? (uint32_t)starts[pass] : 0; y < fh; y += interlace ? (uint32_t)steps[pass] : 1) {
                const uint8_t* srow = idx + (uint64_t)dy * fw;
                uint8_t* orow = px + (uint64_t)y * fw * 4;
                for (x = 0; x < fw; x++) {
                    uint32_t ci = srow[x];
                    uint8_t* o = orow + x * 4;
                    if (ci < (uint32_t)ctn) { o[0] = ct[ci*3]; o[1] = ct[ci*3+1]; o[2] = ct[ci*3+2]; }
                    else { o[0] = o[1] = o[2] = 0; }
                    o[3] = (transparent >= 0 && (int)ci == transparent) ? 0 : 255;
                }
                dy++;
            }
        }
        kfree(idx);
        img->width = fw; img->height = fh; img->pixels = px;
        return 0;
    }
    return -14;                                             // no image descriptor found
}

// ---- known-answer self-test (`giftest`) ----
#include "gif_vectors.h"

static int gif_bufeq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    uint32_t i; for (i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1;
}
static int gif_case(const char* name, const uint8_t* file, uint32_t flen,
                    uint32_t w, uint32_t h, const uint8_t* want) {
    image_t im; int rc = gif_decode(file, flen, &im);
    int ok = (rc == 0 && im.width == w && im.height == h && gif_bufeq(im.pixels, want, w * h * 4));
    if (rc == 0 && im.pixels) kfree(im.pixels);
    if (ok) printf("gif: %s PASS (%dx%d)\n", name, (int)w, (int)h);
    else    printf("gif: %s FAIL (rc=%d)\n", name, rc);
    return ok;
}

int gif_selftest(void) {
    int pass = 0, total = 0;
    total++; pass += gif_case("basic",      GIF_BASIC,     sizeof(GIF_BASIC),     GIF_BASIC_W,     GIF_BASIC_H,     GIF_BASIC_RGBA);
    total++; pass += gif_case("transparency",GIF_TRNS,     sizeof(GIF_TRNS),      GIF_TRNS_W,      GIF_TRNS_H,      GIF_TRNS_RGBA);
    total++; pass += gif_case("interlaced", GIF_INTERLACE, sizeof(GIF_INTERLACE), GIF_INTERLACE_W, GIF_INTERLACE_H, GIF_INTERLACE_RGBA);
    total++; pass += gif_case("dict-growth",GIF_BIG,       sizeof(GIF_BIG),       GIF_BIG_W,       GIF_BIG_H,       GIF_BIG_RGBA);
    printf("gif: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
