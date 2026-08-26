#ifndef IMAGEVIEW_WIN_H
#define IMAGEVIEW_WIN_H

#include "../../core/kernel.h"
#include "../core/compositor.h"
#include "../../image/gif.h"      // gif_anim_t / gif_frame_t for animated-GIF playback

typedef struct {
    uint8_t* pixels;              // RGBA of the CURRENT frame: owned for a static image, or an
                                  // alias into anim.frames[cur_frame].pixels for an animated GIF.
    uint32_t img_w, img_h;
    int offset_x, offset_y;
    float zoom;
    char filename[64];
    char dir[192];                // directory of the loaded file, for n/p folder navigation
    char status[64];

    // Animated-GIF playback. anim.frames != NULL && anim.nframes > 1 => animated; the ctx OWNS
    // anim (freed with gif_anim_free). Timing mirrors Selene's sel_img_t: accumulate tick time on
    // the current frame, step when its delay elapses, and honour the NETSCAPE loop count.
    gif_anim_t anim;
    int      cur_frame;
    uint32_t anim_ms;             // milliseconds accumulated on the current frame
    int      loops_done;

    int      fit_pending;         // 1 => on the next draw, pick a zoom that fits the image to the window
} imageview_win_t;

imageview_win_t* imageview_create_ctx(void);
void imageview_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void imageview_win_key(window_t* win, int key);
int  imageview_win_tick(window_t* win);     // ~30fps tick: advance an animated GIF; 1 if the frame flipped
void imageview_win_close(window_t* win);    // free the decoded pixels / GIF frames (ctx itself freed by window_destroy)

// Load an image file (PNG / BMP / GIF / JPEG, dispatched by magic bytes) by path into the viewer.
// Returns 0 on success; negative on error, leaving any previously-loaded image intact and setting a
// status message. An animated GIF begins playing on the next tick.
int  imageview_open_file(imageview_win_t* iv, const char* path);

#endif
