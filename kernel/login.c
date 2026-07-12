#include "kernel.h"
#include "login.h"
#include "auth.h"
#include "font.h"
#include "nyx_logo.h"

// The login background is a vertical gradient. Sampling its color at a given row
// lets the moon logo blend in seamlessly (font_draw_char always fills the cell
// background, so we hand it the gradient color under each glyph row).
static uint32_t login_grad(uint32_t y, uint32_t fh) {
    uint32_t t = y * 255 / fh;
    uint32_t r = 16 + t * 22 / 255, g = 12 + t * 12 / 255, b = 30 + t * 26 / 255;
    if (r > 38) r = 38;
    if (g > 24) g = 24;
    if (b > 62) b = 62;
    return fb_rgb(r, g, b);
}

extern volatile char kbd_buffer[256];
extern volatile int kbd_head;
extern volatile int kbd_tail;

static void draw_field(int x, int y, int w, int active, const char* text, int masked) {
    uint32_t bd = active ? fb_rgb(150,120,240) : fb_rgb(68,58,92);
    uint32_t bg = active ? fb_rgb(52,44,80) : fb_rgb(40,34,56);
    fb_fill_rect(x, y, w, 20, bg);
    fb_fill_rect(x, y, w, 1, bd);
    fb_fill_rect(x, y+19, w, 1, bd);
    fb_fill_rect(x, y, 1, 20, bd);
    fb_fill_rect(x+w-1, y, 1, 20, bd);
    if (text && text[0]) {
        if (masked) {
            char m[64];
            int len = 0;
            while (text[len]) len++;
            for (int i = 0; i < len && i < 63; i++) m[i] = '*';
            m[len] = '\0';
            font_draw_string(x+4, y+2, m, fb_rgb(200,200,220), bg);
        } else {
            font_draw_string(x+4, y+2, text, fb_rgb(200,200,220), bg);
        }
    }
}

int login_screen(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();

    serial_puts("[LOGIN] Drawing...\n");

    for (uint32_t y = 0; y < fh; y++)
        fb_fill_rect(0, y, fw, 1, login_grad(y, fh));

    int px = (fw - 360) / 2, py = fh / 2 - 100;

    // NyxOS crescent-moon logo (same art as `nyxfetch`) above the login box, in
    // brand purple, each glyph row drawn over the matching gradient color so the
    // logo blends into the background rather than sitting on a flat panel.
    const uint32_t moon = fb_rgb(168, 138, 245);        // NyxOS purple
    int moon_w = NYX_LOGO_COLS * FONT_WIDTH;
    int moon_x = (int)(fw - moon_w) / 2;
    int moon_top = py - NYX_LOGO_ROWS * FONT_HEIGHT - 14;
    if (moon_top < 4) moon_top = 4;
    for (int i = 0; i < NYX_LOGO_ROWS; i++) {
        int y = moon_top + i * FONT_HEIGHT;
        font_draw_string(moon_x, y, NYX_LOGO[i], moon, login_grad((uint32_t)y, fh));
    }
    fb_fill_rect(px, py, 360, 210, fb_rgb(30,24,46));
    fb_fill_rect(px-1, py-1, 362, 1, fb_rgb(72,60,96));
    fb_fill_rect(px-1, py+210, 362, 1, fb_rgb(50,42,66));
    fb_fill_rect(px-1, py, 1, 210, fb_rgb(72,60,96));
    fb_fill_rect(px+360, py, 1, 210, fb_rgb(50,42,66));

    font_draw_string((fw-12*8)/2, py+10, "NyxOS Login", fb_rgb(190,168,245), fb_rgb(30,24,46));
    fb_fill_rect(px+20, py+30, 320, 1, fb_rgb(55,60,75));
    font_draw_string(px+20, py+45, "Username:", fb_rgb(180,165,215), fb_rgb(30,24,46));
    font_draw_string(px+20, py+98, "Password:", fb_rgb(180,165,215), fb_rgb(30,24,46));

    int ux = px+20, uy = py+65, uw = 320;
    int pfx = px+20, pfy = py+118, pfw = 320;

    draw_field(ux, uy, uw, 1, NULL, 0);
    draw_field(pfx, pfy, pfw, 0, NULL, 1);

    char user[32], pass[64];
    int user_pos = 0, pass_pos = 0, field = 0;
    user[0] = pass[0] = '\0';

    serial_puts("[LOGIN] Ready.\n");

    while (1) {
        __asm__ volatile("cli");
        char c = 0;
        if (kbd_tail != kbd_head) {
            c = kbd_buffer[kbd_tail];
            kbd_tail = (kbd_tail + 1) % 256;
        }
        __asm__ volatile("sti");

        if (c) {
            if (c == '\n' || c == '\r') {
                if (field == 0) {
                    field = 1;
                    draw_field(ux, uy, uw, 0, user, 0);
                    draw_field(pfx, pfy, pfw, 1, pass, 1);
                } else {
                    goto submit;
                }
            } else if ((c == '\b' || c == 0x7F) && (field == 0 ? user_pos : pass_pos) > 0) {
                if (field == 0) user[--user_pos] = '\0';
                else pass[--pass_pos] = '\0';
                draw_field(ux, uy, uw, field == 0, user, 0);
                draw_field(pfx, pfy, pfw, field == 1, pass, 1);
            } else if (c >= 32 && c < 127) {
                if (field == 0 && user_pos < 31) {
                    user[user_pos++] = c; user[user_pos] = '\0';
                    draw_field(ux, uy, uw, 1, user, 0);
                } else if (field == 1 && pass_pos < 63) {
                    pass[pass_pos++] = c; pass[pass_pos] = '\0';
                    draw_field(pfx, pfy, pfw, 1, pass, 1);
                }
            }
        } else {
            for (volatile int i = 0; i < 10000; i++);
        }
    }

submit:
    serial_puts("[LOGIN] Verifying...\n");
    int ok = auth_verify(user, pass);
    serial_puts(ok ? "[LOGIN] OK.\n" : "[LOGIN] FAIL.\n");
    if (!ok) {
        font_draw_string((fw-18*8)/2, py+180, "Invalid credentials", fb_rgb(220,60,60), fb_rgb(30,24,46));
        for (volatile int i = 0; i < 30000000; i++);
        return 0;
    }
    return 1;
}
