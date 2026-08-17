// ============================================================
// png.c - a minimal PNG image decoder (built on inflate.c)
// ============================================================
// Decodes the common PNG shapes NyxOS needs for the web: 8-bit depth, non-interlaced, color types
// grayscale(0) / RGB(2) / palette(3) / gray+alpha(4) / RGBA(6). Walks the chunk stream, concatenates
// the IDAT payloads, zlib-inflates them, reverses the per-scanline filters (all 5), and expands each
// pixel to RGBA. No 16-bit depth, sub-byte depth, or Adam7 interlacing yet (returns an error).
#include "../core/kernel.h"
#include "png.h"
#include "inflate.h"
#include "deflate.h"                             // zlib_deflate, for the PNG encoder
#include "bmp.h"                                 // sibling decoders, for the cross-format reject self-test
#include "gif.h"
#include "jpeg.h"

#define PNG_MAX_DIM    4096
#define PNG_MAX_PIXELS (1u << 20)          // 1M pixels — bounds the kmalloc for a decoded image

// CRC-32 (kernel.c) validates each chunk so a corrupt/truncated chunk is refused
// up front rather than reaching an inflate error or a downstream field check.
extern uint32_t crc32_calc(const uint8_t* data, uint32_t len);

static uint32_t png_u32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int png_type_is(const uint8_t* t, char a, char b, char c, char d) {
    return t[0] == (uint8_t)a && t[1] == (uint8_t)b && t[2] == (uint8_t)c && t[3] == (uint8_t)d;
}
static int png_paeth(int a, int b, int c) {
    int p = a + b - c, pa = p - a, pb = p - b, pc = p - c;
    if (pa < 0) pa = -pa;
    if (pb < 0) pb = -pb;
    if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int png_decode(const uint8_t* src, uint32_t srclen, image_t* img) {
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    uint32_t w = 0, h = 0, idat_total = 0, p, channels, stride, raw_cap, io, got;
    int depth = 0, ctype = -1, interlace = 0, i, zr;
    const uint8_t *plte = 0, *trns = 0; uint32_t plte_len = 0, trns_len = 0;
    uint8_t *idat, *raw, *px;

    if (srclen < 8 + 25) return -1;                          // signature + IHDR + IEND minimum
    for (i = 0; i < 8; i++) if (src[i] != sig[i]) return -2;

    // Pass 1: read IHDR, remember PLTE/tRNS, and total up the IDAT payload size.
    for (p = 8; p + 12 <= srclen; ) {
        uint32_t len = png_u32(src + p);
        const uint8_t* typ = src + p + 4;
        const uint8_t* data = src + p + 8;
        if ((uint64_t)p + 12 + len > srclen) return -3;
        // Validate the chunk CRC (over type+data) before trusting any field — a
        // corrupt or tampered chunk is rejected here. PNG stores it big-endian
        // right after the data.
        if (crc32_calc(typ, 4 + len) != png_u32(data + len)) return -15;
        if      (png_type_is(typ,'I','H','D','R')) {
            if (len < 13) return -4;
            w = png_u32(data); h = png_u32(data + 4);
            depth = data[8]; ctype = data[9]; interlace = data[12];
        }
        else if (png_type_is(typ,'P','L','T','E')) { plte = data; plte_len = len; }
        else if (png_type_is(typ,'t','R','N','S')) { trns = data; trns_len = len; }
        else if (png_type_is(typ,'I','D','A','T')) { idat_total += len; }
        else if (png_type_is(typ,'I','E','N','D')) break;
        p += 12 + len;
    }

    if (ctype < 0 || w == 0 || h == 0) return -5;
    if (depth != 8)     return -6;                           // 8-bit depth only (v1)
    if (interlace != 0) return -7;                           // no Adam7 interlacing (v1)
    switch (ctype) {
        case 0: channels = 1; break;                         // grayscale
        case 2: channels = 3; break;                         // RGB
        case 3: channels = 1; break;                         // palette index
        case 4: channels = 2; break;                         // grayscale + alpha
        case 6: channels = 4; break;                         // RGBA
        default: return -8;
    }
    if (ctype == 3 && !plte) return -9;
    if (w > PNG_MAX_DIM || h > PNG_MAX_DIM || (uint64_t)w * h > PNG_MAX_PIXELS) return -10;
    if (idat_total == 0) return -11;

    stride  = w * channels;
    raw_cap = h * (1 + stride);
    idat = (uint8_t*)kmalloc(idat_total);
    raw  = (uint8_t*)kmalloc(raw_cap);
    if (!idat || !raw) { if (idat) kfree(idat); if (raw) kfree(raw); return -12; }

    // Pass 2: concatenate the IDAT payloads into one zlib stream.
    io = 0;
    for (p = 8; p + 12 <= srclen; ) {
        uint32_t len = png_u32(src + p);
        const uint8_t* typ = src + p + 4;
        const uint8_t* data = src + p + 8;
        if (png_type_is(typ,'I','D','A','T')) { uint32_t k; for (k = 0; k < len; k++) idat[io++] = data[k]; }
        else if (png_type_is(typ,'I','E','N','D')) break;
        p += 12 + len;
    }

    got = 0;
    zr = zlib_inflate(idat, idat_total, raw, raw_cap, &got);
    kfree(idat);
    if (zr != 0 || got != raw_cap) { kfree(raw); return -13; }

    // Reverse the per-scanline filters in place (each row is a 1-byte filter tag + `stride` bytes).
    {
        uint32_t y, x;
        for (y = 0; y < h; y++) {
            uint8_t* rowf = raw + (uint64_t)y * (1 + stride);
            uint8_t ft = rowf[0];
            uint8_t* cur = rowf + 1;
            const uint8_t* prev = (y == 0) ? 0 : (raw + (uint64_t)(y - 1) * (1 + stride) + 1);
            for (x = 0; x < stride; x++) {
                int a = (x >= channels) ? cur[x - channels] : 0;
                int b = prev ? prev[x] : 0;
                int c = (prev && x >= channels) ? prev[x - channels] : 0;
                int val = cur[x], r;
                if      (ft == 0) r = val;
                else if (ft == 1) r = val + a;
                else if (ft == 2) r = val + b;
                else if (ft == 3) r = val + ((a + b) >> 1);
                else if (ft == 4) r = val + png_paeth(a, b, c);
                else { kfree(raw); return -14; }
                cur[x] = (uint8_t)r;
            }
        }
    }

    // Expand every pixel to RGBA.
    px = (uint8_t*)kmalloc((uint64_t)w * h * 4);
    if (!px) { kfree(raw); return -12; }
    {
        uint32_t y, x;
        for (y = 0; y < h; y++) {
            const uint8_t* row = raw + (uint64_t)y * (1 + stride) + 1;
            uint8_t* out = px + (uint64_t)y * w * 4;
            for (x = 0; x < w; x++) {
                uint8_t* o = out + x * 4;
                if (ctype == 0)      { uint8_t g = row[x];        o[0] = o[1] = o[2] = g; o[3] = 255; }
                else if (ctype == 2) { const uint8_t* s = row + x*3; o[0]=s[0]; o[1]=s[1]; o[2]=s[2]; o[3]=255; }
                else if (ctype == 6) { const uint8_t* s = row + x*4; o[0]=s[0]; o[1]=s[1]; o[2]=s[2]; o[3]=s[3]; }
                else if (ctype == 4) { uint8_t g = row[x*2];      o[0]=o[1]=o[2]=g; o[3]=row[x*2+1]; }
                else {                                            // palette
                    uint32_t idx = row[x];
                    if (idx * 3 + 2 < plte_len) { o[0]=plte[idx*3]; o[1]=plte[idx*3+1]; o[2]=plte[idx*3+2]; }
                    else { o[0]=o[1]=o[2]=0; }
                    o[3] = (trns && idx < trns_len) ? trns[idx] : 255;
                }
            }
        }
    }
    kfree(raw);
    img->width = w; img->height = h; img->pixels = px;
    return 0;
}

void png_free(image_t* img) {
    if (img && img->pixels) { kfree(img->pixels); img->pixels = 0; }
}

// ---- known-answer self-test (`pngtest`) ----
// Each case: a PNG file + the expected RGBA. Generated by scratchpad/gen_png.py and cross-checked
// against Python Pillow. Covers color types 0/2/3(+tRNS)/6 and all 5 scanline filter types.
#define PNG_RGB_W 4
#define PNG_RGB_H 4
static const uint8_t PNG_RGB[106] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x04,
    0x08,0x02,0x00,0x00,0x00,0x26,0x93,0x09,0x29,0x00,0x00,0x00,
    0x31,0x49,0x44,0x41,0x54,0x78,0xda,0x05,0xc1,0x21,0x01,0x00,
    0x30,0x0c,0x03,0xc1,0x17,0x31,0x5c,0x1c,0x11,0x11,0x11,0x5c,
    0x5c,0x11,0x91,0xbf,0x3b,0x80,0xe1,0x19,0x2d,0x06,0xbd,0x91,
    0x2c,0xaf,0x02,0xd1,0xc4,0x4e,0x36,0x07,0xf5,0x34,0xee,0x6d,
    0xfb,0x01,0xc2,0xbc,0x0a,0x51,0x47,0x51,0xf0,0x47,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82,
};
static const uint8_t PNG_RGB_RGBA[64] = {
    0x00,0x00,0x00,0xff,0x1e,0x00,0x14,0xff,0x3c,0x00,0x28,0xff,
    0x5a,0x00,0x3c,0xff,0x00,0x28,0x14,0xff,0x1e,0x28,0x28,0xff,
    0x3c,0x28,0x3c,0xff,0x5a,0x28,0x50,0xff,0x00,0x50,0x28,0xff,
    0x1e,0x50,0x3c,0xff,0x3c,0x50,0x50,0xff,0x5a,0x50,0x64,0xff,
    0x00,0x78,0x3c,0xff,0x1e,0x78,0x50,0xff,0x3c,0x78,0x64,0xff,
    0x5a,0x78,0x78,0xff,
};
#define PNG_RGBA_W 3
#define PNG_RGBA_H 3
static const uint8_t PNG_RGBA[94] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,
    0x08,0x06,0x00,0x00,0x00,0x56,0x28,0xb5,0xbf,0x00,0x00,0x00,
    0x25,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x60,0x60,0xa8,0x60,
    0x08,0x60,0xa8,0x30,0x5a,0xc0,0x50,0x91,0xc2,0x04,0x64,0xc9,
    0xc1,0x30,0x23,0xc3,0x82,0x0a,0x9b,0x00,0x06,0x06,0x23,0x10,
    0x06,0x00,0x87,0x0e,0x06,0x94,0x3c,0x8f,0x6b,0xf4,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82,
};
static const uint8_t PNG_RGBA_RGBA[36] = {
    0x00,0x00,0x78,0x00,0x50,0x00,0x78,0x32,0xa0,0x00,0x78,0x64,
    0x00,0x50,0x78,0x1e,0x50,0x50,0x78,0x50,0xa0,0x50,0x78,0x82,
    0x00,0xa0,0x78,0x3c,0x50,0xa0,0x78,0x6e,0xa0,0xa0,0x78,0xa0,
};
#define PNG_PAL_W 4
#define PNG_PAL_H 2
static const uint8_t PNG_PAL[114] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x02,
    0x08,0x03,0x00,0x00,0x00,0x48,0x76,0x8d,0x51,0x00,0x00,0x00,
    0x0c,0x50,0x4c,0x54,0x45,0xff,0x00,0x00,0x00,0xff,0x00,0x00,
    0x00,0xff,0xff,0xff,0x00,0xd6,0x02,0x8f,0x7b,0x00,0x00,0x00,
    0x03,0x74,0x52,0x4e,0x53,0xff,0x80,0xff,0x52,0x6f,0x87,0xf5,
    0x00,0x00,0x00,0x12,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x60,
    0x60,0x64,0x62,0x66,0x60,0x66,0x62,0x64,0x00,0x00,0x00,0x46,
    0x00,0x0d,0xa4,0x00,0x59,0x7b,0x00,0x00,0x00,0x00,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82,
};
static const uint8_t PNG_PAL_RGBA[32] = {
    0xff,0x00,0x00,0xff,0x00,0xff,0x00,0x80,0x00,0x00,0xff,0xff,
    0xff,0xff,0x00,0xff,0xff,0xff,0x00,0xff,0x00,0x00,0xff,0xff,
    0x00,0xff,0x00,0x80,0xff,0x00,0x00,0xff,
};
#define PNG_GRAY_W 2
#define PNG_GRAY_H 2
static const uint8_t PNG_GRAY[71] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,
    0x08,0x00,0x00,0x00,0x00,0x57,0xdd,0x52,0xf8,0x00,0x00,0x00,
    0x0e,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0xe0,0x8a,0x62,0x58,
    0xf5,0x0b,0x00,0x03,0xee,0x02,0x09,0x1e,0x8b,0x72,0xeb,0x00,
    0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82,
};
static const uint8_t PNG_GRAY_RGBA[16] = {
    0x0a,0x0a,0x0a,0xff,0x5a,0x5a,0x5a,0xff,0xaa,0xaa,0xaa,0xff,
    0xfa,0xfa,0xfa,0xff,
};
#define PNG_FILT_W 5
#define PNG_FILT_H 5
static const uint8_t PNG_FILT[106] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
    0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0x05,
    0x08,0x02,0x00,0x00,0x00,0x02,0x0d,0xb1,0xb2,0x00,0x00,0x00,
    0x31,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x60,0x60,0x60,0xe0,
    0x16,0x67,0x10,0xd3,0x63,0x50,0x74,0x65,0xd0,0x89,0x61,0x60,
    0x64,0x67,0x90,0x05,0xf2,0xe1,0x88,0x09,0xc8,0x47,0x46,0xcc,
    0x7c,0x0c,0x56,0x9c,0x3c,0xfc,0x70,0xc4,0x02,0x16,0x66,0x80,
    0x23,0x00,0xb6,0x9e,0x03,0xd7,0xa0,0xec,0xac,0xd8,0x00,0x00,
    0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82,
};
static const uint8_t PNG_FILT_RGBA[100] = {
    0x00,0x00,0x00,0xff,0x0b,0x17,0x00,0xff,0x16,0x2e,0x00,0xff,
    0x21,0x45,0x00,0xff,0x2c,0x5c,0x00,0xff,0x07,0x00,0x1d,0xff,
    0x12,0x17,0x1d,0xff,0x1d,0x2e,0x1d,0xff,0x28,0x45,0x1d,0xff,
    0x33,0x5c,0x1d,0xff,0x0e,0x00,0x3a,0xff,0x19,0x17,0x3a,0xff,
    0x24,0x2e,0x3a,0xff,0x2f,0x45,0x3a,0xff,0x3a,0x5c,0x3a,0xff,
    0x15,0x00,0x57,0xff,0x20,0x17,0x57,0xff,0x2b,0x2e,0x57,0xff,
    0x36,0x45,0x57,0xff,0x41,0x5c,0x57,0xff,0x1c,0x00,0x74,0xff,
    0x27,0x17,0x74,0xff,0x32,0x2e,0x74,0xff,0x3d,0x45,0x74,0xff,
    0x48,0x5c,0x74,0xff,
};

