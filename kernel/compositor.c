#include "kernel.h"
#include "compositor.h"
#include "theme.h"
#include "font.h"
#include "terminal_win.h"
#include "fileman_win.h"
#include "paint_win.h"
#include "taskman_win.h"
#include "editor_win.h"
#include "imageview_win.h"
#include "soundtest_win.h"
#include "calc_win.h"
#include "minesweeper_win.h"
#include "wallpaper_win.h"
#include "rtc.h"
#include "login.h"
#include "auth.h"

static window_t* windows[MAX_WINDOWS];
static int window_count = 0;
static int next_id = 100;
static int focused_id = 0;
static int drag_id = 0;
static int resize_id = 0;
static int quit = 0;
static int compositor_active = 0;
static int current_workspace = 0;

static uint32_t taskbar_bg, taskbar_fg, taskbar_hl;
static uint32_t desktop_bg, title_active, title_inactive;

static int start_menu_open = 0;
static int user_menu_open = 0;      // the taskbar user badge's popup menu
int compositor_logout_requested = 0; // set by the user menu; boot loop re-shows login
static int ctx_menu_open = 0;
static int ctx_menu_x = 0, ctx_menu_y = 0;
static int mouse_x = 0, mouse_y = 0;
static int mouse_z = 0;          // previous wheel total (see mouse_get_z); delta = new - this
static uint8_t mouse_btns = 0;
// Set by redraw_all() whenever it recomposites the back buffer; the event loop
// clears it after fb_present(). This means *any* redraw_all() caller — including
// menu/drag handlers that call it directly (not via the loop's `redraw` flag) —
// gets its finished frame published, without every caller having to remember to.
static int frame_dirty = 0;

#define CURSOR_W 12
#define CURSOR_H 16
static const uint8_t cursor_data[CURSOR_H][CURSOR_W] = {
    {1,1,0,0,0,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},{1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},{1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},{1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},{1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,2,2,2,2,1},{1,2,2,2,2,2,1,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},{1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,1,2,2,1,0,0,0},{0,0,0,0,0,0,1,2,2,1,0,0},
};
static uint32_t cursor_bg_buf[CURSOR_H * CURSOR_W];
static int cursor_saved = 0;

static inline uint32_t win_client_x(window_t* w) { return w->x; }
static inline uint32_t win_client_y(window_t* w) { return w->y + TITLE_H; }
static inline uint32_t win_total_h(window_t* w) { return w->h + TITLE_H; }

static void compositor_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (x >= fw || y >= fh) return;
    ((uint32_t*)fb_get_addr())[y * fw + x] = color;
}

static void save_cursor_bg(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++)
            cursor_bg_buf[y * CURSOR_W + x] = ((uint32_t*)fb_get_addr())[(my + y) * fw + (mx + x)];
    cursor_saved = 1;
}

static void restore_cursor_bg(int mx, int my) {
    if (!cursor_saved) return;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++)
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++)
            ((uint32_t*)fb_get_addr())[(my + y) * fw + (mx + x)] = cursor_bg_buf[y * CURSOR_W + x];
}

static void draw_cursor(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (fw == 0 || fh == 0) return;
    uint32_t* fb = (uint32_t*)fb_get_addr();
    if (!fb) return;

    for (int y = 0; y < CURSOR_H && my + y < (int)fh; y++) {
        for (int x = 0; x < CURSOR_W && mx + x < (int)fw; x++) {
            uint8_t p = cursor_data[y][x];
            if (p == 0) continue;
            if (p == 2)
                fb[(my + y) * fw + (mx + x)] = 0xFFFFFF;
            else
                fb[(my + y) * fw + (mx + x)] = 0x000000;
        }
    }
}

static void draw_x_button(int x, int y, int size, uint32_t color) {
    fb_fill_rect(x, y, size, size, fb_rgb(200,40,40));
    int pad = 4;
    for (int i = 0; i < size - pad * 2; i++) {
        compositor_draw_pixel(x + pad + i, y + pad + i, color);
        compositor_draw_pixel(x + pad + i + 1, y + pad + i, color);
        compositor_draw_pixel(x + size - pad - 1 - i, y + pad + i, color);
        compositor_draw_pixel(x + size - pad - 1 - i - 1, y + pad + i, color);
    }
}

static void draw_min_button(int x, int y, int size, uint32_t color) {
    fb_fill_rect(x, y, size, size, fb_rgb(60,60,60));
    int cy = y + size / 2;
    for (int i = 0; i < size - 6; i++)
        compositor_draw_pixel(x + 3 + i, cy, color);
}

static void draw_max_button(int x, int y, int size, uint32_t color) {
    fb_fill_rect(x, y, size, size, fb_rgb(60,120,60));
    for (int i = 1; i < size - 3; i++) {
        compositor_draw_pixel(x + 3 + i, y + 3, color);
        compositor_draw_pixel(x + 3 + i, y + size - 4, color);
        compositor_draw_pixel(x + 3, y + 3 + i, color);
        compositor_draw_pixel(x + size - 4, y + 3 + i, color);
    }
}

// Corner-rounding radius for window title bars.
#define WIN_RADIUS 7

// Rounded-corner primitives (fb_corner_inset / fb_fill_round_rect /
// fb_stroke_round_rect) and the integer sqrt now live in fb.c, shared with the
// login screen. This file keeps only the window-specific pieces below.

// The radius a given window can actually use: shrink it so a minimum-size window
// (120x80) never rounds so hard the two corners meet.
static int win_radius(window_t* win) {
    int r = WIN_RADIUS;
    if (r > (int)win->w / 2) r = (int)win->w / 2;
    if (r > TITLE_H)         r = TITLE_H;             // rounding lives in the title bar
    return r < 0 ? 0 : r;
}

// One row of a vertical gradient: linear interpolation from `top` (i=0) to
// `bottom` (i=n-1). Split out from fb_fill_vgrad so the title bar can drive the
// fill row by row and inset the rounded top corners.
static uint32_t vgrad_row(uint32_t top, uint32_t bottom, int i, int n) {
    int tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF;
    int dr = (int)((bottom >> 16) & 0xFF) - tr;
    int dg = (int)((bottom >>  8) & 0xFF) - tg;
    int db = (int)( bottom        & 0xFF) - tb;
    int den = n > 1 ? n - 1 : 1;
    return fb_rgb(tr + dr * i / den, tg + dg * i / den, tb + db * i / den);
}

// Shift a colour a percentage toward white / black. Proportional (not a flat
// per-channel add) so the hue is preserved as it lightens or darkens.
static uint32_t col_lighten(uint32_t c, int pct) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return fb_rgb(r + (255 - r) * pct / 100,
                  g + (255 - g) * pct / 100,
                  b + (255 - b) * pct / 100);
}
static uint32_t col_darken(uint32_t c, int pct) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return fb_rgb(r - r * pct / 100, g - g * pct / 100, b - b * pct / 100);
}

// A button filled with the same top-lit vertical gradient the title bars use,
// centred on `base` (±amt), with a transparent-background label so the text sits
// directly on the gradient. Used for the taskbar's Menu button and window tabs.
static void draw_grad_button(int x, int y, int w, int h, uint32_t base, uint32_t fg,
                             const char* label, int amt) {
    fb_fill_vgrad(x, y, w, h, col_lighten(base, amt), col_darken(base, amt));
    if (label) {
        int tw = (int)strlen(label) * FONT_WIDTH;
        font_draw_string_trans(x + (w - tw) / 2, y + (h - FONT_HEIGHT) / 2, label, fg);
    }
}

static void draw_titlebar(window_t* win) {
    uint32_t base = win->focused ? title_active : title_inactive;
    // A subtle top-lit vertical gradient gives the bar some dimension to sit with
    // the drop shadows. It is centred on `base` (lighten top, darken bottom by the
    // same amount), so the mid rows — where the title text sits — are ~base; the
    // text is drawn with a transparent background over it, so no flat panel breaks
    // the gradient. Focused bars get a touch more sheen than inactive ones.
    int amt = win->focused ? 16 : 8;
    uint32_t gtop = col_lighten(base, amt), gbot = col_darken(base, amt);
    // Draw the gradient row by row so the top two corners can be rounded: the
    // first R rows are inset by the arc, leaving the corner pixels unpainted for
    // the desktop/lower window to show through. Only the TOP is rounded — the
    // bottom of the title bar meets the (square) body, and app draw callbacks
    // fill the body edge-to-edge, so rounding there would just be squared off.
    int R = win_radius(win);
    for (uint32_t row = 0; row < TITLE_H; row++) {
        int inset = (row < (uint32_t)R) ? fb_corner_inset((int)row, R) : 0;
        fb_fill_rect(win->x + inset, win->y + row, win->w - 2 * inset, 1,
                     vgrad_row(gtop, gbot, (int)row, TITLE_H));
    }

    int y_off = win->y + (TITLE_H - FONT_HEIGHT) / 2;
    font_draw_string_trans(win->x + 4, y_off, win->title, THEME_TITLE_TEXT);

    int bx = win->x + win->w - CLOSE_W - 2;
    if (win->has_close) {
        draw_x_button(bx, win->y + 3, CLOSE_W, THEME_TITLE_TEXT);
        bx -= CLOSE_W + 2;
    }
    if (win->has_max) {
        draw_max_button(bx, win->y + 3, CLOSE_W, THEME_TITLE_TEXT);
        bx -= CLOSE_W + 2;
    }
    if (win->has_min) {
        draw_min_button(bx, win->y + 3, CLOSE_W, THEME_TITLE_TEXT);
    }
}

static void draw_window_frame(window_t* win) {
    // The frame carries the focus state too, not just the title bar: a focused
    // window is outlined in the accent so its border and title strip read as one
    // continuous edge. Previously this bevel was identical on every window, so a
    // window buried under others gave no focus cue at all once its title bar was
    // covered.
    uint32_t hi = win->focused ? THEME_ACCENT     : THEME_FRAME_HI;
    uint32_t lo = win->focused ? THEME_ACCENT_DIM : THEME_FRAME_LO;
    int x = win->x, y = win->y, w = (int)win->w, H = (int)win_total_h(win);
    int R = win_radius(win);

    // Straight edges. Top runs between the rounded corners; the sides start below
    // the top arc; the bottom is square (the body isn't rounded — see draw_titlebar).
    fb_fill_rect(x + R, y - 1, w - 2 * R, 1, hi);   // top
    fb_fill_rect(x - 1, y + H, w + 2,     1, lo);   // bottom
    fb_fill_rect(x - 1, y + R, 1, H - R, hi);       // left
    fb_fill_rect(x + w, y + R, 1, H - R, lo);       // right

    // Top corner arcs, one pixel outside the title bar's rounded corners so the
    // border hugs the same curve the fill leaves.
    for (int row = 0; row < R; row++) {
        int inset = fb_corner_inset(row, R);
        fb_put_pixel((uint32_t)(x + inset - 1),  (uint32_t)(y + row), hi);   // top-left
        fb_put_pixel((uint32_t)(x + w - inset),  (uint32_t)(y + row), hi);   // top-right
    }
}

static int titlebar_hit(window_t* win, int mx, int my) {
    return mx >= win->x && mx < win->x + (int)win->w
        && my >= win->y && my < win->y + TITLE_H;
}

static int close_hit(window_t* win, int mx, int my) {
    if (!win->has_close) return 0;
    int bx = win->x + win->w - CLOSE_W - 2;
    return mx >= bx && mx < bx + CLOSE_W
        && my >= win->y + 3 && my < win->y + 3 + CLOSE_W;
}

static int max_hit(window_t* win, int mx, int my) {
    if (!win->has_max) return 0;
    int bx = win->x + win->w - CLOSE_W - 2 - (win->has_close ? CLOSE_W + 2 : 0);
    return mx >= bx && mx < bx + CLOSE_W
        && my >= win->y + 3 && my < win->y + 3 + CLOSE_W;
}

static int min_hit(window_t* win, int mx, int my) {
    if (!win->has_min) return 0;
    int bx = win->x + win->w - CLOSE_W - 2 - (win->has_close ? CLOSE_W + 2 : 0) - (win->has_max ? CLOSE_W + 2 : 0);
    return mx >= bx && mx < bx + CLOSE_W
        && my >= win->y + 3 && my < win->y + 3 + CLOSE_W;
}

static int window_hit(window_t* win, int mx, int my) {
    return mx >= win->x && mx < win->x + (int)win->w
        && my >= win->y && my < win->y + (int)win_total_h(win);
}

