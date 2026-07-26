#ifndef GIF_H
#define GIF_H
// Minimal GIF decoder: decodes the first image of a GIF87a/GIF89a into RGBA — global/local color
// table, LZW decompression, interlacing, and a transparent color index. See gif.c.
#include "kernel.h"
#include "image.h"

// Decode the first frame of a GIF into RGBA. 0 on success (fills img, allocates img->pixels — free
// with kfree); negative on error / unsupported feature.
int gif_decode(const uint8_t* src, uint32_t srclen, image_t* img);

// Known-answer self-test (`giftest`): basic palette, transparency, interlaced, and a dictionary-
// growing image, each vs expected RGBA.
int gif_selftest(void);

#endif