static int png_bufeq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    uint32_t i; for (i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1;
}
static int png_case(const char* name, const uint8_t* file, uint32_t flen,
                    uint32_t w, uint32_t h, const uint8_t* want) {
    image_t im; int rc = png_decode(file, flen, &im);
    int ok = (rc == 0 && im.width == w && im.height == h && png_bufeq(im.pixels, want, w * h * 4));
    if (rc == 0) png_free(&im);
    if (ok) printf("png: %s PASS (%dx%d)\n", name, (int)w, (int)h);
    else    printf("png: %s FAIL (rc=%d %dx%d)\n", name, rc, (int)im.width, (int)im.height);
    return ok;
}

// ============================================================
// PNG ENCODER - 8-bit RGB truecolor, filter None, single IDAT
// ============================================================
// Writes a standard PNG from w*h XRGB (0xFFRRGGBB, as fb_get_addr provides). Reuses the existing
// zlib_deflate (RFC 1950) for IDAT and crc32_calc for the per-chunk CRC-32. Heap-free: the caller
// supplies `raw` scratch (>= h*(1+w*3) bytes) for the filtered scanlines. Returns bytes written to
// dst, or 0 on overflow/failure. Verified against a real PNG decoder (scratchpad/png_proto.c) and,
// in-OS, round-tripped through png_decode by png_encode_selftest.
static void png_put32be(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

uint32_t png_encode(const uint32_t* px, uint32_t w, uint32_t h,
                    uint8_t* dst, uint32_t cap, uint8_t* raw, uint32_t rawcap) {
    if (!px || !dst || !raw || w == 0 || h == 0) return 0;
    uint32_t raw_len = h * (1 + w * 3);
    if (raw_len > rawcap) return 0;
    uint32_t o = 0;                                   // filtered scanlines: filter 0 (None) + RGB
    for (uint32_t y = 0; y < h; y++) {
        raw[o++] = 0;
        const uint32_t* row = px + (uint64_t)y * w;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t p = row[x];
            raw[o++] = (p >> 16) & 0xFF; raw[o++] = (p >> 8) & 0xFF; raw[o++] = p & 0xFF;
        }
    }
    if (cap < 8 + 25 + 12 + 12) return 0;             // sig + IHDR + min IDAT + IEND
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    uint32_t d = 0;
    for (int i = 0; i < 8; i++) dst[d++] = sig[i];
    png_put32be(dst + d, 13); d += 4;                 // IHDR
    uint32_t ihdr_t = d;
    dst[d++] = 'I'; dst[d++] = 'H'; dst[d++] = 'D'; dst[d++] = 'R';
    png_put32be(dst + d, w); d += 4; png_put32be(dst + d, h); d += 4;
    dst[d++] = 8; dst[d++] = 2; dst[d++] = 0; dst[d++] = 0; dst[d++] = 0;   // 8-bit RGB, deflate, filter, no-interlace
    png_put32be(dst + d, crc32_calc(dst + ihdr_t, 4 + 13)); d += 4;
    uint32_t idat_len_pos = d; d += 4;                // IDAT length placeholder
    uint32_t idat_t = d;
    dst[d++] = 'I'; dst[d++] = 'D'; dst[d++] = 'A'; dst[d++] = 'T';
    uint32_t idat_data = d;
    if (cap < idat_data + 4 + 12) return 0;
    uint32_t avail = cap - idat_data - 4 - 12;        // leave room for IDAT crc + IEND
    uint32_t idat_len = 0;
    if (zlib_deflate(raw, raw_len, dst + idat_data, avail, &idat_len) != 0) return 0;
    d = idat_data + idat_len;
    png_put32be(dst + idat_len_pos, idat_len);
    png_put32be(dst + d, crc32_calc(dst + idat_t, 4 + idat_len)); d += 4;
    png_put32be(dst + d, 0); d += 4;                  // IEND
    uint32_t iend_t = d;
    dst[d++] = 'I'; dst[d++] = 'E'; dst[d++] = 'N'; dst[d++] = 'D';
    png_put32be(dst + d, crc32_calc(dst + iend_t, 4)); d += 4;
    return d;
}