static int resize_hit(window_t* win, int mx, int my, int* dir) {
    int rx = win->x + win->w, by = win->y + win_total_h(win);
    int lx = win->x, ty = win->y;
    int client_top = win->y + TITLE_H;

    int on_left   = mx >= lx - RESIZE_MARGIN && mx < lx + RESIZE_MARGIN;
    int on_right  = mx >= rx - RESIZE_MARGIN && mx < rx + RESIZE_MARGIN;
    int on_top    = my >= ty - RESIZE_MARGIN && my < ty + RESIZE_MARGIN;
    int on_bottom = my >= by - RESIZE_MARGIN && my < by + RESIZE_MARGIN;
    int in_client = my >= client_top && my < by;
    int in_vrange = my >= ty && my < by;

    if (on_top && on_left)    { *dir = RESIZE_LEFT_TOP;     return 1; }
    if (on_top && on_right)   { *dir = RESIZE_RIGHT_TOP;    return 1; }
    if (on_bottom && on_left) { *dir = RESIZE_LEFT_BOTTOM;  return 1; }
    if (on_bottom && on_right){ *dir = RESIZE_CORNER;       return 1; }
    if (on_left && in_vrange) { *dir = RESIZE_LEFT;         return 1; }
    if (on_right && in_client){ *dir = RESIZE_RIGHT;        return 1; }
    if (on_top && in_vrange)  { *dir = RESIZE_TOP;          return 1; }
    if (on_bottom && in_vrange){ *dir = RESIZE_BOTTOM;      return 1; }
    return 0;
}

static int start_hit(int mx, int my) {
    return mx >= 2 && mx < 2 + 80 && my >= (int)(fb_get_height() - TASKBAR_H) && my < (int)fb_get_height();
}

__attribute__((unused)) static int taskbar_clock_hit(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    return mx >= (int)(fw - CLOCK_W - 4) && mx < (int)(fw - 2)
        && my >= (int)(fh - TASKBAR_H) && my < (int)fh;
}

static int taskbar_win_hit(int mx, int my, int* id) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int bx = 90;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i] || !windows[i]->visible) continue;
        // Must match the window-selection predicate in draw_taskbar() exactly,
        // otherwise the button layout (bx) drifts out of sync and clicks land on
        // the wrong window. draw_taskbar() also lists minimized windows from other
        // workspaces (so they can be restored), so this hit-test must too.
        if (windows[i]->workspace != current_workspace && windows[i]->state != WSTATE_MINIMIZED) continue;
        int bw = 150;
        if (bx + bw > (int)(fw - CLOCK_W - 8)) bw = (int)(fw - CLOCK_W - 8) - bx;
        if (bw < 40) break;
        if (mx >= bx && mx < bx + bw && my >= (int)(fh - TASKBAR_H + 4) && my < (int)fh - 4) {
            *id = windows[i]->id;
            return 1;
        }
        bx += bw + 2;
    }
    return 0;
}

static int start_menu_hit(int mx, int my) {
    if (!start_menu_open) return 0;
    uint32_t fh = fb_get_height();
    int sm_x = 2;
    int sm_y = (int)fh - TASKBAR_H - START_H;
    return mx >= sm_x && mx < sm_x + START_W && my >= sm_y && my < sm_y + START_H;
}

/* THE start-menu geometry. draw_start_menu() lays the entries out from these and
 * so does the hit test — they used to derive it separately and disagreed on
 * every one of the three numbers:
 *
 *   - the first entry starts START_ITEM_Y (28) below the menu top, under the
 *     green header, but the hit test subtracted 8. Twenty pixels of skew means
 *     the lower two thirds of every entry selected the one BELOW it: clicking
 *     "Terminal" opened Settings.
 *   - the menu has START_ITEM_N entries and the test rejected `>= 12`, so the
 *     thirteenth — Minesweeper — could not be launched from the menu at all.
 *   - C integer division truncates toward ZERO, so a click on the header gave
 *     (my - sm_y - 8) / 28 == 0 rather than something negative, and the `< 0`
 *     guard never fired: clicking the "NyxOS Menu" title launched File Manager.
 *
 * Same lesson as the TITLE_H family in compositor.h — the fix is one definition
 * both sides read, not two derivations that have to be kept in agreement. */
#define START_HDR_H   24    /* accent brand header band */
#define START_ITEM_Y  28    /* first entry's top, relative to the menu top */
#define START_ITEM_H  28    /* entry pitch: 26 px body + 1 px separator + 1 */
#define START_ITEM_N  13    /* entries in the menu (indices 0..12) */

static int start_menu_item_hit(int mx, int my, int* idx) {
    if (!start_menu_open) return 0;
    uint32_t fh = fb_get_height();
    int sm_x = 2;
    int sm_y = (int)fh - TASKBAR_H - START_H;
    if (mx < sm_x || mx >= sm_x + START_W || my < sm_y || my >= sm_y + START_H) return 0;
    int rel = my - sm_y - START_ITEM_Y;
    if (rel < 0) return 0;                 // the header, not an entry
    *idx = rel / START_ITEM_H;
    if (*idx >= START_ITEM_N) return 0;    // below the last entry
    return 1;
}

static void draw_taskbar(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int tb_y = fh - TASKBAR_H;

    // Subtle top-lit gradient bar with a 1px highlight along its top edge, to sit
    // with the windows' gradient title bars rather than reading as a flat slab.
    fb_fill_vgrad(0, tb_y, fw, TASKBAR_H, col_lighten(taskbar_bg, 12), col_darken(taskbar_bg, 10));
    fb_fill_rect(0, tb_y, fw, 1, col_lighten(taskbar_bg, 34));

    // The Menu button is the brand launcher, so it always wears the accent (dimmed
    // when the menu is closed, full-strength when open) instead of blending in.
    draw_grad_button(2, tb_y + 4, 80, TASKBAR_H - 8,
                     start_menu_open ? THEME_ACCENT : col_darken(THEME_ACCENT, 22),
                     fb_rgb(255, 255, 255), "Menu", 16);

    // Reserve room for the logged-in user badge (avatar + name), left of the clock.
    int av_s = TASKBAR_H - 14;
    int ublock_w = av_s + 6 + (int)strlen(g_login_user) * FONT_WIDTH + 10;
    int right_limit = (int)(fw - CLOCK_W - 8) - ublock_w;
    if (right_limit < 90) right_limit = 90;

    int bx = 90;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i] || !windows[i]->visible) continue;
        if (windows[i]->workspace != current_workspace && windows[i]->state != WSTATE_MINIMIZED) continue;
        int bw = 150;
        if (bx + bw > right_limit) bw = right_limit - bx;
        if (bw < 40) break;
        // Focused window tab wears the accent gradient (matching its title bar);
        // an unfocused one gets a quiet bar gradient; a minimized one a flatter,
        // greyer fill so it reads as dormant.
        int bh = TASKBAR_H - 8;
        if (windows[i]->state == WSTATE_MINIMIZED)
            fb_fill_vgrad(bx, tb_y + 4, bw, bh, fb_rgb(58, 58, 64), fb_rgb(44, 44, 50));
        else if (windows[i]->focused)
            fb_fill_vgrad(bx, tb_y + 4, bw, bh, col_lighten(taskbar_hl, 16), col_darken(taskbar_hl, 16));
        else
            fb_fill_vgrad(bx, tb_y + 4, bw, bh, col_lighten(taskbar_bg, 10), col_darken(taskbar_bg, 10));
        if (windows[i]->title[0])
            font_draw_string_trans(bx + 4, tb_y + (TASKBAR_H - FONT_HEIGHT) / 2,
                                   windows[i]->title, fb_rgb(230, 230, 235));
        bx += bw + 2;
    }

    // Logged-in user badge: profile picture + username.
    int ubx = (int)(fw - CLOCK_W - 8) - ublock_w + 4;
    draw_avatar(ubx, tb_y + 7, av_s, g_login_avatar, 0);
    font_draw_string_trans(ubx + av_s + 6, tb_y + (TASKBAR_H - FONT_HEIGHT) / 2,
                           g_login_user, fb_rgb(215, 215, 235));

    rtc_time_t rt;
    rtc_read_time(&rt);
    char timebuf[16];
    snprintf(timebuf, sizeof(timebuf), "%02u:%02u", rt.hour, rt.minute);
    fb_fill_rect(fw - CLOCK_W - 4, tb_y + 4, CLOCK_W, TASKBAR_H - 8, fb_rgb(30,30,35));
    font_draw_string(fw - CLOCK_W - 2 + (CLOCK_W - strlen(timebuf) * FONT_WIDTH) / 2,
                     tb_y + (TASKBAR_H - FONT_HEIGHT) / 2, timebuf, fb_rgb(180,180,200), fb_rgb(30,30,35));
}

static void draw_start_menu(void) {
    if (!start_menu_open) return;
    uint32_t fh = fb_get_height();
    int sm_x = 2, sm_y = fh - TASKBAR_H - START_H;

    int R = WIN_RADIUS;

    // Rounded-top body. The bottom sits flush on the taskbar, so it stays square
    // there — the menu reads as growing out of the bar. Corner-outside pixels are
    // left unpainted (same technique as the window title bars), showing whatever
    // is behind the menu through the curve.
    for (int row = 0; row < START_H; row++) {
        int inset = (row < R) ? fb_corner_inset(row, R) : 0;
        fb_fill_rect(sm_x + inset, sm_y + row, START_W - 2 * inset, 1, THEME_WINDOW_BG);
    }

    // Brand header: accent gradient (matching the taskbar Menu button and title
    // bars), rounded to the same top corners, with a transparent label so the
    // gradient shows through the text.
    uint32_t hg_t = col_lighten(THEME_ACCENT, 14), hg_b = col_darken(THEME_ACCENT, 12);
    for (int row = 0; row < START_HDR_H; row++) {
        int inset = (row < R) ? fb_corner_inset(row, R) : 0;
        fb_fill_rect(sm_x + inset, sm_y + row, START_W - 2 * inset, 1,
                     vgrad_row(hg_t, hg_b, row, START_HDR_H));
    }
    font_draw_string_trans(sm_x + 8, sm_y + 4, "NyxOS Menu", THEME_ON_ACCENT);

    // Index order here IS the argument do_start_menu_action() switches on, and
    // start_menu_item_hit() computes that index from the same START_ITEM_*
    // constants this loop lays the entries out with.
    static const char* items[START_ITEM_N] = {
        "File Manager", "Text Editor", "Image Viewer", "Terminal",
        "Settings", "Task Manager", "Desktop Demo",
        "Paint", "Sound Test", "About", "Shutdown", "Calculator",
        "Minesweeper",
    };
    // The entry under the pointer is highlighted with the accent, like a hovered
    // menu item (the event loop recomposites on pointer move while the menu is
    // open, so this tracks the cursor).
    int hov = -1, hi;
    if (start_menu_item_hit(mouse_x, mouse_y, &hi)) hov = hi;

    for (int i = 0; i < START_ITEM_N; i++) {
        int iy = sm_y + START_ITEM_Y + i * START_ITEM_H;
        if ((uint32_t)(iy + START_ITEM_H) > fh - TASKBAR_H) break;
        if (i == hov) {
            fb_fill_vgrad(sm_x + 4, iy, START_W - 8, START_ITEM_H - 2,
                          col_lighten(THEME_ACCENT, 12), col_darken(THEME_ACCENT, 14));
            font_draw_string_trans(sm_x + 12, iy + 5, items[i], THEME_ON_ACCENT);
        } else {
            fb_fill_rect(sm_x + 4, iy, START_W - 8, START_ITEM_H - 2, THEME_WINDOW_BG);
            font_draw_string(sm_x + 12, iy + 5, items[i], THEME_TEXT, THEME_WINDOW_BG);
        }
        fb_fill_rect(sm_x + 4, iy + START_ITEM_H - 1, START_W - 8, 1, THEME_ROW_DIV);
    }

    // Border: rounded top corners to match the body, straight sides, square bottom
    // where it meets the taskbar.
    fb_fill_rect(sm_x + R, sm_y, START_W - 2 * R, 1, THEME_BORDER);              // top
    fb_fill_rect(sm_x, sm_y + START_H - 1, START_W, 1, THEME_BORDER);           // bottom
    fb_fill_rect(sm_x, sm_y + R, 1, START_H - R, THEME_BORDER);                 // left
    fb_fill_rect(sm_x + START_W - 1, sm_y + R, 1, START_H - R, THEME_BORDER);   // right
    for (int row = 0; row < R; row++) {
        int inset = fb_corner_inset(row, R);
        fb_put_pixel((uint32_t)(sm_x + inset),              (uint32_t)(sm_y + row), THEME_BORDER);
        fb_put_pixel((uint32_t)(sm_x + START_W - 1 - inset), (uint32_t)(sm_y + row), THEME_BORDER);
    }
}

// ---- Taskbar user-badge popup menu (change profile picture / log out) ----
#define USERMENU_W   150
#define USERMENU_ITH 26
#define USERMENU_HDR 24
#define USERMENU_N   2
static const char* usermenu_items[] = { "Change picture", "Log out" };

