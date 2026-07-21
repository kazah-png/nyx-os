#ifndef THEME_H
#define THEME_H

#include "kernel.h"   /* fb_rgb() */

/* ============================================================================
 * THE GUI palette — one place the desktop's colours live.
 *
 * Before this, every colour was an `fb_rgb(r,g,b)` literal scattered across
 * compositor.c and each *_win.c, so changing the accent meant hunting call
 * sites and there was no way to theme the desktop. Call sites now name a ROLE
 * (THEME_ACCENT, THEME_WINDOW_BG, …) and the value lives here.
 *
 * These are macros, not a const struct, because fb_rgb() is a runtime function
 * (kernel.h): each expands to a call where the drawing code already runs. That
 * also keeps this header a pure compile-time constant set — no initialisation
 * order to worry about — and leaves the door open for a future runtime theme
 * (Settings → accent) by swapping these for a table lookup without touching a
 * single call site.
 *
 * The accent is the brand PURPLE — the wallpaper's default "Morado"
 * {130,90,210}. Keep any new GUI colour as a role here, not a fresh literal.
 * ========================================================================== */

/* --- Brand accent -------------------------------------------------------- */
#define THEME_ACCENT        fb_rgb(130,  90, 210)  /* brand purple ("Morado") */
#define THEME_ACCENT_DIM    fb_rgb( 92,  64, 150)  /* pressed / darker accent  */
#define THEME_ON_ACCENT     fb_rgb(255, 255, 255)  /* text/icons on an accent fill */

/* --- Surfaces ------------------------------------------------------------ */
#define THEME_WINDOW_BG     fb_rgb( 45,  45,  50)  /* window / menu body       */
#define THEME_PANEL         fb_rgb( 30,  30,  35)  /* insets, list backgrounds */
#define THEME_BORDER        fb_rgb(100, 100, 100)  /* 1px window / menu border */
#define THEME_ROW_DIV       fb_rgb( 55,  55,  60)  /* row separator            */

/* --- Taskbar ------------------------------------------------------------- */
#define THEME_TASKBAR_BG    fb_rgb( 40,  45,  55)
#define THEME_TASKBAR_FG    fb_rgb(220, 220, 220)
#define THEME_TASKBAR_HL    THEME_ACCENT           /* active Menu / focused window */

/* --- Selection / text ---------------------------------------------------- */
#define THEME_SELECTION     THEME_ACCENT
#define THEME_TEXT          fb_rgb(230, 230, 240)
#define THEME_TEXT_DIM      fb_rgb(175, 175, 185)

#endif /* THEME_H */
