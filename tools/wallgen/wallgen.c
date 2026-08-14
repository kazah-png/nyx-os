// wallgen — render the NyxOS desktop wallpapers outside the OS.
//
// This is not a re-implementation. `extract.sh` copies the compositor's real
// background painter verbatim out of kernel/gui/core/compositor.c (and the style
// enum + colour palette out of the Wallpaper app) into bg_extract.inc, and this
// file compiles that code against a plain memory framebuffer instead of the video
// hardware. The PNGs it produces are therefore the exact pixels NyxOS paints on
// the desktop — same integer maths, same deterministic star seed, no floats.
//
//   ./extract.sh && cc -O2 -o wallgen wallgen.c && ./wallgen out/
//
// Output is binary PPM (P6); tools/wallgen/pack.py converts to PNG and zips.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- framebuffer
// The kernel-side surface draw_background() expects, backed by malloc.

static uint32_t *g_fb;
static uint32_t  g_w, g_h;
static uint32_t  g_base;    // packed 0x00RRGGBB, what wallpaper_base_color() returns
static int       g_style;   // WP_STYLE_*
static uint32_t  g_ticks;   // frozen clock for the animated styles

static uint32_t fb_get_width(void)  { return g_w; }
static uint32_t fb_get_height(void) { return g_h; }

static uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (x >= g_w || y >= g_h) return;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            g_fb[(y + j) * g_w + (x + i)] = color;
}

// Verbatim from kernel/drivers/video/fb.c — the kernel has no libm, and the circle
// spans must round identically or the moon would differ by a pixel.
static uint32_t fb_isqrt(uint32_t x) {
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

static uint32_t get_ticks(void)           { return g_ticks; }
static uint32_t wallpaper_base_color(void) { return g_base; }
static int      wallpaper_style(void)      { return g_style; }

// ------------------------------------------------- the real compositor source
// bg_fill_circle, wp_isin, bg_ripple_ring, bg_star_line, draw_background — plus
// the WP_STYLE_* enum and the palette[] table. Generated; do not edit by hand.
#include "bg_extract.inc"

// ---------------------------------------------------------------------- output

static int write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 0; }
    fprintf(f, "P6\n%u %u\n255\n", g_w, g_h);
    uint8_t *row = malloc((size_t)g_w * 3);
    if (!row) { fclose(f); return 0; }
    for (uint32_t y = 0; y < g_h; y++) {
        for (uint32_t x = 0; x < g_w; x++) {
            uint32_t p = g_fb[y * g_w + x];
            row[x * 3 + 0] = (uint8_t)(p >> 16);
            row[x * 3 + 1] = (uint8_t)(p >> 8);
            row[x * 3 + 2] = (uint8_t)p;
        }
        fwrite(row, 1, (size_t)g_w * 3, f);
    }
    free(row);
    fclose(f);
    return 1;
}

// The animated styles are pure functions of the clock, so a still frame just means
// picking a tick. These are hand-chosen moments where each effect reads best: the
// meteor mid-flight, the aurora curtains open, the ripples spread.
static uint32_t still_frame_tick(int style) {
    switch (style) {
        // The meteor only exists for the first 900 ms of each 3600 ms period, so the
        // tick has to land inside a streak: 4050 = period 1, halfway down the path.
        case WP_STYLE_SHOOTINGSTAR: return 4050;
        case WP_STYLE_AURORA:       return 5200;   // curtains at full drift
        case WP_STYLE_NEBULA:       return 9000;   // clouds off the seam
        case WP_STYLE_LUCES:        return 4200;   // orbs spread up the frame
        case WP_STYLE_ONDAS:        return 3400;   // three rings visible
        case WP_STYLE_LLUVIA:       return 7000;   // rain evenly distributed
        default:                    return 1200;
    }
}

// The OS names the styles and colours in Spanish ("Nightfall", "Cordillera",
// "Morado"). Lowercased they make the filenames, so the pack stays labelled with
// the same names the Wallpaper app shows.
static void slug(const char *name, char *out, size_t n) {
    size_t i = 0;
    for (; name[i] && i + 1 < n; i++) {
        char ch = name[i];
        out[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch;
    }
    out[i] = '\0';
}

// usage: wallgen OUTDIR [WIDTH HEIGHT [COLOUR_INDEX]]
//
// 1920x1080 is the resolution the desktop is designed for — the moon geometry and
// the star count are absolute pixel values, so only this size reproduces the real
// desktop exactly. Larger sizes still render natively (the gradient, aurora,
// nebula, rain and mountains are all framebuffer-relative); the moon and stars
// simply keep their absolute size, so the scene reads wider and airier.
int main(int argc, char **argv) {
    const char *outdir = (argc > 1) ? argv[1] : ".";
    g_w = (argc > 3) ? (uint32_t)strtoul(argv[2], NULL, 10) : 1920;
    g_h = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 10) : 1080;
    int only = (argc > 4) ? atoi(argv[4]) : -1;          // -1 = every base colour
    if (g_w < 64 || g_h < 64 || only >= WALLPAPER_COUNT) {
        fputs("usage: wallgen OUTDIR [WIDTH HEIGHT [COLOUR_INDEX]]\n", stderr);
        return 2;
    }

    g_fb = malloc((size_t)g_w * g_h * sizeof(uint32_t));
    if (!g_fb) { fputs("out of memory\n", stderr); return 1; }

    int written = 0;
    for (int c = 0; c < WALLPAPER_COUNT; c++) {
        if (only >= 0 && c != only) continue;
        for (int s = 0; s < WP_STYLE_COUNT; s++) {
            g_base  = fb_rgb(palette[c].r, palette[c].g, palette[c].b);
            g_style = s;
            g_ticks = still_frame_tick(s);
            memset(g_fb, 0, (size_t)g_w * g_h * sizeof(uint32_t));
            draw_background();

            char path[512], sty[64], col[64];
            slug(style_names[s], sty, sizeof sty);
            slug(palette[c].name, col, sizeof col);
            snprintf(path, sizeof path, "%s/nyxos-%s-%s.ppm", outdir, sty, col);
            if (!write_ppm(path)) return 1;
            written++;
        }
    }
    printf("wallgen: %d wallpapers at %ux%u -> %s\n", written, g_w, g_h, outdir);
    return 0;
}