static void user_menu_rect(int* rx, int* ry, int* rh) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int h = USERMENU_HDR + USERMENU_N * USERMENU_ITH + 2;
    int x = (int)(fw - CLOCK_W - 8) - USERMENU_W;
    if (x < 2) x = 2;
    *rx = x; *ry = (int)(fh - TASKBAR_H) - h; if (rh) *rh = h;
}

static int user_menu_item_hit(int mx, int my, int* idx);   // defined below
static int ctx_menu_item_hit(int mx, int my, int* idx);    // defined below

static void draw_user_menu(void) {
    if (!user_menu_open) return;
    int x, y, h; user_menu_rect(&x, &y, &h);
    int R = 6;
    fb_fill_round_rect(x, y, USERMENU_W, h, R, THEME_WINDOW_BG);

    // header: accent gradient (rounded to the body's top corners) carrying the
    // logged-in user's avatar + name, with a transparent label.
    uint32_t hg_t = col_lighten(THEME_ACCENT, 14), hg_b = col_darken(THEME_ACCENT, 12);
    for (int row = 0; row < USERMENU_HDR; row++) {
        int inset = (row < R) ? fb_corner_inset(row, R) : 0;
        fb_fill_rect(x + inset, y + row, USERMENU_W - 2 * inset, 1,
                     vgrad_row(hg_t, hg_b, row, USERMENU_HDR));
    }
    draw_avatar(x + 5, y + 3, USERMENU_HDR - 6, g_login_avatar, 0);
    font_draw_string_trans(x + USERMENU_HDR + 6, y + (USERMENU_HDR - FONT_HEIGHT) / 2,
                           g_login_user, THEME_ON_ACCENT);

    int hov = -1, hi;
    if (user_menu_item_hit(mouse_x, mouse_y, &hi)) hov = hi;
    for (int i = 0; i < USERMENU_N; i++) {
        int iy = y + USERMENU_HDR + i * USERMENU_ITH;
        if (i == hov) {
            fb_fill_vgrad(x + 3, iy, USERMENU_W - 6, USERMENU_ITH - 1,
                          col_lighten(THEME_ACCENT, 12), col_darken(THEME_ACCENT, 14));
            font_draw_string_trans(x + 12, iy + (USERMENU_ITH - FONT_HEIGHT) / 2,
                                   usermenu_items[i], THEME_ON_ACCENT);
        } else {
            font_draw_string(x + 12, iy + (USERMENU_ITH - FONT_HEIGHT) / 2,
                             usermenu_items[i], THEME_TEXT, THEME_WINDOW_BG);
        }
    }
    fb_stroke_round_rect(x, y, USERMENU_W, h, R, THEME_BORDER);
}

// The taskbar user badge (avatar + name, left of the clock) is clickable; this
// mirrors the geometry draw_taskbar uses for it.
static int badge_hit(int mx, int my) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int tb_y = (int)fh - TASKBAR_H;
    int av_s = TASKBAR_H - 14;
    int ublock_w = av_s + 6 + (int)strlen(g_login_user) * FONT_WIDTH + 10;
    int ubx = (int)(fw - CLOCK_W - 8) - ublock_w + 4;
    return my >= tb_y && mx >= ubx - 4 && mx < (int)(fw - CLOCK_W - 8);
}

static int user_menu_item_hit(int mx, int my, int* idx) {
    if (!user_menu_open) return 0;
    int x, y; user_menu_rect(&x, &y, 0);
    for (int i = 0; i < USERMENU_N; i++) {
        int iy = y + USERMENU_HDR + i * USERMENU_ITH;
        if (mx >= x && mx < x + USERMENU_W && my >= iy && my < iy + USERMENU_ITH) { *idx = i; return 1; }
    }
    return 0;
}

static int user_menu_area_hit(int mx, int my) {
    if (!user_menu_open) return 0;
    int x, y, h; user_menu_rect(&x, &y, &h);
    return mx >= x && mx < x + USERMENU_W && my >= y && my < y + h;
}

static void do_user_menu_action(int idx) {
    if (idx == 0) {            // Change picture: cycle to the next avatar + persist
        g_login_avatar = (g_login_avatar + 1) % AVATAR_COUNT;
        auth_set_avatar(g_login_user, g_login_avatar);
    } else if (idx == 1) {     // Log out: unwind to the login screen (handled in the boot loop)
        compositor_logout_requested = 1;
        quit = 1;
    }
}

#define CTX_MENU_W 160
#define CTX_MENU_H 124
#define CTX_MENU_N 5
static const char* ctx_menu_items[] = {
    "New Folder", "New File", "Refresh", "Settings", "Wallpaper"
};

// Forward declarations
static void redraw_all(void);
static void do_start_menu_action(int idx);
static void settings_win_click(window_t* win, int mx, int my, int btn);

static void draw_ctx_menu(void) {
    if (!ctx_menu_open) return;
    int x = ctx_menu_x, y = ctx_menu_y;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (x + CTX_MENU_W > (int)fw) x = (int)fw - CTX_MENU_W - 2;
    if (y + CTX_MENU_H > (int)fh - TASKBAR_H) y = (int)fh - TASKBAR_H - CTX_MENU_H - 2;
    int R = 6;
    fb_fill_round_rect(x, y, CTX_MENU_W, CTX_MENU_H, R, THEME_WINDOW_BG);

    int hov = -1, hi;
    if (ctx_menu_item_hit(mouse_x, mouse_y, &hi)) hov = hi;
    for (int i = 0; i < CTX_MENU_N; i++) {
        int iy = y + 4 + i * 24;
        if (i == hov) {
            fb_fill_vgrad(x + 4, iy, CTX_MENU_W - 8, 22,
                          col_lighten(THEME_ACCENT, 12), col_darken(THEME_ACCENT, 14));
            font_draw_string_trans(x + 12, iy + 3, ctx_menu_items[i], THEME_ON_ACCENT);
        } else {
            font_draw_string(x + 12, iy + 3, ctx_menu_items[i], THEME_TEXT, THEME_WINDOW_BG);
        }
    }
    fb_stroke_round_rect(x, y, CTX_MENU_W, CTX_MENU_H, R, THEME_BORDER);
}

static int ctx_menu_hit(int mx, int my) {
    if (!ctx_menu_open) return 0;
    int x = ctx_menu_x, y = ctx_menu_y;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (x + CTX_MENU_W > (int)fw) x = (int)fw - CTX_MENU_W - 2;
    if (y + CTX_MENU_H > (int)fh - TASKBAR_H) y = (int)fh - TASKBAR_H - CTX_MENU_H - 2;
    return mx >= x && mx < x + CTX_MENU_W && my >= y && my < y + CTX_MENU_H;
}

static int ctx_menu_item_hit(int mx, int my, int* idx) {
    if (!ctx_menu_open) return 0;
    int x = ctx_menu_x, y = ctx_menu_y;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (x + CTX_MENU_W > (int)fw) x = (int)fw - CTX_MENU_W - 2;
    if (y + CTX_MENU_H > (int)fh - TASKBAR_H) y = (int)fh - TASKBAR_H - CTX_MENU_H - 2;
    if (mx < x || mx >= x + CTX_MENU_W || my < y || my >= y + CTX_MENU_H) return 0;
    *idx = (my - y - 4) / 24;
    if (*idx < 0 || *idx >= CTX_MENU_N) return 0;
    return 1;
}

static void do_ctx_menu_action(int idx) {
    ctx_menu_open = 0;
    switch (idx) {
        case 0: // New Folder
            {
                // Find an existing file manager window or create one
                for (int i = 0; i < MAX_WINDOWS; i++) {
                    if (windows[i] && strcmp(windows[i]->title, "File Manager") == 0) {
                        fileman_new_folder((fileman_win_t*)windows[i]->reserved);
                        redraw_all();
                        return;
                    }
                }
                // Create File Manager which will show the dialog
                window_t* fwin = window_create(100, 100, 550, 380, "File Manager", fileman_win_draw);
                if (fwin) {
                    fwin->reserved = fileman_create_ctx();
                    if (fwin->reserved) {
                        fwin->on_click = fileman_win_click;
                        fwin->on_key = fileman_win_key;
                        fwin->on_mousemove = fileman_win_mousemove;
                        fileman_new_folder((fileman_win_t*)fwin->reserved);
                    }
                }
            }
            break;
        case 1: // New File
            {
                for (int i = 0; i < MAX_WINDOWS; i++) {
                    if (windows[i] && strcmp(windows[i]->title, "File Manager") == 0) {
                        fileman_new_file((fileman_win_t*)windows[i]->reserved);
                        redraw_all();
                        return;
                    }
                }
                window_t* fwin = window_create(100, 100, 550, 380, "File Manager", fileman_win_draw);
                if (fwin) {
                    fwin->reserved = fileman_create_ctx();
                    if (fwin->reserved) {
                        fwin->on_click = fileman_win_click;
                        fwin->on_key = fileman_win_key;
                        fwin->on_mousemove = fileman_win_mousemove;
                        fileman_new_file((fileman_win_t*)fwin->reserved);
                    }
                }
            }
            break;
        case 2: // Refresh
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i] && strcmp(windows[i]->title, "File Manager") == 0 && windows[i]->reserved) {
                    fileman_refresh((fileman_win_t*)windows[i]->reserved);
                }
            }
            break;
        case 3: // Settings
            do_start_menu_action(4);
            break;
        case 4: // Wallpaper
            {
                window_t* wwin = window_create(180, 110, 360, 340, "Wallpaper", wallpaper_win_draw);
                if (wwin) wwin->on_click = wallpaper_win_click;
            }
            break;
    }
    redraw_all();
}

static void draw_workspace_indicator(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    int y = fh - TASKBAR_H - 10;
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        uint32_t c = (i == current_workspace) ? THEME_INDICATOR_ON : THEME_INDICATOR_OFF;
        fb_fill_rect(fw - CLOCK_W - 4 + i * 18, y, 14, 6, c);
    }
}

// The desktop background: a smooth vertical gradient built from the wallpaper base
// color the user picked (default = the brand purple). Each band scales the base's
// brightness from ~45% at the top to ~115% at the bottom (clamped), which reads as a
// soft top-to-bottom glow in whatever hue is selected. The Wallpaper app changes the
// base color (wallpaper_base_color) and the compositor repaints this on the next frame.
// Filled circle by horizontal spans (clipped to the framebuffer). Uses fb_isqrt
// (the kernel has no libm) — the same integer sqrt the rounded corners rely on.
static void bg_fill_circle(int cx, int cy, int rad, uint32_t col) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    for (int dy = -rad; dy <= rad; dy++) {
        int yy = cy + dy;
        if (yy < 0 || yy >= (int)fh) continue;
        int dx = (int)fb_isqrt((uint32_t)(rad * rad - dy * dy));
        int x0 = cx - dx, x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= (int)fw) x1 = (int)fw - 1;
        if (x1 >= x0) fb_fill_rect(x0, yy, x1 - x0 + 1, 1, col);
    }
}

// The desktop: a "Nightfall" sky — the user's wallpaper hue as a soft vertical
// gradient, overlaid with a glowing moon and a deterministic field of stars.
// Drawn first each frame, behind the icons and windows. The star layout is
// seeded by a fixed constant so it is identical on every repaint (no flicker).
static void draw_background(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    uint32_t base = wallpaper_base_color();
    int br = (int)((base >> 16) & 0xFF), bg = (int)((base >> 8) & 0xFF), bb = (int)(base & 0xFF);

    uint32_t steps = 96;
    uint32_t band_h = fh / steps + 1;
    for (uint32_t i = 0; i < steps; i++) {
        int pct = 45 + (int)(i * 70 / steps);      // 45% (top) .. 115% (bottom)
        int r = br * pct / 100; if (r > 255) r = 255;
        int g = bg * pct / 100; if (g > 255) g = 255;
        int b = bb * pct / 100; if (b > 255) b = 255;
        fb_fill_rect(0, i * band_h, fw, band_h, fb_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b));
    }

    // Moon in the upper-right, with a soft halo (concentric rings fading inward).
    int mx = (int)fw - 130, my = 96, mr = 40;
    bg_fill_circle(mx, my, mr + 14, fb_rgb(70,  60,  104));
    bg_fill_circle(mx, my, mr + 8,  fb_rgb(104, 92,  150));
    bg_fill_circle(mx, my, mr + 3,  fb_rgb(150, 138, 196));
    bg_fill_circle(mx, my, mr,      fb_rgb(214, 202, 244));
    // Faint craters for a little character.
    bg_fill_circle(mx - 12, my - 8,  6, fb_rgb(196, 184, 228));
    bg_fill_circle(mx + 10, my + 12, 8, fb_rgb(198, 186, 230));
    bg_fill_circle(mx + 4,  my - 16, 4, fb_rgb(200, 188, 232));

    // Stars — deterministic LCG, upper ~3/4 of the screen, kept clear of the moon.
    uint32_t seed = 0x9E3779B1u;
    uint32_t star_zone = fh * 3 / 4;
    for (int i = 0; i < 110; i++) {
        seed = seed * 1103515245u + 12345u;
        int sx = (int)((seed >> 9) % fw);
        seed = seed * 1103515245u + 12345u;
        int sy = (int)((seed >> 9) % star_zone);
        int ex = sx - mx, ey = sy - my;
        if (ex * ex + ey * ey < (mr + 18) * (mr + 18)) continue;   // don't scatter over the moon
        int shade = (int)((seed >> 20) & 3);
        uint32_t sc = (shade == 0) ? fb_rgb(120, 112, 156)
                    : (shade == 1) ? fb_rgb(168, 158, 202)
                                   : fb_rgb(214, 208, 240);
        int sz = (shade >= 2) ? 2 : 1;
        fb_fill_rect(sx, sy, sz, sz, sc);
    }
}

