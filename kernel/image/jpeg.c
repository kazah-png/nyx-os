// ============================================================
// jpeg.c - a baseline (sequential DCT, Huffman) JPEG decoder
// ============================================================
// Decodes the common web JPEG: SOI/APPn/DQT/SOF0/DHT/DRI/SOS markers, Huffman-coded 8x8 blocks
// (DC diff + run/size AC), dequantise, an 8x8 separable inverse-DCT, chroma upsampling and
// YCbCr->RGB, into an RGBA image_t. Grayscale (1 component) and YCbCr (3 components, 4:4:4 / 4:2:2
// / 4:2:0) with restart markers. Progressive / arithmetic / 12-bit / 4-component are rejected.
#include "../core/kernel.h"
#include "jpeg.h"

#define JPG_MAX_DIM 4096
#define JPG_MAX_PX  (1u << 21)      // 2 Mpx cap (output RGBA + planes stay bounded)

// zig-zag scan index -> natural (row-major) 8x8 position; DQT values and coefficients arrive zig-zagged.
static const uint8_t ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10, 17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34, 27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36, 29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46, 53,60,61,54,47,55,62,63
};

// Separable IDCT basis in FIXED POINT (the kernel is built -mno-sse, so no float): the coefficient
// IDCT_M[x][u] = Cu*cos((2x+1)u*pi/16) is stored scaled by 2^13. Two int64 passes give a value scaled
// by 2^26; a >>28 (that's 2*13 + the two 0.5 normalisation factors) recovers the spatial sample.
#define IDCT_SHIFT 13
static const int IDCT_MI[8][8] = {   // round(Cu*cos((2x+1)u*pi/16) * 8192)
    {   5793,   8035,   7568,   6811,   5793,   4551,   3135,   1598 },
    {   5793,   6811,   3135,  -1598,  -5793,  -8035,  -7568,  -4551 },
    {   5793,   4551,  -3135,  -8035,  -5793,   1598,   7568,   6811 },
    {   5793,   1598,  -7568,  -4551,   5793,   6811,  -3135,  -8035 },
    {   5793,  -1598,  -7568,   4551,   5793,  -6811,  -3135,   8035 },
    {   5793,  -4551,  -3135,   8035,  -5793,  -1598,   7568,  -6811 },
    {   5793,  -6811,   3135,   1598,  -5793,   8035,  -7568,   4551 },
    {   5793,  -8035,   7568,  -6811,   5793,  -4551,   3135,  -1598 },
};

typedef struct { int mincode[17], maxcode[17], valptr[17]; uint8_t huffval[256]; } jhuff;
typedef struct { int id, h, v, tq, td, ta, pred; } jcomp;
typedef struct {
    uint16_t qt[4][64];                  // quant tables (zig-zag order)
    jhuff hdc[4], hac[4];
    int W, H, ncomp, restart;
    jcomp comp[3];
} jstate;

// ---- entropy bit reader: MSB-first, with 0xFF00 de-stuffing + marker detection ----
typedef struct { const uint8_t* d; uint32_t len, pos; uint32_t buf; int cnt; int marker; } jbr;

static int jpeg_bit(jbr* b) {
    if (b->cnt == 0) {
        if (b->marker || b->pos >= b->len) return 0;      // stalled at a marker / out of data -> feed zeros
        uint8_t c = b->d[b->pos++];
        if (c == 0xFF) {
            uint8_t m = (b->pos < b->len) ? b->d[b->pos] : 0xD9;
            if (m == 0x00) { b->pos++; }                  // stuffed 0xFF00 -> a literal 0xFF byte
            else { b->marker = m; b->buf = 0; b->cnt = 8; return 0; }   // real marker: stall
        }
        b->buf = c; b->cnt = 8;
    }
    b->cnt--;
    return (int)((b->buf >> b->cnt) & 1);
}
static int jpeg_receive(jbr* b, int s) { int v = 0; while (s-- > 0) v = (v << 1) | jpeg_bit(b); return v; }
// JPEG EXTEND (T.81 F.12): sign-extend an s-bit magnitude. Uses -(1<<s) rather than (-1<<s) — the
// latter is left-shift of a negative value (undefined behaviour); the two are numerically identical.
static int jpeg_extend(int v, int s) { return (v < (1 << (s - 1))) ? v - (1 << s) + 1 : v; }

