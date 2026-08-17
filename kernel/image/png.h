#ifndef PNG_H
#define PNG_H
// Minimal PNG decoder (on top of inflate.c). Decodes 8-bit, non-interlaced PNGs of color types
// grayscale / RGB / palette / gray+alpha / RGBA into a flat RGBA buffer. See png.c.
#include "../core/kernel.h"
#include "image.h"

// Decode a PNG image. On success returns 0 and fills img (allocating img->pixels — release it with
// png_free); returns a negative code on error (bad signature, unsupported format, corrupt stream).
int  png_decode(const uint8_t* src, uint32_t srclen, image_t* img);
void png_free(image_t* img);

// Encode w*h XRGB (0xFFRRGGBB) pixels as an 8-bit RGB truecolor PNG (filter None) into dst[cap],
// reusing zlib_deflate for IDAT + crc32_calc for chunk CRCs. `raw` is caller-owned scratch of at
// least h*(1+w*3) bytes (keeps the encoder heap-free). Returns bytes written, or 0 on overflow.
uint32_t png_encode(const uint32_t* px, uint32_t w, uint32_t h,
                    uint8_t* dst, uint32_t cap, uint8_t* raw, uint32_t rawcap);
int  png_encode_selftest(void);   // KAT: encode a fixed image, decode it back, require exact pixels

// Known-answer self-test (`pngtest`): decode embedded PNGs of each color type + all filter types.
int  png_selftest(void);

// Adversarial self-test (`imgreject`): every image decoder (png/bmp/gif/jpeg) must REFUSE
// malformed/hostile input (empty, garbage, and PNGs with zero/oversized dimensions, a bad
// colour type, a corrupt signature or a truncated stream) with a negative return, no crash.
int  image_reject_selftest(void);

#endif