static void init_desktop_icons(void);
static void draw_desktop_icons(void);
static void settings_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);

// Drop-shadow geometry. OFFSET is how far down-right the shadow sits; RADIUS is
// how far it feathers out past the window edge; CORE is the darkening (0..255) of
// the innermost ring, fading linearly to nothing at the outermost.
#define SHADOW_OFFSET 6
#define SHADOW_RADIUS 12
#define SHADOW_CORE   100

// Cast a soft drop shadow for one window onto whatever has already been drawn
// beneath it. Called from the back-to-front composite loop right before the
// window itself, so the shadow lands on the desktop and any lower windows, and
// the window is then painted opaque on top — leaving only the soft band past the
// bottom and right edges visible, which is the drop-shadow look.
//
// Drawn as concentric 1px OUTLINES growing outward from the offset window rect,
// darkest (SHADOW_CORE) nearest the window and fading to zero at RADIUS. Outlines
// rather than filled rects means each pixel is darkened exactly once (a clean
// gradient, no compounding) and the cost is perimeter, not area — a handful of
// thin strips per window instead of RADIUS full-window fills.
static void draw_window_shadow(window_t* win) {
    // A maximized window fills the usable area, so its shadow would be entirely
    // behind it or off-screen: pure wasted work. Snapped/normal windows all cast.
    if (win->state == WSTATE_MAXIMIZED) return;
    int rx = win->x + SHADOW_OFFSET;
    // Start the shadow BELOW the rounded title-bar top, not at the window top.
    // The top corners are cut away (see draw_titlebar), so a shadow drawn up there
    // shows through the cutout as a stray dark step at the top-right; beginning it
    // at the title/body junction keeps it out of the rounded region entirely while
    // leaving the visible drop (the body's right edge and bottom) untouched.
    int ry = win->y + TITLE_H;
    int rw = (int)win->w;
    int rh = (int)win_total_h(win) + SHADOW_OFFSET - TITLE_H;   // reach to below the window bottom
    for (int r = SHADOW_RADIUS; r >= 1; r--) {
        uint8_t shade = (uint8_t)(SHADOW_CORE * (SHADOW_RADIUS - r + 1) / SHADOW_RADIUS);
        int x = rx - r, y = ry - r, w = rw + 2 * r, h = rh + 2 * r;
        fb_darken_rect(x,         y,         w, 1, shade);   // top edge
        fb_darken_rect(x,         y + h - 1, w, 1, shade);   // bottom edge
        fb_darken_rect(x,         y + 1,     1, h - 2, shade);   // left edge
        fb_darken_rect(x + w - 1, y + 1,     1, h - 2, shade);   // right edge
    }
}

static void redraw_all(void) {
    draw_background();
    draw_desktop_icons();

    window_t* sorted[MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->visible && windows[i]->state != WSTATE_MINIMIZED
            && windows[i]->workspace == current_workspace)
            sorted[n++] = windows[i];

    for (int i = 1; i < n; i++) {
        window_t* key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j]->z_order > key->z_order) { sorted[j+1] = sorted[j]; j--; }
        sorted[j+1] = key;
    }

    for (int i = 0; i < n; i++) {
        window_t* win = sorted[i];
        draw_window_shadow(win);      // cast onto lower windows/desktop, before this window paints
        fb_fill_rect(win->x, win->y + TITLE_H, win->w, win->h, fb_rgb(35,35,40));
        draw_window_frame(win);
        draw_titlebar(win);
        if (win->draw)
            win->draw(win, win->x, win->y + TITLE_H, win->w, win->h);
    }

    draw_workspace_indicator();
    draw_taskbar();
    draw_start_menu();
    draw_user_menu();
    draw_ctx_menu();
    frame_dirty = 1;      // a fresh frame is in the back buffer, awaiting fb_present()
}

// Public one-shot recomposite. Used by the foreground-exec wait loop (cmd_exec)
// to keep the desktop repainting — so a running TUI (e.g. top) is visible live —
// while the compositor thread is parked in that loop instead of its own event
// loop. Pure draw: touches no input state, safe to call re-entrantly from a
// command handler that the compositor's key dispatch invoked.
void compositor_redraw_now(void) {
    redraw_all();
    fb_present();     // this path runs outside the event loop, so publish here
    frame_dirty = 0;
}

static window_t* find_window(int id) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->id == id) return windows[i];
    return NULL;
}

// Open a Text Editor window, optionally pre-loaded with a file. Shared by the
// start-menu / desktop "Text Editor" action (path = NULL → blank document) and
// the file manager, which passes the path of a file the user opened. Returns the
// new window, or NULL if the window pool is full / allocation failed.
window_t* compositor_open_editor(const char* path) {
    window_t* ewin = window_create(150, 120, 600, 400, "Text Editor", editor_win_draw);
    if (!ewin) return NULL;
    ewin->reserved = editor_create_ctx();
    if (!ewin->reserved) { window_destroy(ewin->id); return NULL; }
    ewin->on_click = editor_win_click;
    ewin->on_key = editor_win_key;
    if (path && path[0]) editor_load_file((editor_win_t*)ewin->reserved, path);
    return ewin;
}

static void do_start_menu_action(int idx) {
    start_menu_open = 0;
    redraw_all();
    switch (idx) {
        case 0: // File Manager
            {
                window_t* fwin = window_create(100, 100, 550, 380, "File Manager", fileman_win_draw);
                if (fwin) {
                    fwin->reserved = fileman_create_ctx();
                    if (fwin->reserved) {
                        fwin->on_click = fileman_win_click;
                        fwin->on_key = fileman_win_key;
                        fwin->on_mousemove = fileman_win_mousemove;
                        fileman_refresh((fileman_win_t*)fwin->reserved);
                    }
                }
            }
            break;
        case 1: // Text Editor
            compositor_open_editor(NULL);
            break;
        case 2: // Image Viewer
            {
                window_t* iwin = window_create(200, 140, 540, 420, "Image Viewer", imageview_win_draw);
                if (iwin) {
                    iwin->reserved = imageview_create_ctx();
                    if (iwin->reserved) {
                        iwin->on_key = imageview_win_key;
                    }
                }
            }
            break;
        case 3: // Terminal
            {
                window_t* twin = window_create(80, 80, 640, 400, "Terminal", terminal_win_draw);
                if (twin) {
                    twin->reserved = terminal_create_ctx();
                    twin->on_key = terminal_win_key;
                }
            }
            break;
        case 4: // Settings
            {
                window_t* swin = window_create(160, 100, 500, 340, "Settings", settings_draw_fn);
                if (swin) {
                    int* tab = kmalloc(sizeof(int));
                    if (tab) { *tab = 0; swin->reserved = tab; }
                    swin->on_click = settings_win_click;
                }
            }
            break;
        case 5: // Task Manager
            {
                window_t* twin = window_create(100, 80, 480, 340, "Task Manager", taskman_win_draw);
                if (twin) {
                    twin->reserved = taskman_create_ctx();
                    if (twin->reserved) {
                        twin->on_key = taskman_win_key;
                        twin->on_click = taskman_win_click;
                    }
                }
            }
            break;
        case 6: // Desktop Demo
            window_create(100, 100, 300, 200, "Desktop Demo", NULL);
            break;
        case 7: // Paint
            {
                window_t* pwin = window_create(80, 60, 580, 480, "Paint", paint_win_draw);
                if (pwin) {
                    pwin->reserved = paint_create_ctx();
                    if (pwin->reserved) {
                        pwin->on_click = paint_win_click;
                        pwin->on_key = paint_win_key;
                        pwin->on_pressed = paint_win_pressed;
                    }
                }
            }
            break;
        case 8: // Sound Test
            {
                window_t* sndwin = window_create(200, 150, 220, 340, "Sound Test", soundtest_win_draw);
                if (sndwin) {
                    sndwin->reserved = soundtest_create_ctx();
                    if (sndwin->reserved) {
                        sndwin->on_click = soundtest_win_click;
                    }
                }
            }
            break;
        case 9: // About
            // Centred on the 1024x768 design grid — (1024-300)/2, (768-200)/2 —
            // NOT on the live framebuffer. Deriving it from fb_get_width() would
            // have the scale applied twice and land the window left of centre.
            window_create(362, 284, 300, 200, "About NyxOS", NULL);
            break;
        case 10: // Shutdown
            quit = 1;
            break;
        case 11: // Calculator
            {
                window_t* cwin = window_create(300, 200, CALC_WIN_W, CALC_WIN_H,
                                               "Calculator", calc_win_draw);
                if (cwin) {
                    cwin->reserved = calc_create_ctx();
                    if (cwin->reserved) {
                        cwin->on_click = calc_win_click;
                        cwin->on_key = calc_win_key;
                    }
                }
            }
            break;
        case 12: // Minesweeper
            {
                window_t* mwin = window_create(320, 180, MS_WIN_W, MS_WIN_H,
                                               "Minesweeper", minesweeper_win_draw);
                if (mwin) {
                    mwin->reserved = minesweeper_create_ctx();
                    if (mwin->reserved) {
                        mwin->on_click = minesweeper_win_click;
                        mwin->on_key = minesweeper_win_key;
                    }
                }
            }
            break;
    }
    redraw_all();
}

void compositor_init(void) {
    // Free any windows from a previous session (so logout -> re-login starts with a
    // clean desktop instead of leaking the old user's windows + their contexts).
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i]) {
            if (windows[i]->reserved) kfree(windows[i]->reserved);
            kfree(windows[i]);
            windows[i] = NULL;
        }
    }
    window_count = 0; next_id = 100; focused_id = 0; drag_id = 0; resize_id = 0; quit = 0;
    current_workspace = 0; start_menu_open = 0; user_menu_open = 0; cursor_saved = 0;

    taskbar_bg = THEME_TASKBAR_BG;
    taskbar_fg = THEME_TASKBAR_FG;
    taskbar_hl = THEME_TASKBAR_HL;   // active Menu button + focused window = brand accent
    desktop_bg = fb_rgb(30,35,50);
    title_active = THEME_TITLE_ACTIVE;      // was an off-brand blue (60,130,200)
    title_inactive = THEME_TITLE_INACTIVE;
    init_desktop_icons();
}

// Every window_create() call site writes its geometry as literals — 640x400 for
// the Terminal, 580x480 for Paint. Those numbers were all authored while looking
// at 1024x768, so that is the grid they are expressed in. State it once here
// instead of leaving it as folklore in nineteen call sites.
#define DESIGN_W 1024
#define DESIGN_H  768

// Geometry scales are Q16.16. The extra precision is not fussiness — see
// scale_u below for what Q8.8 actually cost.
#define SCALE_ONE 65536u

// Scale from the design grid to the live framebuffer, aspect-preserving and
// SHRINK-ONLY. Shrinking is the half that matters: at 640x480 the Terminal's
// authored 640x400 is the entire screen width and most of its height, so v5.9.5's
// clamp — which could only cap it at the screen edge — turned "a window" into
// "the desktop". Scaled instead, it lands at 400x250 and 640x480 gets the same
// layout as 1024x768, just smaller.
//
// Growing is deliberately NOT done. On a 1280x1024 screen the right answer is
// more room for windows, not one bigger window, and blowing up fixed-pixel
// content (the font, the calculator's button grid) would only make it blurrier.
static uint32_t design_scale(void) {
    uint32_t sx = (fb_get_width()  * SCALE_ONE) / DESIGN_W;
    uint32_t sy = (fb_get_height() * SCALE_ONE) / DESIGN_H;
    uint32_t s  = sx < sy ? sx : sy;
    return s > SCALE_ONE ? SCALE_ONE : s;
}