// Byte-align and consume the next restart marker (FF D0..D7); resets the bit buffer.
static void jpeg_restart(jbr* b) {
    b->cnt = 0;
    if (b->marker >= 0xD0 && b->marker <= 0xD7) { b->pos++; b->marker = 0; return; }
    while (b->pos + 1 < b->len) {
        if (b->d[b->pos] == 0xFF && b->d[b->pos+1] >= 0xD0 && b->d[b->pos+1] <= 0xD7) { b->pos += 2; b->marker = 0; return; }
        b->pos++;
    }
    b->marker = 0;
}

static int jpeg_huff(jbr* b, const jhuff* h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | jpeg_bit(b);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l])
            return h->huffval[h->valptr[l] + code - h->mincode[l]];
    }
    return -1;
}

// Inverse-DCT a dequantised (natural-order) block into level-shifted, clamped 8x8 samples. Fixed
// point: two int64 passes (rows then columns) accumulate a value scaled by 2^(2*IDCT_SHIFT); the
// final >>(2*IDCT_SHIFT+2) applies both 0.5 normalisation factors, then +128 level shift.
static void jpeg_idct(const int* in, uint8_t* out) {
    long tmp[64];
    for (int y = 0; y < 8; y++)                          // rows
        for (int x = 0; x < 8; x++) {
            long s = 0;
            for (int u = 0; u < 8; u++) s += (long)IDCT_MI[x][u] * in[y*8 + u];
            tmp[y*8 + x] = s;
        }
    const int sh = 2 * IDCT_SHIFT + 2;
    const long half = 1L << (sh - 1);
    for (int x = 0; x < 8; x++)                          // columns
        for (int y = 0; y < 8; y++) {
            long s = 0;
            for (int v = 0; v < 8; v++) s += (long)IDCT_MI[y][v] * tmp[v*8 + x];
            long val = ((s + half) >> sh) + 128;
            out[y*8 + x] = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);
        }
}

// Decode one 8x8 block: DC (predictor + diff), then run/size AC; dequantise into natural order.
static int jpeg_block(jbr* b, jstate* st, jcomp* c, int* out) {
    for (int i = 0; i < 64; i++) out[i] = 0;
    const uint16_t* q = st->qt[c->tq];
    int t = jpeg_huff(b, &st->hdc[c->td]);
    if (t < 0 || t > 15) return -1;
    int diff = t ? jpeg_extend(jpeg_receive(b, t), t) : 0;
    c->pred += diff;
    out[0] = c->pred * (int)q[0];
    int k = 1;
    while (k < 64) {
        int rs = jpeg_huff(b, &st->hac[c->ta]);
        if (rs < 0) return -1;
        int r = rs >> 4, s = rs & 15;
        if (s == 0) { if (r == 15) { k += 16; continue; } break; }   // ZRL skips 16 zeros; else EOB
        k += r;
        if (k >= 64) break;
        int coeff = jpeg_extend(jpeg_receive(b, s), s);
        out[ZIGZAG[k]] = coeff * (int)q[k];
        k++;
    }
    return 0;
}

// ---- marker-segment parsers ----
static int jpeg_dqt(jstate* st, const uint8_t* p, uint32_t seglen) {
    uint32_t i = 0;
    while (i < seglen) {
        uint8_t pq = p[i] >> 4, tq = p[i] & 15; i++;
        if (tq > 3 || pq != 0) return -1;               // 8-bit tables only
        if (i + 64 > seglen) return -1;
        for (int k = 0; k < 64; k++) st->qt[tq][k] = p[i + k];
        i += 64;
    }
    return 0;
}
static int jpeg_dht(jstate* st, const uint8_t* p, uint32_t seglen) {
    uint32_t i = 0;
    while (i < seglen) {
        uint8_t tc = p[i] >> 4, th = p[i] & 15; i++;
        if (th > 3 || tc > 1 || i + 16 > seglen) return -1;
        int bits[17], total = 0;
        for (int l = 1; l <= 16; l++) { bits[l] = p[i + l - 1]; total += bits[l]; }
        i += 16;
        if (total > 256 || i + (uint32_t)total > seglen) return -1;
        jhuff* h = tc ? &st->hac[th] : &st->hdc[th];
        for (int k = 0; k < total; k++) h->huffval[k] = p[i + k];
        i += total;
        int code = 0, kk = 0;                            // build min/max/valptr
        for (int l = 1; l <= 16; l++) {
            if (bits[l]) { h->valptr[l] = kk; h->mincode[l] = code; code += bits[l]; kk += bits[l]; h->maxcode[l] = code - 1; }
            else h->maxcode[l] = -1;
            code <<= 1;
        }
    }
    return 0;
}

