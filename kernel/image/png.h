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

// Known-answer self-test (`pngtest`): decode embedded PNGs of each color type + all filter types.
int  png_selftest(void);

#endif