// Apply a scale, rounding to nearest instead of truncating.
//
// Both halves of that matter, because these scales get applied AGAIN when the
// resolution changes back, so any systematic bias compounds every time the user
// toggles modes. Measured on the real thing at Q8.8: 1024 -> 640 -> 1024 brought
// the Terminal back one pixel narrower, every single round trip. The culprit was
// the scale itself — 1024/640 is 1.6, which Q8.8 can only hold as 409/256 =
// 1.5977, so each round trip quietly multiplied the window by 0.9985.
//
// Q16.16 holds it as 104857/65536 = 1.59999, and with round-to-nearest the size
// round-trips exactly: 640 -> 400 -> 640, measured identical over three trips.
//
// POSITION can still move one pixel, once. A window at x=300 maps to 187.5 on
// the smaller screen, and no amount of precision decides that tie for us — it
// lands on 188 and comes back as 301. That is lost information, not lost
// precision. What matters is that it then stays there: 301 -> 188 -> 301 is a
// fixed point, so the shift happens on the first trip and never again (verified
// over three consecutive 1024 -> 640 -> 1024 cycles).
static inline int scale_i(int v, uint32_t s) {
    return (int)(((int64_t)v * (int)s + (SCALE_ONE / 2)) >> 16);
}
static inline uint32_t scale_u(uint32_t v, uint32_t s) {
    return (uint32_t)(((uint64_t)v * s + (SCALE_ONE / 2)) >> 16);
}

// Clamp a window INTO the current framebuffer. Shared by window_create (born
// on-screen) and windows_reflow (stays on-screen across a mode change) so the
// two paths cannot drift apart.
//
// Order matters: cap the SIZE against the usable area first, then move the
// window back inside, then floor at the origin — repositioning before capping
// would just push an oversized window off the opposite edge.
static void window_clamp(window_t* win) {
    int fw = (int)fb_get_width();
    int fh = (int)fb_get_height();
    int usable_h = fh - TASKBAR_H - TITLE_H;
    if ((int)win->w > fw) win->w = (uint32_t)fw;
    if ((int)win->h > usable_h && usable_h > 0) win->h = (uint32_t)usable_h;
    if (win->x + (int)win->w > fw) win->x = fw - (int)win->w;
    if (win->y + (int)win->h + TITLE_H > fh - TASKBAR_H)
        win->y = fh - TASKBAR_H - (int)win->h - TITLE_H;
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;
}

// Re-flow every open window when the screen size changes underneath it.
//
// Both of the geometry guards so far only run at BIRTH — v5.9.5 clamps a window
// as it is created, and the scaling above sizes it as it is created. Nothing had
// ever revisited a window that was already open when the mode changed, and the
// most visible casualty was the Settings window itself: switching 1024x768 ->
// 640x480 from its own Display tab left the window being clicked hanging 20px
// off the right edge and 18px under the taskbar.
//
// Scale by the OLD->NEW screen ratio rather than re-deriving from the design
// grid, so a window the user has since dragged or resized keeps the arrangement
// they chose instead of snapping back to wherever it was authored.
// Defined further down beside window_snap; forward-declared because the re-flow
// below needs to re-derive snapped windows onto the new framebuffer.
static void apply_snap_geom(window_t* win);
static int  win_is_snapped(int state);

static void windows_reflow(uint32_t old_fw, uint32_t old_fh) {
    if (!old_fw || !old_fh) return;
    uint32_t sx = (fb_get_width()  * SCALE_ONE) / old_fw;
    uint32_t sy = (fb_get_height() * SCALE_ONE) / old_fh;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t* win = windows[i];
        if (!win) continue;

        // normal_* has to travel too. It is where un-maximising lands, so leaving
        // it on the old screen's coordinates would stage a window that looks fine
        // while maximised and jumps off-screen the moment it is restored.
        win->normal_x = scale_i(win->normal_x, sx);
        win->normal_y = scale_i(win->normal_y, sy);
        win->normal_w = scale_u(win->normal_w, sx);
        win->normal_h = scale_u(win->normal_h, sy);
        if (win->normal_w < MIN_WIN_W) win->normal_w = MIN_WIN_W;
        if (win->normal_h < MIN_WIN_H) win->normal_h = MIN_WIN_H;

        // Maximised and snapped windows are defined by the SCREEN, not by their
        // history: re-derive their rect from the new framebuffer instead of
        // scaling it, or rounding would leave a seam at the edge (maximised) or
        // down the middle (snapped). normal_* was already scaled above, so
        // restoring one of these later still lands where the user left it.
        // (Since v5.9.7 Alt+Up maximises and Alt+Left/Right snap from the
        // keyboard, so both of these branches are now headlessly exercised.)
        if (win->state == WSTATE_MAXIMIZED) {
            win->x = 0; win->y = 0;
            win->w = fb_get_width();
            win->h = fb_get_height() - TASKBAR_H - TITLE_H;
            continue;
        }
        if (win_is_snapped(win->state)) {
            apply_snap_geom(win);
            continue;
        }

        win->x = scale_i(win->x, sx);
        win->y = scale_i(win->y, sy);
        win->w = scale_u(win->w, sx);
        win->h = scale_u(win->h, sy);
        if (win->w < MIN_WIN_W) win->w = MIN_WIN_W;
        if (win->h < MIN_WIN_H) win->h = MIN_WIN_H;
        window_clamp(win);
    }
}

// Switch the display mode and bring the whole desktop across with it.
//
// One function because there are now two ways in — the Settings Display tab and
// the `setres` shell command — and a mode switch that forgot half its follow-up
// work would be a bug you could only see at one resolution. It also gives the
// re-flow a seam a script can drive, instead of the change being reachable only
// by clicking a button inside the very window the re-flow has to rescue.
void display_set_mode(uint32_t w, uint32_t h) {
    uint32_t old_fw = fb_get_width(), old_fh = fb_get_height();
    if (w == old_fw && h == old_fh) return;

    printf("[DISPLAY] Switching to %ux%u...\n", w, h);
    vbe_set_mode(w, h, 32);
    fb_init(w, h, 32, (void*)0xE0000000);

    // Re-flow the desktop icons for the new width. Without this the layout kept
    // the OLD screen's coordinates, so shrinking the resolution stranded icons
    // off the right edge with no way to reach them. A full re-flow (rather than
    // clamping each icon back inside) is what desktops conventionally do on a
    // mode change, and it cannot leave two icons stacked on one another the way
    // clamping to the edge would.
    init_desktop_icons();
    windows_reflow(old_fw, old_fh);

    printf("[DISPLAY] Now %ux%u, %d window(s) re-flowed\n",
           fb_get_width(), fb_get_height(), window_count);
}

window_t* window_create(int x, int y, uint32_t w, uint32_t h, const char* title, window_draw_fn draw) {
    if (window_count >= MAX_WINDOWS) return NULL;
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (!windows[i]) { slot = i; break; }
    if (slot < 0) return NULL;

    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    // kmalloc does not zero. Every field this function does NOT assign was heap
    // garbage — including the on_pressed / on_key callback pointers, which the
    // compositor guards with `if (win->on_pressed)` before calling. A non-NULL
    // garbage value passes that guard and is then CALLED, so opening a window
    // could jump to an arbitrary address. Zero first, assign after.
    memset_asm(win, 0, sizeof(window_t));

    // Map the authored geometry onto this screen BEFORE the minimum-size floor
    // and the clamp. Order is the whole point: once everything has shrunk to fit,
    // the clamp usually has nothing left to do, so it goes back to being the
    // safety net it was meant to be instead of the thing that decides the layout.
    //
    // Position scales with size, or a window shrunk to 62% of its width would
    // still start at its full-size x and crowd the right edge.
    {
        uint32_t s = design_scale();
        if (s < SCALE_ONE) {
            w = scale_u(w, s); h = scale_u(h, s);
            x = scale_i(x, s); y = scale_i(y, s);
        }
    }
    win->w = w < MIN_WIN_W ? MIN_WIN_W : w;
    win->h = h < MIN_WIN_H ? MIN_WIN_H : h;
    win->x = x; win->y = y;

    // Backstop for whatever the scale did not save: a window born partly
    // off-screen is not just ugly, it can be unreachable, because the title bar
    // is the drag handle — if that is what went off the edge there is no way to
    // pull the window back. Clamping here rather than at each call site means no
    // future caller can reintroduce it either.
    window_clamp(win);
    // normal_* is what un-maximising restores to, so it must record the CLAMPED
    // geometry — copying the caller's raw request would send the window straight
    // back off-screen the first time it was maximised and restored.
    // (Read these from win->, not from the local x/y: window_clamp adjusts the
    // window, so the locals still hold the caller's unclamped request.)
    win->normal_x = win->x; win->normal_y = win->y;
    win->normal_w = win->w; win->normal_h = win->h;
    win->z_order = window_count;
    win->visible = 1; win->state = WSTATE_NORMAL;
    win->dragging = 0; win->resizing = 0;
    win->focused = 0; win->id = next_id++;
    win->workspace = current_workspace;
    win->has_close = 1; win->has_min = 1; win->has_max = 1;
    win->draw = draw;
    win->on_key = NULL;
    win->on_click = NULL;
    win->on_mousemove = NULL;
    win->reserved = NULL;
    int sl = strlen(title);
    if (sl >= MAX_TITLE) sl = MAX_TITLE - 1;
    memcpy(win->title, title, sl);
    win->title[sl] = '\0';
    windows[slot] = win;
    window_count++;
    window_focus(win->id);
    return win;
}

static void focus_next_window(void) {
    // Find the highest-z-order visible, non-minimized window
    window_t* best = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i] || !windows[i]->visible) continue;
        if (windows[i]->state == WSTATE_MINIMIZED) continue;
        if (windows[i]->workspace != current_workspace) continue;
        if (!best || windows[i]->z_order > best->z_order)
            best = windows[i];
    }
    if (best) {
        window_focus(best->id);
    } else {
        focused_id = 0;
    }
}

// Alt+Tab: focus the next window in a stable, repeatable cycle.
//
// Deliberately NOT focus_next_window()'s highest-z-order pick. That is the right
// choice on destroy (fall back to whatever is on top), but as a cycler it would
// only ever toggle the top two windows, because focusing one bumps its z-order
// above the other. Walking the array in SLOT order instead — slots are stable
// for a window's whole life — makes repeated Alt+Tab visit every eligible window
// in a fixed rotation and come back round, which is what a cycler should do.
static void window_cycle_focus(void) {
    int start = -1;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->id == focused_id) { start = i; break; }
    // start == -1 (nothing focused) makes the first probe land on slot 0.
    for (int step = 1; step <= MAX_WINDOWS; step++) {
        int i = (start + step) % MAX_WINDOWS;
        window_t* w = windows[i];
        if (!w || !w->visible || w->state == WSTATE_MINIMIZED) continue;
        if (w->workspace != current_workspace) continue;
        window_focus(w->id);
        return;
    }
}

void window_destroy(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] == win) { windows[i] = NULL; break; }
    window_count--;
    if (win->reserved)
        kfree(win->reserved);
    kfree(win);
    if (focused_id == id)
        focus_next_window();
}

void window_focus(int id) {
    window_t* win = find_window(id);
    if (!win || !win->visible || win->state == WSTATE_MINIMIZED) return;
    win->focused = 1;
    win->z_order = window_count + 10;
    focused_id = id;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i] && windows[i]->id != id)
            windows[i]->focused = 0;
}

void window_move(int id, int x, int y) {
    window_t* win = find_window(id);
    if (!win) return;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    // Allow off-screen during resize (clamp is done in window_resize)
    if (!win->resizing) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + (int)win->w > (int)fw) x = (int)fw - (int)win->w;
        if (y + (int)win_total_h(win) > (int)fh - (int)TASKBAR_H)
            y = (int)fh - (int)TASKBAR_H - (int)win_total_h(win);
    }
    win->x = x; win->y = y;
}

void window_resize(int id, uint32_t w, uint32_t h) {
    window_t* win = find_window(id);
    if (!win) return;
    win->w = w < MIN_WIN_W ? MIN_WIN_W : w;
    win->h = h < MIN_WIN_H ? MIN_WIN_H : h;
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    if (win->x + win->w > fw) win->w = fw - win->x;
    if (win->y + win_total_h(win) > fh - TASKBAR_H) win->h = fh - TASKBAR_H - win->y - TITLE_H;
}

void window_minimize(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    win->state = WSTATE_MINIMIZED;
    win->visible = 1;
    win->focused = 0;
    if (focused_id == id) focused_id = 0;
}

// Snapshot the restore-to geometry — but ONLY from the normal state. If the
// window is already maximized or snapped, normal_* already holds the true
// pre-arrangement rect; overwriting it with the current full/half-screen one
// would make "restore" restore to the arrangement instead of the original.
static void save_normal_geom(window_t* win) {
    if (win->state == WSTATE_NORMAL) {
        win->normal_x = win->x; win->normal_y = win->y;
        win->normal_w = win->w; win->normal_h = win->h;
    }
}