// KAT: encode a fixed image, then decode it with the OS's own png_decode and require the pixels
// round-trip exactly (also checks the PNG signature). Robust to the exact deflate byte output.
int png_encode_selftest(void) {
    static const uint32_t src[6] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF, 0x00000000, 0x00808080};
    static uint8_t raw[64];
    static uint8_t out[512];
    uint32_t n = png_encode(src, 3, 2, out, sizeof(out), raw, sizeof(raw));
    if (n == 0) return 1;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    for (int i = 0; i < 8; i++) if (out[i] != sig[i]) return 2;
    image_t img;
    if (png_decode(out, n, &img) != 0) return 3;
    if (img.width != 3 || img.height != 2 || !img.pixels) { png_free(&img); return 4; }
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t p = src[i];
        if (img.pixels[i * 4 + 0] != ((p >> 16) & 0xFF) ||
            img.pixels[i * 4 + 1] != ((p >> 8) & 0xFF) ||
            img.pixels[i * 4 + 2] != (p & 0xFF)) { png_free(&img); return 5; }
    }
    png_free(&img);
    return 0;
}

int png_selftest(void) {
    int pass = 0, total = 0;
    total++; pass += png_case("RGB",       PNG_RGB,  sizeof(PNG_RGB),  PNG_RGB_W,  PNG_RGB_H,  PNG_RGB_RGBA);
    total++; pass += png_case("RGBA",      PNG_RGBA, sizeof(PNG_RGBA), PNG_RGBA_W, PNG_RGBA_H, PNG_RGBA_RGBA);
    total++; pass += png_case("palette",   PNG_PAL,  sizeof(PNG_PAL),  PNG_PAL_W,  PNG_PAL_H,  PNG_PAL_RGBA);
    total++; pass += png_case("grayscale", PNG_GRAY, sizeof(PNG_GRAY), PNG_GRAY_W, PNG_GRAY_H, PNG_GRAY_RGBA);
    total++; pass += png_case("all-filters", PNG_FILT, sizeof(PNG_FILT), PNG_FILT_W, PNG_FILT_H, PNG_FILT_RGBA);
    printf("png: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}

// ---- adversarial / hostile-input self-test (`imgreject`) ----
// The known-answer tests above prove the decoders accept VALID images; this proves they
// REJECT malformed or hostile ones gracefully — a negative return, no crash and no
// out-of-bounds access. That is exactly the property that matters when Selene hands a
// decoder bytes straight off the network. Every case below MUST be refused; a decode
// that returns 0 (accepts) on junk is the failure. It feeds all four decoders empty and
// garbage buffers, then mutates the valid 4x4 RGB PNG above into hostile variants.
static int img_must_reject(const char* what, int (*dec)(const uint8_t*, uint32_t, image_t*),
                           const uint8_t* buf, uint32_t len) {
    image_t im; im.pixels = 0; im.width = 0; im.height = 0;
    int rc = dec(buf, len, &im);
    if (rc == 0) {                                  // accepted junk -> failure; don't leak the pixels
        if (im.pixels) kfree(im.pixels);
        printf("imgreject: %s NOT rejected (rc=0, %ux%u) FAIL\n", what, im.width, im.height);
        return 0;
    }
    return 1;                                        // refused, as required
}

int image_reject_selftest(void) {
    int ok = 1;
    static const uint8_t empty[1] = { 0 };          // 1 real byte, passed with len 0 (no OOB even if peeked)
    static uint8_t junk[64];
    for (int i = 0; i < 64; i++) junk[i] = (uint8_t)(0xA5 ^ i);   // not a valid magic for any format

    // Every decoder must refuse an empty buffer and 64 bytes of garbage (bad magic/signature).
    ok &= img_must_reject("png/empty",  png_decode,  empty, 0);
    ok &= img_must_reject("png/junk",   png_decode,  junk,  sizeof(junk));
    ok &= img_must_reject("bmp/empty",  bmp_decode,  empty, 0);
    ok &= img_must_reject("bmp/junk",   bmp_decode,  junk,  sizeof(junk));
    ok &= img_must_reject("gif/empty",  gif_decode,  empty, 0);
    ok &= img_must_reject("gif/junk",   gif_decode,  junk,  sizeof(junk));
    ok &= img_must_reject("jpeg/empty", jpeg_decode, empty, 0);
    ok &= img_must_reject("jpeg/junk",  jpeg_decode, junk,  sizeof(junk));

    // Mutate the valid 4x4 RGB PNG into hostile variants. IHDR layout: [16..19]=width,
    // [20..23]=height, [25]=colour type; its CRC is at [29..32] over the type+data at
    // [12..28]. v6.4.97 added chunk-CRC validation, so after a DATA mutation we recompute
    // that CRC (PNG_FIXCRC) — otherwise the CRC gate would catch it first and these cases
    // would no longer reach the specific field check they are meant to exercise (a real
    // attacker recomputes the CRC too). The dedicated bad-crc case leaves the CRC wrong on
    // purpose to prove the new gate itself refuses a tampered chunk.
    uint8_t m[sizeof(PNG_RGB)];
    #define PNG_RESET() do { for (uint32_t _i = 0; _i < sizeof(PNG_RGB); _i++) m[_i] = PNG_RGB[_i]; } while (0)
    #define PNG_FIXCRC() do { uint32_t _c = crc32_calc(&m[12], 17); \
        m[29]=(uint8_t)(_c>>24); m[30]=(uint8_t)(_c>>16); m[31]=(uint8_t)(_c>>8); m[32]=(uint8_t)_c; } while (0)
    PNG_RESET(); m[16] = m[17] = m[18] = m[19] = 0; PNG_FIXCRC();   // zero width
    ok &= img_must_reject("png/zero-w", png_decode, m, sizeof(m));
    PNG_RESET(); m[16] = 0; m[17] = 0; m[18] = 0x20; m[19] = 0x00; PNG_FIXCRC(); // width 8192 > PNG_MAX_DIM
    ok &= img_must_reject("png/huge-w", png_decode, m, sizeof(m));
    PNG_RESET(); m[25] = 7; PNG_FIXCRC();                          // invalid colour type
    ok &= img_must_reject("png/bad-ctype", png_decode, m, sizeof(m));
    PNG_RESET(); m[1] = 0x00;                                       // corrupt signature
    ok &= img_must_reject("png/bad-sig", png_decode, m, sizeof(m));
    PNG_RESET();                                                    // truncated stream (IDAT cut off)
    ok &= img_must_reject("png/truncated", png_decode, m, 40);
    PNG_RESET(); m[29] ^= 0xFF;                                    // tamper the IHDR CRC (data intact)
    ok &= img_must_reject("png/bad-crc", png_decode, m, sizeof(m));
    #undef PNG_FIXCRC
    #undef PNG_RESET

    if (ok) printf("imgreject: all hostile inputs rejected PASS\n");
    return ok ? 0 : -1;
}