// "Fancy" (triangle-filter) chroma upsampling, matching libjpeg's jdsample.c so decoded 4:2:x photos
// come out as smooth as a reference decoder rather than blocky. Horizontal 2x: each output sample is
// 3/4 of the nearer source sample + 1/4 of the farther (edges copy). h2v2 does the same separably in
// both axes (a 9:3:3:1 weighting), with the row's neighbour taken above for the top output row of a
// source row and below for the bottom one; image-edge rows/cols replicate.
static void jpeg_fancy_h2v1(const uint8_t* src, int sw, int sh, uint8_t* dst) {
    int dw = 2 * sw;
    for (int y = 0; y < sh; y++) {
        const uint8_t* r = src + (uint64_t)y * sw;
        uint8_t* o = dst + (uint64_t)y * dw;
        if (sw == 1) { o[0] = r[0]; o[1] = r[0]; continue; }
        o[0] = r[0];
        o[1] = (uint8_t)((r[0] * 3 + r[1] + 2) >> 2);
        for (int i = 1; i < sw - 1; i++) {
            o[2*i]     = (uint8_t)((r[i] * 3 + r[i-1] + 1) >> 2);
            o[2*i + 1] = (uint8_t)((r[i] * 3 + r[i+1] + 2) >> 2);
        }
        o[2*(sw-1)]     = (uint8_t)((r[sw-1] * 3 + r[sw-2] + 1) >> 2);
        o[2*(sw-1) + 1] = r[sw-1];
    }
}
static void jpeg_fancy_h2v2(const uint8_t* src, int sw, int sh, uint8_t* dst) {
    int dw = 2 * sw;
    for (int oy = 0; oy < 2 * sh; oy++) {
        int sy = oy >> 1, ny = (oy & 1) ? sy + 1 : sy - 1;
        if (ny < 0) ny = 0; else if (ny >= sh) ny = sh - 1;
        const uint8_t* r0 = src + (uint64_t)sy * sw;    // this row, vertical weight 3
        const uint8_t* r1 = src + (uint64_t)ny * sw;    // neighbour row, weight 1
        uint8_t* o = dst + (uint64_t)oy * dw;
        int thiscol = r0[0] * 3 + r1[0];
        if (sw == 1) { o[0] = (uint8_t)((thiscol * 4 + 8) >> 4); o[1] = (uint8_t)((thiscol * 4 + 7) >> 4); continue; }
        int nextcol = r0[1] * 3 + r1[1];
        o[0] = (uint8_t)((thiscol * 4 + 8) >> 4);
        o[1] = (uint8_t)((thiscol * 3 + nextcol + 7) >> 4);
        int lastcol = thiscol; thiscol = nextcol;
        for (int i = 1; i < sw - 1; i++) {
            nextcol = r0[i+1] * 3 + r1[i+1];
            o[2*i]     = (uint8_t)((thiscol * 3 + lastcol + 8) >> 4);
            o[2*i + 1] = (uint8_t)((thiscol * 3 + nextcol + 7) >> 4);
            lastcol = thiscol; thiscol = nextcol;
        }
        o[2*(sw-1)]     = (uint8_t)((thiscol * 3 + lastcol + 8) >> 4);
        o[2*(sw-1) + 1] = (uint8_t)((thiscol * 4 + 7) >> 4);
    }
}