// Fill a snapped window's rect for the CURRENT framebuffer. Shared by window_snap
// The snap states form a 2-axis grid: a horizontal side (left/right) and a
// vertical zone (full height, top half, or bottom half). These four accessors
// keep the axis logic in one place so the chord handler and the geometry code
// agree on what each state means.
static int win_is_snapped(int s) {
    return s == WSTATE_SNAP_LEFT || s == WSTATE_SNAP_RIGHT ||
           s == WSTATE_SNAP_TL   || s == WSTATE_SNAP_TR   ||
           s == WSTATE_SNAP_BL   || s == WSTATE_SNAP_BR;
}
static int win_snap_left(int s) {
    return s == WSTATE_SNAP_LEFT || s == WSTATE_SNAP_TL || s == WSTATE_SNAP_BL;
}
// Vertical zone: 0 = full height (a half), 1 = top quarter, 2 = bottom quarter.
static int win_snap_vzone(int s) {
    if (s == WSTATE_SNAP_TL || s == WSTATE_SNAP_TR) return 1;
    if (s == WSTATE_SNAP_BL || s == WSTATE_SNAP_BR) return 2;
    return 0;
}
static int win_snap_state(int left, int vzone) {
    if (vzone == 1) return left ? WSTATE_SNAP_TL : WSTATE_SNAP_TR;
    if (vzone == 2) return left ? WSTATE_SNAP_BL : WSTATE_SNAP_BR;
    return left ? WSTATE_SNAP_LEFT : WSTATE_SNAP_RIGHT;
}

// Fill a snapped window's rect for the CURRENT framebuffer from its state's two
// axes. Shared by window_snap (initial placement) and windows_reflow (re-derive
// on a mode change) so the two paths cannot drift — the same reason maximize is
// re-derived rather than scaled. The far edges take the odd pixel (right w =
// fw - fw/2, bottom h = rest) so the two tiles on each axis meet with no
// one-pixel gap or overlap.
static void apply_snap_geom(window_t* win) {
    uint32_t fw = fb_get_width();
    uint32_t half_w = fw / 2;
    uint32_t vtotal = fb_get_height() - TASKBAR_H;   // usable height incl. title bars
    uint32_t half_v = vtotal / 2;
    int vz = win_snap_vzone(win->state);

    if (win_snap_left(win->state)) { win->x = 0;            win->w = half_w; }
    else                           { win->x = (int)half_w;  win->w = fw - half_w; }

    if (vz == 1) {          // top quarter
        win->y = 0;               win->h = half_v - TITLE_H;
    } else if (vz == 2) {   // bottom quarter
        win->y = (int)half_v;     win->h = vtotal - half_v - TITLE_H;
    } else {                // full-height half
        win->y = 0;               win->h = vtotal - TITLE_H;
    }
}

void window_maximize(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    if (win->state == WSTATE_MAXIMIZED) { window_restore(id); return; }
    save_normal_geom(win);   // no-op if coming from a snap, preserving the original
    win->x = 0; win->y = 0;
    win->w = fb_get_width();
    win->h = fb_get_height() - TASKBAR_H - TITLE_H;
    win->state = WSTATE_MAXIMIZED;
}

// Alt+Left / Alt+Right: set the horizontal side of the snap.
//  - from normal/maximized: snap to the full-height half on that side;
//  - from a snap already on that side: toggle back to the pre-snap geometry
//    (this is what makes Alt+Left double as a spring-loaded un-snap);
//  - from a snap on the OTHER side: flip sides, keeping the vertical zone, so a
//    top-right quarter slides to a top-left quarter.
void window_snap(int id, int left) {
    window_t* win = find_window(id);
    if (!win) return;
    if (win_is_snapped(win->state)) {
        if (win_snap_left(win->state) == left) { window_restore(id); return; }
        win->state = win_snap_state(left, win_snap_vzone(win->state));
    } else {
        save_normal_geom(win);
        win->state = win_snap_state(left, 0);   // full-height half
    }
    apply_snap_geom(win);
}

// Alt+Up / Alt+Down when the window is already snapped: slide the vertical zone
// along a single ladder  max <- top <- half <- bottom <- normal. up=1 climbs
// toward maximize, up=0 descends toward the un-snapped window. The ends spill
// out of the snap grid: climbing past "top" maximizes, descending past "bottom"
// restores. Returns nothing; the caller only reaches here when win_is_snapped.
static void window_snap_vslide(int id, int up) {
    window_t* win = find_window(id);
    if (!win) return;
    int left = win_snap_left(win->state);
    int vz   = win_snap_vzone(win->state);   // 0 half, 1 top, 2 bottom
    if (up) {
        if (vz == 0)      { win->state = win_snap_state(left, 1); apply_snap_geom(win); } // half -> top
        else if (vz == 2) { win->state = win_snap_state(left, 0); apply_snap_geom(win); } // bottom -> half
        else                window_maximize(win->id);                                     // top -> maximize
    } else {
        if (vz == 1)      { win->state = win_snap_state(left, 0); apply_snap_geom(win); } // top -> half
        else if (vz == 0) { win->state = win_snap_state(left, 2); apply_snap_geom(win); } // half -> bottom
        else                window_restore(win->id);                                      // bottom -> un-snap
    }
}

void window_restore(int id) {
    window_t* win = find_window(id);
    if (!win) return;
    win->x = win->normal_x; win->y = win->normal_y;
    win->w = win->normal_w; win->h = win->normal_h;
    win->state = WSTATE_NORMAL;
}

void window_set_workspace(int id, int ws) {
    window_t* win = find_window(id);
    if (!win) return;
    win->workspace = ws;
}

int window_get_count(void) { return window_count; }

int window_get_ids(int* ids, int max) {
    int n = 0;
    for (int i = 0; i < MAX_WINDOWS && n < max; i++)
        if (windows[i]) ids[n++] = windows[i]->id;
    return n;
}

static void demo_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    font_draw_string(cx + 5, cy + 5, win->title, fb_rgb(200,200,200), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 25, "Drag title bar to move", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 45, "Resize from edges", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 65, "Minimize/Maximize/Close", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 85, "Alt+Tab switch  Alt+F4 close", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 105, "Alt+Up/Down maximize/min", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 5, cy + 125, "Alt+Arrows tile/snap window", fb_rgb(160,160,160), fb_rgb(35,35,40));
    char buf[32];
    snprintf(buf, sizeof(buf), "ID: %d", win->id);
    font_draw_string(cx + 5, cy + 145, buf, fb_rgb(255,255,0), fb_rgb(35,35,40));
    snprintf(buf, sizeof(buf), "WS: %d", win->workspace);
    font_draw_string(cx + 5, cy + 165, buf, fb_rgb(255,255,0), fb_rgb(35,35,40));
}

static void about_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)win; (void)cw; (void)ch;
    font_draw_string(cx + 10, cy + 10, "NyxOS Desktop", THEME_ACCENT, fb_rgb(35,35,40));
    font_draw_string(cx + 10, cy + 30, "Version 0.2.0", fb_rgb(200,200,200), fb_rgb(35,35,40));
    font_draw_string(cx + 10, cy + 60, "A lightweight desktop OS", fb_rgb(160,160,160), fb_rgb(35,35,40));
    font_draw_string(cx + 10, cy + 80, "NyxOS Nightfall", fb_rgb(160,160,160), fb_rgb(35,35,40));
}

enum { SETTINGS_TAB_INFO, SETTINGS_TAB_DISPLAY, SETTINGS_TAB_KEYBOARD };

static void settings_draw_fn(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    int* tab = win->reserved ? (int*)win->reserved : NULL;
    int cur_tab = tab ? *tab : 0;
    uint32_t char_h = FONT_HEIGHT;
    fb_fill_rect(cx, cy, cw, ch, fb_rgb(30,30,35));

    // Tab bar
    int tab_h = 28;
    const char* tabs[] = {"Info", "Display", "Keyboard"};
    for (int i = 0; i < 3; i++) {
        int tx = cx + 4 + i * 90;
        uint32_t tbg = (i == cur_tab) ? fb_rgb(50,60,80) : fb_rgb(45,45,50);
        fb_fill_rect(tx, cy + 2, 86, tab_h - 2, tbg);
        font_draw_string(tx + (86 - strlen(tabs[i]) * FONT_WIDTH) / 2,
                         cy + 2 + (tab_h - 2 - char_h) / 2, tabs[i], fb_rgb(220,220,220), tbg);
    }
    fb_fill_rect(cx, cy + tab_h, cw, 1, fb_rgb(70,70,80));

    int y = cy + tab_h + 12;
    char buf[128];

    if (cur_tab == SETTINGS_TAB_INFO) {
        font_draw_string(cx + 10, y, "System Information", fb_rgb(100,200,100), fb_rgb(30,30,35));
        y += 24;
        snprintf(buf, sizeof(buf), "Kernel: %s %s (%s)", KERNEL_NAME, KERNEL_VERSION, KERNEL_CODENAME);
        font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35)); y += 18;
        snprintf(buf, sizeof(buf), "Memory: %d MB total, %d MB free",
            memory_total / (1024*1024), (memory_total - memory_used) / (1024*1024));
        font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35)); y += 18;
        snprintf(buf, sizeof(buf), "Uptime: %d sec", tick_count / 1000);
        font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35)); y += 18;
        snprintf(buf, sizeof(buf), "Heap: %d KB", KERNEL_HEAP_SIZE / 1024);
        font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35)); y += 18;
        snprintf(buf, sizeof(buf), "Windows: %d / %d", window_count, MAX_WINDOWS);
        font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35)); y += 18;
        snprintf(buf, sizeof(buf), "Resolution: %u x %u", fb_get_width(), fb_get_height());
        font_draw_string(cx + 10, y, buf, fb_rgb(200,200,200), fb_rgb(30,30,35));
    } else if (cur_tab == SETTINGS_TAB_DISPLAY) {
        font_draw_string(cx + 10, y, "Display Settings", fb_rgb(100,200,100), fb_rgb(30,30,35));
        y += 24;
        font_draw_string(cx + 10, y, "Resolution:", fb_rgb(220,220,220), fb_rgb(30,30,35)); y += 20;

        const char* res_buttons[] = {"640x480", "800x600", "1024x768", "1280x720"};
        int btn_w = 110, btn_h = 26;
        for (int i = 0; i < 4; i++) {
            int bx = cx + 10 + i * (btn_w + 8);
            uint32_t bg = fb_rgb(60,70,80);
            fb_fill_rect(bx, y, btn_w, btn_h, bg);
            font_draw_string(bx + (btn_w - strlen(res_buttons[i]) * FONT_WIDTH) / 2,
                             y + (btn_h - char_h) / 2, res_buttons[i], fb_rgb(220,220,220), bg);
        }
        y += btn_h + 12;
        font_draw_string(cx + 10, y, "Current: 1024x768@32bpp", fb_rgb(160,160,160), fb_rgb(30,30,35));
    } else if (cur_tab == SETTINGS_TAB_KEYBOARD) {
        font_draw_string(cx + 10, y, "Keyboard Layout", fb_rgb(100,200,100), fb_rgb(30,30,35));
        y += 24;
        const char* layout_name = keyboard_layout == 0 ? "US (QWERTY)" : "Spanish (ES)";
        snprintf(buf, sizeof(buf), "Current: %s", layout_name);
        font_draw_string(cx + 10, y, buf, fb_rgb(220,220,220), fb_rgb(30,30,35)); y += 24;
        // US button
        uint32_t us_bg = (keyboard_layout == 0) ? fb_rgb(50,90,50) : fb_rgb(60,70,80);
        fb_fill_rect(cx + 10, y, 120, 26, us_bg);
        font_draw_string(cx + 10 + (120 - 2 * FONT_WIDTH) / 2, y + (26 - char_h) / 2, "US", fb_rgb(220,220,220), us_bg);
        // ES button
        uint32_t es_bg = (keyboard_layout == 1) ? fb_rgb(50,90,50) : fb_rgb(60,70,80);
        fb_fill_rect(cx + 140, y, 120, 26, es_bg);
        font_draw_string(cx + 140 + (120 - 2 * FONT_WIDTH) / 2, y + (26 - char_h) / 2, "ES", fb_rgb(220,220,220), es_bg);
    }
}

