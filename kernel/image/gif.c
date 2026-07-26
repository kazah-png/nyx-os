// ============================================================
// gif.c - a minimal GIF (87a/89a) image decoder, static + animated
// ============================================================
// Decodes a GIF: header + Logical Screen Descriptor, an optional Global Color Table, Graphic Control
// Extensions (transparent index, frame delay, disposal method) and Image Descriptors — LZW-decompressing
// each image's indices, de-interlacing if needed, and mapping through the palette to RGBA. `gif_decode`
// returns just the first image; `gif_decode_anim` composites EVERY frame onto a logical-screen canvas
// (honouring sub-rectangles + transparency + disposal) for animation playback.
#include "../core/kernel.h"
#include "gif.h"

#define GIF_MAXCODES 4096
#define GIF_MAX_DIM  4096
#define GIF_MAX_PX   (1u << 20)
#define GIF_MAX_FRAMES     256
#define GIF_MAX_ANIM_BYTES (8u << 20)   // cap on total decoded-frame memory (~8 MB); extra frames dropped

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

// Read one Image Descriptor whose 9 header bytes start at src[*pp] (the 0x2C marker already consumed).
// Fills geometry (fx,fy,fw,fh) + the palette to use (ct/ctn) + a kmalloc'd fw*fh buffer of ROW-ORDERED
// palette indices (de-interlaced). Advances *pp past the LZW sub-blocks. 0 on success; negative on error.
static int gif_read_frame(const uint8_t* src, uint32_t srclen, uint32_t* pp,
                          const uint8_t* gct, int gct_n,
                          uint32_t* ofx, uint32_t* ofy, uint32_t* ofw, uint32_t* ofh,
                          const uint8_t** oct, int* octn, uint8_t** oidx) {
    uint32_t p = *pp;
    if (p + 9 > srclen) return -6;
    uint32_t fx = src[p]     | ((uint32_t)src[p + 1] << 8);
    uint32_t fy = src[p + 2] | ((uint32_t)src[p + 3] << 8);
    uint32_t fw = src[p + 4] | ((uint32_t)src[p + 5] << 8);
    uint32_t fh = src[p + 6] | ((uint32_t)src[p + 7] << 8);
    uint8_t ipk = src[p + 8];
    p += 9;
    int lct_flag = ipk >> 7, interlace = (ipk >> 6) & 1, lct_n = 2 << (ipk & 7);
    const uint8_t* ct = gct; int ctn = gct_n;
    if (lct_flag) { if (p + (uint32_t)lct_n * 3 > srclen) return -7; ct = src + p; ctn = lct_n; p += lct_n * 3; }
    if (!ct) return -8;
    if (fw == 0 || fh == 0 || fw > GIF_MAX_DIM || fh > GIF_MAX_DIM || (uint64_t)fw * fh > GIF_MAX_PX) return -9;
    if (p >= srclen) return -10;
    int mcs = src[p++];
    if (mcs < 2 || mcs > 8) return -11;

    uint32_t total = 0, q = p;                              // measure the LZW sub-block chain
    while (q < srclen) { uint8_t sz = src[q++]; if (sz == 0) break; total += sz; q += sz; }
    uint8_t* lzw = (uint8_t*)kmalloc(total ? total : 1);
    uint8_t* lin = (uint8_t*)kmalloc((uint64_t)fw * fh);    // LZW output = pixels in (possibly interlaced) order
    uint8_t* idx = (uint8_t*)kmalloc((uint64_t)fw * fh);    // de-interlaced, natural row order
    if (!lzw || !lin || !idx) { if (lzw) kfree(lzw); if (lin) kfree(lin); if (idx) kfree(idx); return -12; }
    { uint32_t o = 0, r = p; while (r < srclen) { uint8_t sz = src[r++]; if (sz == 0) break;
          uint32_t k; for (k = 0; k < sz && r < srclen; k++) lzw[o++] = src[r++]; } p = r; }
    int rc = gif_lzw(lzw, total, mcs, lin, fw * fh);
    kfree(lzw);
    if (rc != 0) { kfree(lin); kfree(idx); return -13; }

    if (interlace) {
        static const int starts[4] = {0, 4, 2, 1}, steps[4] = {8, 8, 4, 2};
        uint32_t dy = 0, pass, y, x;
        for (pass = 0; pass < 4; pass++)
            for (y = (uint32_t)starts[pass]; y < fh; y += (uint32_t)steps[pass]) {
                for (x = 0; x < fw; x++) idx[(uint64_t)y * fw + x] = lin[(uint64_t)dy * fw + x];
                dy++;
            }
    } else {
        uint64_t k, n = (uint64_t)fw * fh; for (k = 0; k < n; k++) idx[k] = lin[k];
    }
    kfree(lin);
    *ofx = fx; *ofy = fy; *ofw = fw; *ofh = fh; *oct = ct; *octn = ctn; *oidx = idx; *pp = p;
    return 0;
}