int jpeg_decode(const uint8_t* src, uint32_t srclen, image_t* img) {
    if (srclen < 4 || src[0] != 0xFF || src[1] != 0xD8) return -1;   // SOI
    jstate* st = (jstate*)kmalloc(sizeof(jstate));
    if (!st) return -2;
    for (int t = 0; t < 4; t++) for (int l = 0; l <= 16; l++) { st->hdc[t].maxcode[l] = -1; st->hac[t].maxcode[l] = -1; }
    st->ncomp = 0; st->restart = 0; st->W = st->H = 0;

    uint8_t* planes[3] = {0, 0, 0};
    int cw[3] = {0,0,0}, ch[3] = {0,0,0};
    uint8_t* up[3] = {0, 0, 0}; int up_owned[3] = {0, 0, 0};   // full-res (upsampled) component planes
    uint8_t* out = 0;
    int rc = -100;
    uint32_t p = 2;

    while (p + 1 < srclen) {
        if (src[p] != 0xFF) { p++; continue; }
        while (p < srclen && src[p] == 0xFF) p++;        // skip fill bytes
        if (p >= srclen) break;
        uint8_t m = src[p++];
        if (m == 0xD9) break;                            // EOI
        if (m >= 0xD0 && m <= 0xD7) continue;            // stray RST (shouldn't appear here)
        if (p + 1 >= srclen) { rc = -3; goto done; }
        uint32_t seglen = ((uint32_t)src[p] << 8) | src[p+1];
        if (seglen < 2 || p + seglen > srclen) { rc = -4; goto done; }
        const uint8_t* seg = src + p + 2;
        uint32_t slen = seglen - 2;

        if (m == 0xC0) {                                 // SOF0 baseline
            if (slen < 6) { rc = -5; goto done; }
            if (seg[0] != 8) { rc = -6; goto done; }     // 8-bit precision only
            st->H = ((int)seg[1] << 8) | seg[2];
            st->W = ((int)seg[3] << 8) | seg[4];
            st->ncomp = seg[5];
            if ((st->ncomp != 1 && st->ncomp != 3) || slen < 6 + (uint32_t)st->ncomp * 3) { rc = -7; goto done; }
            if (st->W <= 0 || st->H <= 0 || st->W > JPG_MAX_DIM || st->H > JPG_MAX_DIM ||
                (uint32_t)st->W * st->H > JPG_MAX_PX) { rc = -8; goto done; }
            for (int ci = 0; ci < st->ncomp; ci++) {
                const uint8_t* cp = seg + 6 + ci * 3;
                st->comp[ci].id = cp[0];
                st->comp[ci].h = cp[1] >> 4; st->comp[ci].v = cp[1] & 15;
                st->comp[ci].tq = cp[2];
                if (st->comp[ci].h < 1 || st->comp[ci].h > 4 || st->comp[ci].v < 1 || st->comp[ci].v > 4 ||
                    st->comp[ci].tq > 3) { rc = -9; goto done; }
            }
        } else if (m == 0xC4) { if (jpeg_dht(st, seg, slen)) { rc = -10; goto done; } }
        else if (m == 0xDB) { if (jpeg_dqt(st, seg, slen)) { rc = -11; goto done; } }
        else if (m == 0xDD) { if (slen >= 2) st->restart = ((int)seg[0] << 8) | seg[1]; }
        else if (m == 0xC1 || m == 0xC2 || m == 0xC3 || (m >= 0xC5 && m <= 0xCF && m != 0xC8)) {
            rc = -12; goto done;                         // progressive / arithmetic / other SOF: unsupported
        } else if (m == 0xDA) {                          // SOS: scan header, then entropy-coded data
            if (st->ncomp == 0) { rc = -13; goto done; }
            int ns = seg[0];
            if (ns != st->ncomp || slen < 1 + (uint32_t)ns * 2 + 3) { rc = -14; goto done; }
            for (int k = 0; k < ns; k++) {
                int cs = seg[1 + k*2], tdta = seg[2 + k*2];
                for (int ci = 0; ci < st->ncomp; ci++) if (st->comp[ci].id == cs) {
                    st->comp[ci].td = tdta >> 4; st->comp[ci].ta = tdta & 15;
                }
            }
            // --- geometry: MCU grid + per-component sample planes ---
            int hmax = 1, vmax = 1;
            for (int ci = 0; ci < st->ncomp; ci++) { if (st->comp[ci].h > hmax) hmax = st->comp[ci].h; if (st->comp[ci].v > vmax) vmax = st->comp[ci].v; }
            int mcux = (st->W + 8*hmax - 1) / (8*hmax);
            int mcuy = (st->H + 8*vmax - 1) / (8*vmax);
            for (int ci = 0; ci < st->ncomp; ci++) {
                cw[ci] = mcux * st->comp[ci].h * 8;
                ch[ci] = mcuy * st->comp[ci].v * 8;
                planes[ci] = (uint8_t*)kmalloc((size_t)cw[ci] * ch[ci]);   // 64-bit multiply before widening to kmalloc's size_t
                if (!planes[ci]) { rc = -15; goto done; }
            }
            // --- decode every MCU ---
            jbr br = { src + p + seglen, srclen - (p + seglen), 0, 0, 0, 0 };
            for (int ci = 0; ci < st->ncomp; ci++) st->comp[ci].pred = 0;
            int blk[64]; uint8_t sm[64];
            int mcu = 0, ok = 1;
            for (int my = 0; my < mcuy && ok; my++) {
                for (int mx = 0; mx < mcux && ok; mx++) {
                    if (st->restart && mcu > 0 && (mcu % st->restart) == 0) {
                        jpeg_restart(&br);
                        for (int ci = 0; ci < st->ncomp; ci++) st->comp[ci].pred = 0;
                    }
                    for (int ci = 0; ci < st->ncomp; ci++) {
                        jcomp* c = &st->comp[ci];
                        for (int by = 0; by < c->v; by++)
                            for (int bx = 0; bx < c->h; bx++) {
                                if (jpeg_block(&br, st, c, blk)) { ok = 0; break; }
                                jpeg_idct(blk, sm);
                                int ox = (mx * c->h + bx) * 8, oy = (my * c->v + by) * 8;
                                for (int yy = 0; yy < 8; yy++)
                                    for (int xx = 0; xx < 8; xx++)
                                        planes[ci][(uint64_t)(oy + yy) * cw[ci] + (ox + xx)] = sm[yy*8 + xx];
                            }
                    }
                    mcu++;
                }
            }
            if (!ok) { rc = -16; goto done; }

            // --- upsample each component to full resolution (FW x FH), then colour convert into RGBA ---
            int FW = mcux * hmax * 8, FH = mcuy * vmax * 8;
            for (int ci = 0; ci < st->ncomp; ci++) {
                jcomp* c = &st->comp[ci];
                if (c->h == hmax && c->v == vmax) { up[ci] = planes[ci]; up_owned[ci] = 0; continue; }   // already full-res
                up[ci] = (uint8_t*)kmalloc((size_t)FW * FH);   // 64-bit multiply before widening to kmalloc's size_t
                if (!up[ci]) { rc = -17; goto done; }
                up_owned[ci] = 1;
                if (c->h * 2 == hmax && c->v * 2 == vmax)      jpeg_fancy_h2v2(planes[ci], cw[ci], ch[ci], up[ci]);   // 4:2:0
                else if (c->h * 2 == hmax && c->v == vmax)     jpeg_fancy_h2v1(planes[ci], cw[ci], ch[ci], up[ci]);   // 4:2:2
                else {                                                                                                // other: nearest
                    for (int oy = 0; oy < FH; oy++) {
                        int sy = oy * c->v / vmax;
                        for (int ox = 0; ox < FW; ox++)
                            up[ci][(uint64_t)oy * FW + ox] = planes[ci][(uint64_t)sy * cw[ci] + (ox * c->h / hmax)];
                    }
                }
            }
            out = (uint8_t*)kmalloc((size_t)st->W * st->H * 4);   // 64-bit multiply before widening to kmalloc's size_t
            if (!out) { rc = -17; goto done; }
            for (int y = 0; y < st->H; y++) {
                for (int x = 0; x < st->W; x++) {
                    uint8_t* o = out + ((uint64_t)y * st->W + x) * 4;
                    if (st->ncomp == 1) {
                        uint8_t Y = up[0][(uint64_t)y * FW + x];
                        o[0] = o[1] = o[2] = Y;
                    } else {
                        int Y  = up[0][(uint64_t)y * FW + x];
                        int Cb = up[1][(uint64_t)y * FW + x] - 128;
                        int Cr = up[2][(uint64_t)y * FW + x] - 128;
                        // fixed-point JFIF YCbCr->RGB (coefficients * 2^16, rounded): 1.402, 0.344136, 0.714136, 1.772
                        int ri = Y + ((91881 * Cr + 32768) >> 16);
                        int gi = Y - ((22554 * Cb + 46802 * Cr + 32768) >> 16);
                        int bi = Y + ((116130 * Cb + 32768) >> 16);
                        o[0] = (uint8_t)(ri < 0 ? 0 : ri > 255 ? 255 : ri);
                        o[1] = (uint8_t)(gi < 0 ? 0 : gi > 255 ? 255 : gi);
                        o[2] = (uint8_t)(bi < 0 ? 0 : bi > 255 ? 255 : bi);
                    }
                    o[3] = 255;
                }
            }
            img->width = st->W; img->height = st->H; img->pixels = out;
            out = 0; rc = 0;                              // success — don't free out in cleanup
            goto done;
        } else {                                         // APPn / COM / other: skip the segment
            /* length already validated */
        }
        p += seglen;
    }
    if (rc == -100) rc = -18;                             // no SOS reached

done:
    for (int ci = 0; ci < 3; ci++) if (up_owned[ci] && up[ci]) kfree(up[ci]);
    for (int ci = 0; ci < 3; ci++) if (planes[ci]) kfree(planes[ci]);
    if (out) kfree(out);
    kfree(st);
    return rc;
}

