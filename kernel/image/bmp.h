#ifndef BMP_H
#define BMP_H
// Minimal BMP (Windows bitmap) decoder. Handles the uncompressed (BI_RGB) BITMAPINFOHEADER shapes
// the web still uses: 24-bit BGR, 32-bit BGRX, and 8-bit palette, bottom-up or top-down. See bmp.c.
#include "../core/kernel.h"
#include "image.h"

// Decode a BMP into RGBA. 0 on success (fills img, allocates img->pixels — free with png_free /
// image kfree); negative on error (bad header / unsupported compression or depth).
int bmp_decode(const uint8_t* src, uint32_t srclen, image_t* img);

// Known-answer self-test (`bmptest`): 24/32/8-bit + a top-down image, vs expected RGBA.
int bmp_selftest(void);

#endif
