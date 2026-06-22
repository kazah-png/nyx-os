#ifndef FONT_H
#define FONT_H

#include "kernel.h"

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

// Draw a single glyph at (x, y) in 32bpp framebuffer
void font_draw_char(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg);

// Draw a null-terminated string at (x, y)
void font_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);

// Get the width/height of a character
uint32_t font_get_width(void);
uint32_t font_get_height(void);

#endif
