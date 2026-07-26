#ifndef GIF_H
#define GIF_H
// Minimal GIF decoder: decodes the first image of a GIF87a/GIF89a into RGBA — global/local color
// table, LZW decompression, interlacing, and a transparent color index. See gif.c.
#include "../core/kernel.h"
#include "image.h"

// Decode the first frame of a GIF into RGBA. 0 on success (fills img, allocates img->pixels — free
// with kfree); negative on error / unsupported feature.
int gif_decode(const uint8_t* src, uint32_t srclen, image_t* img);

// One frame of an animated GIF: fully-composited RGBA at the logical-screen size + its display delay.
typedef struct { uint8_t* pixels; uint16_t delay_cs; } gif_frame_t;   // delay in centiseconds (1/100 s)
// A decoded (possibly animated) GIF: logical-screen size + the array of composited frames + the
// NETSCAPE loop count (0 = loop forever; N = play N times then stop; default 0 when unspecified).
typedef struct { uint32_t width, height; int nframes; gif_frame_t* frames; int loop_count; } gif_anim_t;

// Decode ALL frames of a GIF into composited RGBA frames — honours each frame's sub-rectangle,
// transparent index, and disposal method (leave / restore-to-background / restore-to-previous),
// so every frame is a ready-to-blit logical-screen image. 0 on success (fills out, allocates
// out->frames + each frame's pixels — free with gif_anim_free); negative on error. A static
// (single-image) GIF yields nframes == 1.
int  gif_decode_anim(const uint8_t* src, uint32_t srclen, gif_anim_t* out);
void gif_anim_free(gif_anim_t* a);

// Known-answer self-test (`giftest`): static (basic palette, transparency, interlaced, dictionary-
// growing) + animated (multi-frame count + per-frame RGBA + delays), each vs expected.
int gif_selftest(void);

#endif
