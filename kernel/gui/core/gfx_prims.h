#ifndef NYX_GUI_GFX_PRIMS_H
#define NYX_GUI_GFX_PRIMS_H
// Low-level graphics + input primitives — the driver-layer surface the compositor and every GUI
// window draws through: VBE mode setting (drivers/video/vbe.c), the software framebuffer
// (drivers/video/framebuffer.c: pixels, rects, rounded rects, gradients, blits, the back buffer),
// the PS/2 mouse (drivers/input/mouse.c), and the bitmap font (drivers/video/font.c).
//
// Split out of the core/kernel.h god-header as one cohesive module boundary — the same
// incremental per-subsystem header split the launchers / netstack rungs began (see the
// architecture-modularity thread). kernel.h re-includes this header, so every existing includer
// keeps seeing these prototypes unchanged, while a GUI-only consumer can include just this
// focused header. Depends only on the fixed-width integer types.
#include "../../core/types.h"

int vbe_init(void);
int vbe_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
uint32_t vbe_get_width(void);
uint32_t vbe_get_height(void);
uint32_t vbe_get_bpp(void);
void* vbe_get_lfb(void);
void* vbe_map_lfb(uint64_t phys, uint32_t bytes);   // map a bootloader/GOP LFB into the kernel window

void fb_init(uint32_t width, uint32_t height, uint32_t bpp, void* addr);
void fb_init_ex(uint32_t width, uint32_t height, uint32_t bpp, void* addr, uint32_t stride_px);  // + hw pitch
void fb_debug_banner(void);   // early real-HW "framebuffer is live" signal (Nyx-purple top bar)
void fb_set_rotation(int deg); // 0/90/180/270 CW display rotation (portrait panels); set before fb_init
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_darken_rect(int x, int y, int w, int h, uint8_t shade);  // mix rect toward black (drop shadows)
void fb_fill_vgrad(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t top, uint32_t bottom);  // vertical gradient fill
uint32_t fb_isqrt(uint32_t x);                                          // integer sqrt
int  fb_corner_inset(int d, int R);                                     // rounded-corner row inset
void fb_fill_round_rect(int x, int y, int w, int h, int R, uint32_t col);    // all-4-corner rounded fill
void fb_stroke_round_rect(int x, int y, int w, int h, int R, uint32_t col);  // matching rounded outline
void fb_set_round_clip(int x, int y, int w, int h, int r);  // mask fills/blits to a rect w/ rounded bottom corners
void fb_clear_clip(void);
void fb_set_region_clip(int x, int y, int w, int h);        // outer damage-rect clip, INTERSECTS the round clip (dirty-rect groundwork)
int  fb_clip_active(void);                                  // 1 if a round/region clip is up (per-glyph gate for direct blitters)
int  fb_pixel_visible(int x, int y);                        // 1 if (x,y) passes the active round/region clip
void fb_clear_region_clip(void);
void fb_blit(const void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);
void fb_clear(uint32_t color);
int  fb_enable_backbuffer(void);   // opt-in double buffering (compositor)
void fb_present(void);             // blit the back buffer to the LFB (one frame)
void fb_present_rect(int x, int y, int w, int h);  // publish only a sub-rect (e.g. cursor move)
int  fb_fullscreen_active(void);   // a userspace app is driving the whole screen right now
void fb_query(uint32_t* w, uint32_t* h, uint32_t* bpp);              // SYS_FBINFO
void fb_present_kbuf(const uint32_t* src, uint32_t sw, uint32_t sh); // scale+blit a KERNEL buffer
void fb_use_lfb_direct(void);      // repoint drawing at the hardware LFB (panic screen)
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
void* fb_get_addr(void);
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

int mouse_init(void);
void mouse_irq_handler(void* unused);
int mouse_get_x(void);
int mouse_get_y(void);
int mouse_get_buttons(void);
int mouse_get_z(void);          // accumulated wheel notches (IntelliMouse); +up / -down
void mouse_set_pos(int x, int y);
void mouse_kbd_click(int right);   // MouseKeys: synthesize a left(0)/right(1) click at the cursor

void font_draw_char(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg);
void font_draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);
void font_draw_string_trans(uint32_t x, uint32_t y, const char* str, uint32_t fg);  // transparent bg
void font_draw_char_scaled(uint32_t x, uint32_t y, unsigned char c, uint32_t fg, uint32_t bg, uint32_t scale);
void font_draw_string_scaled(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg, uint32_t scale);
uint32_t font_get_width(void);
uint32_t font_get_height(void);

#endif // NYX_GUI_GFX_PRIMS_H