// ---- known-answer self-test (`jpegtest`) ----
#include "jpeg_vectors.h"

// Compare a decode to Pillow's reference within tolerance (IDCT/upsampling differ between decoders,
// so exact match is impossible; a correct decode stays within a few levels, a buggy one is way off).
static int jpeg_case(const char* name, const uint8_t* file, uint32_t flen, int w, int h,
                     const uint8_t* ref, int max_tol, int mean_tol_x100) {
    image_t im; int rc = jpeg_decode(file, flen, &im);
    if (rc != 0 || (int)im.width != w || (int)im.height != h) {
        printf("jpeg: %s FAIL (rc=%d %dx%d)\n", name, rc, rc == 0 ? (int)im.width : -1, rc == 0 ? (int)im.height : -1);
        if (rc == 0 && im.pixels) kfree(im.pixels);
        return 0;
    }
    long n = (long)w * h * 4, sum = 0; int mx = 0;
    for (long i = 0; i < n; i++) {
        int d = (int)im.pixels[i] - (int)ref[i]; if (d < 0) d = -d;
        sum += d; if (d > mx) mx = d;
    }
    kfree(im.pixels);
    int mean_x100 = (int)(sum * 100 / n);
    int ok = (mx <= max_tol && mean_x100 <= mean_tol_x100);
    printf("jpeg: %s %s (%dx%d, maxdiff=%d meandiff=%d.%02d)\n", name, ok ? "PASS" : "FAIL",
           w, h, mx, mean_x100 / 100, mean_x100 % 100);
    return ok;
}

int jpeg_selftest(void) {
    int pass = 0, total = 0;
    total++; pass += jpeg_case("grayscale", JPG_GRAY, sizeof(JPG_GRAY), JPG_GRAY_W, JPG_GRAY_H, JPG_GRAY_RGBA, 4, 40);
    total++; pass += jpeg_case("ycbcr-444", JPG_444,  sizeof(JPG_444),  JPG_444_W,  JPG_444_H,  JPG_444_RGBA,  4, 40);
    total++; pass += jpeg_case("ycbcr-420", JPG_420,  sizeof(JPG_420),  JPG_420_W,  JPG_420_H,  JPG_420_RGBA,  6, 60);   // fancy upsampling -> tighter than v5.9.89's 14/250
    printf("jpeg: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
