#ifndef PNG_H
#define PNG_H
// Minimal PNG decoder (on top of inflate.c). Decodes 8-bit, non-interlaced PNGs of color types
// grayscale / RGB / palette / gray+alpha / RGBA into a flat RGBA buffer. See png.c.
#include "kernel.h"

typedef struct {
    uint32_t width, height;
    uint8_t* pixels;            // RGBA, 4 bytes/pixel, width*height*4 (kmalloc'd by png_decode)
} png_image_t;

// Decode a PNG image. On success returns 0 and fills img (allocating img->pixels — release it with
// png_free); returns a negative code on error (bad signature, unsupported format, corrupt stream).
int  png_decode(const uint8_t* src, uint32_t srclen, png_image_t* img);
void png_free(png_image_t* img);

// Known-answer self-test (`pngtest`): decode embedded PNGs of each color type + all filter types.
int  png_selftest(void);

#endif