int gif_decode(const uint8_t* src, uint32_t srclen, image_t* img) {
    if (srclen < 13) return -1;
    if (src[0] != 'G' || src[1] != 'I' || src[2] != 'F') return -2;
    uint8_t packed = src[10];
    int gct_flag = packed >> 7, gct_n = 2 << (packed & 7);  // 2^(size+1) entries
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

        uint32_t fx, fy, fw, fh; const uint8_t* ct; int ctn; uint8_t* idx;
        int rc = gif_read_frame(src, srclen, &p, gct, gct_n, &fx, &fy, &fw, &fh, &ct, &ctn, &idx);
        if (rc != 0) return rc;
        uint8_t* px = (uint8_t*)kmalloc((uint64_t)fw * fh * 4);
        if (!px) { kfree(idx); return -12; }
        uint64_t n = (uint64_t)fw * fh, k;
        for (k = 0; k < n; k++) {
            uint32_t ci = idx[k]; uint8_t* o = px + k * 4;
            if (ci < (uint32_t)ctn) { o[0] = ct[ci*3]; o[1] = ct[ci*3+1]; o[2] = ct[ci*3+2]; }
            else { o[0] = o[1] = o[2] = 0; }
            o[3] = (transparent >= 0 && (int)ci == transparent) ? 0 : 255;
        }
        kfree(idx);
        img->width = fw; img->height = fh; img->pixels = px;
        return 0;
    }
    return -14;                                             // no image descriptor found
}