static void settings_win_click(window_t* win, int mx, int my, int btn) {
    (void)btn;
    int* tab = win->reserved ? (int*)win->reserved : NULL;
    if (!tab) return;
    int cx = win->x, cy = win->y + TITLE_H;

    // Tab click
    if (my >= cy + 2 && my < cy + 28) {
        int rel = mx - cx;
        for (int i = 0; i < 3; i++) {
            if (rel >= 4 + i * 90 && rel < 4 + i * 90 + 86) {
                *tab = i;
                return;
            }
        }
    }

    if (*tab == SETTINGS_TAB_DISPLAY) {
        // Resolution buttons
        int y = cy + 48;
        int btn_w = 110, btn_h = 26;
        struct { uint32_t w, h; } res_modes[] = {{640,480},{800,600},{1024,768},{1280,720}};
        for (int i = 0; i < 4; i++) {
            int bx = cx + 10 + i * (btn_w + 8);
            if (mx >= bx && mx < bx + btn_w && my >= y && my < y + btn_h) {
                // Set resolution
                // Note this runs while Settings is the window handling the click,
                // so the window doing the switching is itself re-flowed.
                display_set_mode(res_modes[i].w, res_modes[i].h);
                return;
            }
        }
    } else if (*tab == SETTINGS_TAB_KEYBOARD) {
        int y = cy + 48;
        // US button
        if (mx >= cx + 10 && mx < cx + 130 && my >= y && my < y + 26) {
            if (keyboard_layout != 0) {
                set_keyboard_layout(0);
                printf("[SETTINGS] Keyboard layout: US\n");
            }
            return;
        }
        // ES button
        if (mx >= cx + 140 && mx < cx + 260 && my >= y && my < y + 26) {
            if (keyboard_layout != 1) {
                set_keyboard_layout(1);
                printf("[SETTINGS] Keyboard layout: ES\n");
            }
            return;
        }
    }
}

#define NUM_DESKTOP_ICONS 9
#define ICON_SIZE 64
#define ICON_PAD 12
static const char* desktop_icon_names[] = {
    "Files", "Terminal", "Editor", "Viewer", "Settings", "Paint", "Sounds", "Calc", "Mines"
};
static int desktop_icon_actions[] = {0, 3, 1, 2, 4, 7, 8, 11, 12};
static int desktop_icon_x[NUM_DESKTOP_ICONS];
static int desktop_icon_y[NUM_DESKTOP_ICONS];

// Lay the icons out as a GRID that wraps on the screen width, instead of the one
// fixed row this used to be. That row was `20 + i * 76` with no bound: the ninth
// icon ("Mines") spans x=628..692, so at the 640x480 mode Settings offers it was
// drawn past the right edge — invisible AND unclickable, since the hit test reads
// the same coordinates. Nothing shrank the row and nothing re-ran on a mode
// change, so there was no way to get it back.
//
// At 1024x768 this computes 13 columns, so all nine still sit in one row and the
// default desktop looks exactly as before; only the narrow modes actually wrap.
#define ICON_CELL_W (ICON_SIZE + ICON_PAD)
#define ICON_CELL_H (ICON_SIZE + 16 + ICON_PAD)   /* icon + label strip + pad */
#define ICON_MARGIN 20

static void init_desktop_icons(void) {
    int fw = (int)fb_get_width();
    int cols = (fw - ICON_MARGIN) / ICON_CELL_W;
    if (cols < 1) cols = 1;                       // absurdly narrow: single column
    for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
        desktop_icon_x[i] = ICON_MARGIN + (i % cols) * ICON_CELL_W;
        desktop_icon_y[i] = ICON_MARGIN + (i / cols) * ICON_CELL_H;
    }
}

static int desktop_icon_hit(int mx, int my) {
    uint32_t fh = fb_get_height();
    if (my >= (int)(fh - 36)) return -1;  // taskbar area
    for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
        if (mx >= desktop_icon_x[i] && mx < desktop_icon_x[i] + ICON_SIZE &&
            my >= desktop_icon_y[i] && my < desktop_icon_y[i] + ICON_SIZE + 16)
            return i;
    }
    return -1;
}

// Drag-reorder desktop icons
static int drag_icon_idx = -1;
static int drag_icon_ofs_x = 0, drag_icon_ofs_y = 0;

static void draw_icon_at(int i) {
    int x = desktop_icon_x[i];
    int y = desktop_icon_y[i];
    int hl = (i == drag_icon_idx);
    fb_fill_rect(x, y, ICON_SIZE, ICON_SIZE, hl ? fb_rgb(65,70,95) : fb_rgb(45,50,65));
    fb_fill_rect(x+1, y+1, ICON_SIZE-2, ICON_SIZE-2, hl ? fb_rgb(75,80,105) : fb_rgb(55,60,75));
    uint32_t icon_color[] = {
        fb_rgb(70,160,230), fb_rgb(0,220,0), fb_rgb(230,60,60),
        fb_rgb(220,220,50), fb_rgb(100,220,100), fb_rgb(220,100,220)
    };
    // Icon background (rounded square)
    fb_fill_rect(x+8, y+6, ICON_SIZE-16, ICON_SIZE-16, icon_color[i % 6]);
    // Subtle inner highlight
    uint32_t hi = fb_rgb(255,255,255);
    fb_fill_rect(x+12, y+10, ICON_SIZE-24, 2, hi);
    // Content lines inside icon
    fb_fill_rect(x+16, y+18, ICON_SIZE-32, 3, fb_rgb(255,255,255));
    fb_fill_rect(x+16, y+26, ICON_SIZE-32, 3, fb_rgb(255,255,255));
    fb_fill_rect(x+16, y+34, ICON_SIZE-32, 3, fb_rgb(255,255,255));
    // Label background for readability
    int tw = strlen(desktop_icon_names[i]) * 8;
    int tx = x + (ICON_SIZE - tw) / 2;
    if (tx < 0) tx = 0;
    uint32_t lw = tw + 8;
    if (lw > ICON_SIZE + 8) lw = ICON_SIZE + 8;
    fb_fill_rect(tx - 2, y + ICON_SIZE + 1, lw, FONT_HEIGHT + 2, fb_rgb(20,25,35));
    font_draw_string(tx, y + ICON_SIZE + 3, desktop_icon_names[i], fb_rgb(230,230,230), fb_rgb(20,25,35));
}

static void draw_desktop_icons(void) {
    for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
        if (i == drag_icon_idx) continue; // drawn last on top
        draw_icon_at(i);
    }
    if (drag_icon_idx >= 0) draw_icon_at(drag_icon_idx);
}

static void draw_welcome_windows(void) {
    // 360 tall so all the keyboard-shortcut hint lines (the last is WS: at cy+165,
    // ~181 px) still clear the bottom edge at 640x480, where design-grid scaling
    // shrinks this to 0.625 (=> 203 px of client height, comfortably above 181).
    window_create(80, 40, 400, 360, "Welcome to NyxOS", demo_draw_fn);
    window_create(200, 120, 450, 200, "About NyxOS", about_draw_fn);
    {
        window_t* twin = window_create(300, 200, 640, 400, "Terminal", terminal_win_draw);
        if (twin) {
            twin->reserved = terminal_create_ctx();
            twin->on_key = terminal_win_key;
        }
    }
}

