#ifndef IMAGE_H
#define IMAGE_H
// A decoded raster image: width x height RGBA pixels (4 bytes each), kmalloc'd. Shared by every
// image decoder (png.c, bmp.c, ...) so Selene can hold and draw them format-agnostically.
#include "kernel.h"

typedef struct {
    uint32_t width, height;
    uint8_t* pixels;            // RGBA, 4 bytes/pixel, width*height*4
} image_t;

#endif