// Composite every frame onto a logical-screen canvas. Each frame's displayed image is the full canvas
// after drawing that frame's sub-rectangle over the previous contents (transparent indices leave the
// pixel underneath), then the disposal method decides what the NEXT frame starts from.
int gif_decode_anim(const uint8_t* src, uint32_t srclen, gif_anim_t* out) {
    if (srclen < 13) return -1;
    if (src[0] != 'G' || src[1] != 'I' || src[2] != 'F') return -2;
    uint32_t W = src[6] | ((uint32_t)src[7] << 8);
    uint32_t H = src[8] | ((uint32_t)src[9] << 8);
    uint8_t packed = src[10];
    int gct_flag = packed >> 7, gct_n = 2 << (packed & 7);
    if (W == 0 || H == 0 || W > GIF_MAX_DIM || H > GIF_MAX_DIM || (uint64_t)W * H > GIF_MAX_PX) return -3;
    uint32_t p = 13;
    const uint8_t* gct = 0;
    if (gct_flag) { if (p + (uint32_t)gct_n * 3 > srclen) return -4; gct = src + p; p += gct_n * 3; }

    uint64_t fbytes = (uint64_t)W * H * 4;
    uint8_t* canvas = (uint8_t*)kmalloc(fbytes);
    uint8_t* saved  = 0;                                    // disposal 3 (restore-to-previous) snapshot
    gif_frame_t* frames = (gif_frame_t*)kmalloc(GIF_MAX_FRAMES * sizeof(gif_frame_t));
    if (!canvas || !frames) { if (canvas) kfree(canvas); if (frames) kfree(frames); return -5; }
    { uint64_t k; for (k = 0; k < fbytes; k++) canvas[k] = 0; }   // start fully transparent

    int nf = 0, delay = 0, trans = -1, disposal = 0, loop_count = 0;   // loop_count 0 = infinite (default)
    while (p < srclen && nf < GIF_MAX_FRAMES) {
        uint8_t blk = src[p++];
        if (blk == 0x3B) break;                             // trailer
        if (blk == 0x21) {                                  // extension
            if (p >= srclen) break;
            uint8_t label = src[p++];
            if (label == 0xF9 && p + 5 <= srclen && src[p] == 4) {     // Graphic Control Extension
                uint8_t flags = src[p + 1];
                delay    = src[p + 2] | ((int)src[p + 3] << 8);        // centiseconds
                trans    = (flags & 1) ? src[p + 4] : -1;
                disposal = (flags >> 2) & 7;
            } else if (label == 0xFF && p + 16 <= srclen && src[p] == 11) {   // Application Extension
                static const char NS[11] = { 'N','E','T','S','C','A','P','E','2','.','0' };
                int is_ns = 1; for (int k = 0; k < 11; k++) if (src[p + 1 + k] != (uint8_t)NS[k]) is_ns = 0;
                if (is_ns && src[p + 12] == 3 && src[p + 13] == 1)     // sub-block: 03 01 LL HH -> loop count
                    loop_count = src[p + 14] | ((int)src[p + 15] << 8);
            }
            while (p < srclen) { uint8_t sz = src[p++]; if (sz == 0) break; p += sz; }
            continue;
        }
        if (blk != 0x2C) break;                             // unknown block: stop, keep frames so far

        uint32_t fx, fy, fw, fh; const uint8_t* ct; int ctn; uint8_t* idx;
        int rc = gif_read_frame(src, srclen, &p, gct, gct_n, &fx, &fy, &fw, &fh, &ct, &ctn, &idx);
        if (rc != 0) break;
        if ((uint64_t)(nf + 1) * fbytes > GIF_MAX_ANIM_BYTES) { kfree(idx); break; }   // memory cap

        if (disposal == 3) {                                // snapshot BEFORE drawing (restore-to-previous)
            if (!saved) saved = (uint8_t*)kmalloc(fbytes);
            if (saved) { uint64_t k; for (k = 0; k < fbytes; k++) saved[k] = canvas[k]; }
        }
        uint32_t y, x;                                      // draw the sub-image over the canvas
        for (y = 0; y < fh; y++) {
            uint32_t gy = fy + y; if (gy >= H) break;
            const uint8_t* srow = idx + (uint64_t)y * fw;
            for (x = 0; x < fw; x++) {
                uint32_t gx = fx + x; if (gx >= W) continue;
                uint32_t ci = srow[x];
                if (trans >= 0 && (int)ci == trans) continue;          // transparent -> keep canvas pixel
                uint8_t* o = canvas + ((uint64_t)gy * W + gx) * 4;
                if (ci < (uint32_t)ctn) { o[0] = ct[ci*3]; o[1] = ct[ci*3+1]; o[2] = ct[ci*3+2]; }
                else { o[0] = o[1] = o[2] = 0; }
                o[3] = 255;
            }
        }
        kfree(idx);

        uint8_t* fpx = (uint8_t*)kmalloc(fbytes);           // snapshot the composited canvas as this frame
        if (!fpx) break;
        { uint64_t k; for (k = 0; k < fbytes; k++) fpx[k] = canvas[k]; }
        int d = delay; if (d < 2) d = 10;                   // browsers clamp <20ms delays to 100ms
        frames[nf].pixels = fpx; frames[nf].delay_cs = (uint16_t)d; nf++;

        if (disposal == 2) {                                // restore to background: clear this rect (transparent)
            for (y = 0; y < fh; y++) { uint32_t gy = fy + y; if (gy >= H) break;
                for (x = 0; x < fw; x++) { uint32_t gx = fx + x; if (gx >= W) continue;
                    uint8_t* o = canvas + ((uint64_t)gy * W + gx) * 4; o[0] = o[1] = o[2] = o[3] = 0; } }
        } else if (disposal == 3 && saved) {                // restore to previous
            uint64_t k; for (k = 0; k < fbytes; k++) canvas[k] = saved[k];
        }
        delay = 0; trans = -1; disposal = 0;                // reset GCE state for the next frame
    }
    kfree(canvas); if (saved) kfree(saved);
    if (nf == 0) { kfree(frames); return -14; }
    out->width = W; out->height = H; out->nframes = nf; out->frames = frames; out->loop_count = loop_count;
    return 0;
}