void compositor_run(void) {
    compositor_active = 1;
    // Double buffering: compose each frame off-screen and blit it in one shot
    // (fb_present) so the screen never shows the half-drawn background between
    // elements — this is what removes the flicker on click / drag / repaint.
    fb_enable_backbuffer();
    uint32_t fw = fb_get_width(), fh = fb_get_height();
    quit = 0;
    mouse_x = fw / 2; mouse_y = fh / 2;
    mouse_set_pos(mouse_x, mouse_y);
    uint8_t prev_btns = 0;

    draw_welcome_windows();
    redraw_all();
    save_cursor_bg(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    fb_present();
    frame_dirty = 0;
    int redraw = 0;
    uint32_t clock_tick = 0;
    extern volatile int kbd_head, kbd_tail;
    while (!quit) {
        // Idle-yield: when nothing is happening — no key queued, no mouse button
        // held, the pointer hasn't moved, and no drag/resize/menu is active — sleep
        // briefly instead of busy-polling, so background jobs (or the CPU itself)
        // get the time. Any activity skips the sleep, so interactivity is unchanged;
        // the wakeup is within ~5ms (imperceptible). sleep() blocks via the scheduler
        // when it's running, or idles (hlt) to the tick otherwise.
        if (kbd_head == kbd_tail && mouse_get_buttons() == 0 &&
            mouse_get_x() == mouse_x && mouse_get_y() == mouse_y &&
            mouse_get_z() == mouse_z &&
            !drag_id && !resize_id && drag_icon_idx < 0 &&
            !ctx_menu_open && !start_menu_open) {
            sleep(5);
        }

        kernel_poll_net();
        run_background_tasks();

        int k = getkey_poll();

        int mx = mouse_get_x();
        int my = mouse_get_y();
        uint8_t btns = mouse_get_buttons();

        restore_cursor_bg(mouse_x, mouse_y);

        if (mx < 0) mx = 0;
        if (mx >= (int)fw) mx = fw - 1;
        if (my < 0) my = 0;
        if (my >= (int)fh) my = fh - 1;

        // Dispatch mouse-move to focused window
        if (mx != mouse_x || my != mouse_y) {
            window_t* fwin = NULL;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i] && windows[i]->id == focused_id) {
                    fwin = windows[i];
                    break;
                }
            }
            if (fwin && fwin->on_mousemove)
                fwin->on_mousemove(fwin, mx, my, btns);
        }

        // Dispatch mouse-wheel notches to the focused window as synthetic scroll keys
        // (the terminal turns them into scrollback movement). One key per notch.
        int wz = mouse_get_z();
        if (wz != mouse_z) {
            int notches = wz - mouse_z;
            int steps = notches < 0 ? -notches : notches;
            if (steps > 8) steps = 8;                    // clamp a fast flick
            window_t* fwin = find_window(focused_id);
            if (fwin && fwin->on_key) {
                int keycode = (notches > 0) ? KEY_WHEEL_UP : KEY_WHEEL_DOWN;
                for (int s = 0; s < steps; s++) fwin->on_key(fwin, keycode);
                redraw = 1;
            }
            mouse_z = wz;
        }

        if (drag_id && !(btns & 1)) {
            drag_id = 0; redraw = 1;
        }

        if (resize_id && !(btns & 1)) {
            resize_id = 0; redraw = 1;
        }

        if (drag_icon_idx >= 0 && !(btns & 1)) {
            int cx = desktop_icon_x[drag_icon_idx] + ICON_SIZE / 2;
            int cy = desktop_icon_y[drag_icon_idx] + ICON_SIZE / 2;
            int nearest = 0, near_dist = 999999;
            for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
                int sx = 20 + i * (ICON_SIZE + ICON_PAD) + ICON_SIZE / 2;
                int dx = cx - sx, dy = cy - (20 + ICON_SIZE / 2);
                int d = dx * dx + dy * dy;
                if (d < near_dist) { near_dist = d; nearest = i; }
            }
            int old_idx = drag_icon_idx;
            int moved = (nearest != old_idx);
            if (moved) {
                const char* name = desktop_icon_names[old_idx];
                int action = desktop_icon_actions[old_idx];
                if (nearest < old_idx) {
                    for (int i = old_idx; i > nearest; i--) {
                        desktop_icon_names[i] = desktop_icon_names[i-1];
                        desktop_icon_actions[i] = desktop_icon_actions[i-1];
                    }
                } else {
                    for (int i = old_idx; i < nearest; i++) {
                        desktop_icon_names[i] = desktop_icon_names[i+1];
                        desktop_icon_actions[i] = desktop_icon_actions[i+1];
                    }
                }
                desktop_icon_names[nearest] = name;
                desktop_icon_actions[nearest] = action;
            }
            for (int i = 0; i < NUM_DESKTOP_ICONS; i++) {
                desktop_icon_x[i] = 20 + i * (ICON_SIZE + ICON_PAD);
                desktop_icon_y[i] = 20;
            }
            if (!moved) do_start_menu_action(desktop_icon_actions[old_idx]);
            drag_icon_idx = -1;
            redraw = 1;
            goto done_click;
        }

        // Right-click: desktop context menu
        if ((btns & 2) && !(btns & 1)) {
            // Check if we hit desktop (not on a window, not on taskbar)
            int on_desktop = 1;
            uint32_t fh = fb_get_height();
            if (my >= (int)(fh - TASKBAR_H)) on_desktop = 0; // taskbar
            window_t* sorted_win[MAX_WINDOWS];
            int nw = 0;
            for (int i = 0; i < MAX_WINDOWS; i++)
                if (windows[i] && windows[i]->visible && windows[i]->state != WSTATE_MINIMIZED
                    && windows[i]->workspace == current_workspace)
                    sorted_win[nw++] = windows[i];
            // Order topmost-first (descending z-order), like the left-click path, so
            // an overlapped right-click is routed to the window on top instead of
            // whichever one happens to sit earliest in the array. Without this a
            // right-click (e.g. flagging in Minesweeper, or the File Manager context
            // menu) leaks to a lower window when the two overlap.
            for (int i = 0; i < nw; i++)
                for (int j = i + 1; j < nw; j++)
                    if (sorted_win[i]->z_order < sorted_win[j]->z_order) {
                        window_t* t = sorted_win[i]; sorted_win[i] = sorted_win[j]; sorted_win[j] = t;
                    }
            for (int i = 0; i < nw; i++)
                if (window_hit(sorted_win[i], mx, my)) { on_desktop = 0; break; }
            if (on_desktop) {
                if (ctx_menu_hit(mx, my)) {
                    int idx;
                    if (ctx_menu_item_hit(mx, my, &idx)) {
                        do_ctx_menu_action(idx);
                        redraw = 1;
                    }
                } else {
                    ctx_menu_open = 1;
                    ctx_menu_x = mx; ctx_menu_y = my;
                    redraw = 1;
                }
            } else {
                // Right-click on a window → route to window's on_click
                ctx_menu_open = 0;
                for (int i = 0; i < nw; i++)
                    if (window_hit(sorted_win[i], mx, my)) {
                        if (sorted_win[i]->on_click)
                            sorted_win[i]->on_click(sorted_win[i], mx, my, 2);
                        break;
                    }
            }
            goto done_click;
        }

        if (btns & 1) {
            if (drag_icon_idx >= 0) {
                int nx = mx - drag_icon_ofs_x;
                int ny = my - drag_icon_ofs_y;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                desktop_icon_x[drag_icon_idx] = nx;
                desktop_icon_y[drag_icon_idx] = ny;
                redraw = 1;
                goto done_click;
            } else if (drag_id) {
                window_t* win = find_window(drag_id);
                if (win) {
                    window_move(drag_id, mx - win->drag_off_x, my - win->drag_off_y);
                    redraw = 1;
                }
            } else if (resize_id) {
                window_t* win = find_window(resize_id);
                if (win) {
                    int dx = mx - win->resize_start_x;
                    int dy = my - win->resize_start_y;
                    int nx = win->x, ny = win->y;
                    uint32_t nw = win->resize_start_w, nh = win->resize_start_h;
                    switch (win->resize_dir) {
                        case RESIZE_RIGHT:
                        case RESIZE_RIGHT_TOP:
                        case RESIZE_CORNER:
                            nw = win->resize_start_w + dx; break;
                        case RESIZE_LEFT:
                        case RESIZE_LEFT_TOP:
                        case RESIZE_LEFT_BOTTOM:
                            nw = (int)win->resize_start_w - dx;
                            nx = win->resize_start_x + dx;
                            break;
                    }
                    switch (win->resize_dir) {
                        case RESIZE_BOTTOM:
                        case RESIZE_CORNER:
                        case RESIZE_LEFT_BOTTOM:
                            nh = win->resize_start_h + dy; break;
                        case RESIZE_TOP:
                        case RESIZE_LEFT_TOP:
                        case RESIZE_RIGHT_TOP:
                            nh = (int)win->resize_start_h - dy;
                            ny = win->resize_start_y + dy;
                            break;
                    }
                    if ((int)nw < MIN_WIN_W) { nw = MIN_WIN_W; nx = win->x; }
                    if ((int)nh < MIN_WIN_H) { nh = MIN_WIN_H; ny = win->y; }
                    win->x = nx; win->y = ny;
                    window_resize(resize_id, nw, nh);
                    redraw = 1;
                }
            } else {
                window_t* hit = NULL;
                window_t* sorted[MAX_WINDOWS];
                int n = 0;
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (windows[i] && windows[i]->visible && windows[i]->state != WSTATE_MINIMIZED
                        && windows[i]->workspace == current_workspace)
                        sorted[n++] = windows[i];
                for (int i = 0; i < n; i++)
                    for (int j = i + 1; j < n; j++)
                        if (sorted[i]->z_order < sorted[j]->z_order) {
                            window_t* t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t;
                        }

                // Close context menu on any left-click
                if (ctx_menu_open) {
                    int idx;
                    if (ctx_menu_item_hit(mx, my, &idx)) {
                        do_ctx_menu_action(idx);
                        ctx_menu_open = 0;
                        redraw = 1;
                    } else {
                        ctx_menu_open = 0;
                    }
                }

                /* This whole left-button block runs under `if (btns & 1)`, which
                 * is LEVEL-triggered: it re-runs on every compositor iteration
                 * for as long as the button is down. That is right for dragging
                 * and resizing, which must keep acting while held. It is wrong
                 * for every menu transition below, and the two combined made the
                 * Start menu impossible to open with a normal click:
                 *
                 *   iteration 1  press on "Menu"  -> start_hit toggles it OPEN
                 *   iteration 2  button still down, pointer still on the taskbar
                 *                at y=750, which is OUTSIDE the menu rect
                 *                (y 332..732) -> the click-outside branch runs
                 *                and closes it again.
                 *
                 * So the menu opened and shut inside a single press, and whether
                 * anything survived came down to how long the button was held.
                 * Opening, closing and selecting are all EDGE events; only the
                 * drag paths are level events. Found by driving the real mouse —
                 * the headless keyboard-only suite could never have seen it. */
                int pressed = !(prev_btns & 1);

                if (user_menu_open) {
                    int idx;
                    if (user_menu_item_hit(mx, my, &idx)) {
                        if (pressed) {
                            do_user_menu_action(idx);
                            user_menu_open = 0;
                            redraw = 1;
                        }
                    } else if (pressed && !user_menu_area_hit(mx, my)) {
                        user_menu_open = 0;
                        redraw = 1;
                    }
                    goto done_click;
                }

                if (badge_hit(mx, my)) {
                    if (pressed) {
                        user_menu_open = !user_menu_open;
                        start_menu_open = 0;
                        redraw = 1;
                    }
                    goto done_click;
                }

                if (start_menu_open) {
                    int idx;
                    if (start_menu_item_hit(mx, my, &idx)) {
                        if (pressed) {
                            do_start_menu_action(idx);
                            redraw = 1;
                        }
                    } else if (pressed && !start_menu_hit(mx, my)) {
                        start_menu_open = 0;
                        redraw = 1;
                    }
                    goto done_click;
                }

                if (start_hit(mx, my)) {
                    if (pressed) {
                        start_menu_open = !start_menu_open;
                        redraw = 1;
                    }
                    goto done_click;
                }

                int tb_win_id;
                if (taskbar_win_hit(mx, my, &tb_win_id)) {
                    window_t* w = find_window(tb_win_id);
                    if (w) {
                        if (w->state == WSTATE_MINIMIZED) {
                            w->state = WSTATE_NORMAL;
                            w->workspace = current_workspace;
                            window_focus(tb_win_id);
                        } else if (w->focused) {
                            window_minimize(tb_win_id);
                        } else {
                            window_focus(tb_win_id);
                        }
                        redraw = 1;
                    }
                    goto done_click;
                }

                {
                    int icon_idx = desktop_icon_hit(mx, my);
                    if (icon_idx >= 0) {
                        drag_icon_idx = icon_idx;
                        drag_icon_ofs_x = mx - desktop_icon_x[icon_idx];
                        drag_icon_ofs_y = my - desktop_icon_y[icon_idx];
                        redraw = 1;
                        goto done_click;
                    }
                }

                for (int i = 0; i < n; i++) {
                    if (window_hit(sorted[i], mx, my)) {
                        hit = sorted[i]; break;
                    }
                }

                if (hit) {
                    window_focus(hit->id);
                    redraw = 1;

                    if (close_hit(hit, mx, my)) {
                        window_destroy(hit->id);
                    } else if (max_hit(hit, mx, my)) {
                        if (hit->state == WSTATE_MAXIMIZED) window_restore(hit->id);
                        else window_maximize(hit->id);
                    } else if (min_hit(hit, mx, my)) {
                        window_minimize(hit->id);
                    } else if (titlebar_hit(hit, mx, my)) {
                        hit->dragging = 1;
                        hit->drag_off_x = mx - hit->x;
                        hit->drag_off_y = my - hit->y;
                        drag_id = hit->id;
                    } else {
                        int rdir;
                        if (resize_hit(hit, mx, my, &rdir)) {
                            hit->resizing = 1;
                            hit->resize_dir = rdir;
                            hit->resize_start_x = mx;
                            hit->resize_start_y = my;
                            hit->resize_start_w = hit->w;
                            hit->resize_start_h = hit->h;
                            resize_id = hit->id;
                        } else if (hit->on_click && prev_btns != btns) {
                            hit->on_click(hit, mx, my, 1);
                            redraw = 1;
                        }
                        if (hit->on_pressed) {
                            hit->on_pressed(hit, mx, my, 1);
                            redraw = 1;
                        }
                    }
                }
            }
        } else {
            // Ctrl+1..4: workspace switching
            if (is_ctrl_pressed() && k >= '1' && k <= '4') {
                current_workspace = k - '1';
                redraw = 1;
                goto done_click;
            }

            // Window-management chords. Checked BEFORE the key is routed to the
            // focused window, and each ends in `goto done_click`, so a maximised
            // Terminal never also receives the Tab/arrow as input. Alt+Tab works
            // even with nothing focused (it picks the first window); the rest
            // need a target, so they no-op when focused_id is 0.
            if (is_alt_pressed()) {
                if (k == '\t') {                       // Alt+Tab: cycle focus
                    window_cycle_focus();
                    redraw = 1;
                    goto done_click;
                }
                window_t* awin = find_window(focused_id);
                if (awin) {
                    if (k == KEY_F4) {                 // Alt+F4: close
                        window_destroy(awin->id);
                        redraw = 1;
                        goto done_click;
                    }
                    if (k == KEY_UP) {                 // Alt+Up: climb the ladder
                        // While snapped, Up refines the vertical zone (half -> top
                        // quarter -> maximise). Otherwise it just maximises.
                        if (win_is_snapped(awin->state))
                            window_snap_vslide(awin->id, 1);
                        else if (awin->state != WSTATE_MAXIMIZED)
                            window_maximize(awin->id);
                        redraw = 1;
                        goto done_click;
                    }
                    if (k == KEY_DOWN) {               // Alt+Down: descend the ladder
                        // Snapped: Down refines the vertical zone (top -> half ->
                        // bottom -> un-snap). Maximised: restore. Plain: minimise.
                        // (A focused window is never minimised, so the else really
                        // is a plain normal window.)
                        if (win_is_snapped(awin->state))
                            window_snap_vslide(awin->id, 0);
                        else if (awin->state == WSTATE_MAXIMIZED)
                            window_restore(awin->id);
                        else
                            window_minimize(awin->id);
                        redraw = 1;
                        goto done_click;
                    }
                    if (k == KEY_LEFT) {               // Alt+Left: snap/flip left (toggle off if already left)
                        window_snap(awin->id, 1);
                        redraw = 1;
                        goto done_click;
                    }
                    if (k == KEY_RIGHT) {              // Alt+Right: snap/flip right (toggle off if already right)
                        window_snap(awin->id, 0);
                        redraw = 1;
                        goto done_click;
                    }
                }
            }

            // Route all keys to focused window's key handler
            window_t* fwin = find_window(focused_id);
            if (fwin && fwin->on_key && k > 0) {
                fwin->on_key(fwin, k);
                redraw = 1;
            }
        }
        prev_btns = btns;

done_click:

        int moved = (mx != mouse_x || my != mouse_y);
        mouse_x = mx; mouse_y = my;
        mouse_btns = btns;

        // While any menu is open, a bare pointer move must recomposite so the
        // hovered-entry highlight tracks the cursor (a move alone only re-blits the
        // existing frame + cursor, which would leave the highlight frozen).
        if (moved && (start_menu_open || ctx_menu_open || user_menu_open)) redraw = 1;

        uint32_t now = get_ticks();
        if (now - clock_tick > 1000) {
            clock_tick = now;
            redraw = 1;
        }

        if (redraw) {
            redraw_all();
            redraw = 0;
        }

        save_cursor_bg(mouse_x, mouse_y);
        draw_cursor(mouse_x, mouse_y);
        // Publish the finished frame in one blit — only when something actually
        // changed (a recomposite happened, or the pointer moved) so an idle
        // desktop doesn't pointlessly copy the whole framebuffer every wakeup.
        if (frame_dirty || moved) { fb_present(); frame_dirty = 0; }
        for (int d = 0; d < 100000; d++) __asm__ volatile("pause");
    }

    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i]) { kfree(windows[i]); windows[i] = NULL; }
    window_count = 0;
    init_screen();
    clear_screen();
    compositor_active = 0;
}

int compositor_is_running(void) {
    return compositor_active;
}

void compositor_quit(void) {
    quit = 1;
}
