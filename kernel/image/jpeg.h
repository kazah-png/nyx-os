#ifndef JPEG_H
#define JPEG_H
#include "../core/kernel.h"
#include "image.h"

// Decode a BASELINE (sequential DCT, Huffman-coded, 8-bit) JPEG into RGBA. Handles grayscale and
// YCbCr (4:4:4 / 4:2:2 / 4:2:0 chroma subsampling) with restart markers. 0 on success (fills img,
// allocates img->pixels — free with kfree); negative on error or unsupported feature (progressive,
// arithmetic, 12-bit, or 4-component CMYK/YCCK all fail cleanly rather than mis-decode).
int jpeg_decode(const uint8_t* src, uint32_t srclen, image_t* img);

// Known-answer self-test (`jpegtest`): grayscale + 4:4:4 + 4:2:0 test images, each decoded and
// compared to Pillow's reference decode within a small tolerance (exact match is impossible across
// IDCT implementations — libjpeg's integer IDCT differs from any other by a rounding step).
int jpeg_selftest(void);

#endif