void gif_anim_free(gif_anim_t* a) {
    if (!a || !a->frames) return;
    int f; for (f = 0; f < a->nframes; f++) if (a->frames[f].pixels) kfree(a->frames[f].pixels);
    kfree(a->frames); a->frames = 0; a->nframes = 0;
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
// Animated case: decode all frames, check the frame count, each frame's composited RGBA, and delays.
static int gif_anim_case(const char* name, const uint8_t* file, uint32_t flen, uint32_t w, uint32_t h,
                         int nframes, const uint8_t* const* want, const uint16_t* delays) {
    gif_anim_t a; int rc = gif_decode_anim(file, flen, &a);
    int ok = (rc == 0 && a.width == w && a.height == h && a.nframes == nframes);
    for (int f = 0; ok && f < nframes; f++)
        ok = gif_bufeq(a.frames[f].pixels, want[f], w * h * 4) && (a.frames[f].delay_cs == delays[f]);
    if (rc == 0) gif_anim_free(&a);
    if (ok) printf("gif: %s PASS (%dx%d, %d frames)\n", name, (int)w, (int)h, nframes);
    else    printf("gif: %s FAIL (rc=%d nframes=%d)\n", name, rc, (rc == 0 ? a.nframes : -1));
    return ok;
}

// NETSCAPE loop count: a GIF saved with loop=3 must report loop_count 3; the loop=0 ("infinite") one reports 0.
static int gif_loop_case(void) {
    gif_anim_t a; int ok = 1;
    if (gif_decode_anim(GIF_LOOP3, sizeof(GIF_LOOP3), &a) == 0) { ok = ok && (a.loop_count == 3); gif_anim_free(&a); } else ok = 0;
    if (gif_decode_anim(GIF_ANIMF, sizeof(GIF_ANIMF), &a) == 0) { ok = ok && (a.loop_count == 0); gif_anim_free(&a); } else ok = 0;
    printf("gif: loop-count %s (finite=3 + infinite=0)\n", ok ? "PASS" : "FAIL");
    return ok;
}

int gif_selftest(void) {
    int pass = 0, total = 0;
    total++; pass += gif_case("basic",       GIF_BASIC,     sizeof(GIF_BASIC),     GIF_BASIC_W,     GIF_BASIC_H,     GIF_BASIC_RGBA);
    total++; pass += gif_case("transparency",GIF_TRNS,      sizeof(GIF_TRNS),      GIF_TRNS_W,      GIF_TRNS_H,      GIF_TRNS_RGBA);
    total++; pass += gif_case("interlaced",  GIF_INTERLACE, sizeof(GIF_INTERLACE), GIF_INTERLACE_W, GIF_INTERLACE_H, GIF_INTERLACE_RGBA);
    total++; pass += gif_case("dict-growth", GIF_BIG,       sizeof(GIF_BIG),       GIF_BIG_W,       GIF_BIG_H,       GIF_BIG_RGBA);
    total++; pass += gif_anim_case("anim-full", GIF_ANIMF, sizeof(GIF_ANIMF), GIF_ANIMF_W, GIF_ANIMF_H,
                                   GIF_ANIMF_N, GIF_ANIMF_FRAMES, GIF_ANIMF_DELAYS);
    total++; pass += gif_anim_case("anim-partial", GIF_ANIMP, sizeof(GIF_ANIMP), GIF_ANIMP_W, GIF_ANIMP_H,
                                   GIF_ANIMP_N, GIF_ANIMP_FRAMES, GIF_ANIMP_DELAYS);
    total++; pass += gif_loop_case();
    printf("gif: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
