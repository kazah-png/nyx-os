#include "kernel.h"
#include "login.h"
#include "auth.h"
#include "font.h"
#include "nyx_logo.h"

#define LOGIN_MAX_ATTEMPTS 3   /* lock out (reboot) after this many failed logins */
#define LOGIN_MIN_PASS     4   /* mirrors auth.c AUTH_MIN_PASS for the sign-up UI  */

// The account that logged in (guest 'nyx' by default), read by the desktop
// taskbar to show "who am I" with a profile picture.
char g_login_user[32] = "nyx";
int  g_login_avatar = 0;

// The login background is a vertical gradient. Sampling its color at a given row
// lets the moon logo blend in seamlessly.
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

// ---- Profile pictures -----------------------------------------------------
// Four simple procedural "avatars": a coloured tile with a blocky face, each
// recognisably different. Drawn at any size (login picker ~46px, taskbar ~22px),
// so feature positions are fractions of `s`. Shared with the compositor taskbar.
void draw_avatar(int x, int y, int s, int id, int selected) {
    id &= 3;
    uint32_t col, eye = fb_rgb(28, 24, 40), white = fb_rgb(245, 245, 255);
    switch (id) {
        case 0:  col = fb_rgb(150, 120, 240); break;  // purple (nyx / moon)
        case 1:  col = fb_rgb(70, 150, 235);  break;  // blue   (smiley)
        case 2:  col = fb_rgb(80, 200, 130);  break;  // green  (cool)
        default: col = fb_rgb(240, 150, 60);  break;  // orange (cat)
    }
    fb_fill_rect(x - 3, y - 3, s + 6, s + 6, selected ? white : fb_rgb(70, 60, 90));  // ring
    if (id == 3) {                                   // cat: ears poke above the tile
        fb_fill_rect(x + s * 12 / 100, y - s * 12 / 100, s * 22 / 100, s * 20 / 100, col);
        fb_fill_rect(x + s * 66 / 100, y - s * 12 / 100, s * 22 / 100, s * 20 / 100, col);
    }
    fb_fill_rect(x, y, s, s, col);                   // tile

    int ew = s / 6; if (ew < 2) ew = 2;
    int lex = x + s * 27 / 100, rex = x + s * 58 / 100, ey = y + s * 33 / 100;
    int mw = s * 34 / 100, mx = x + (s - mw) / 2, my = y + s * 62 / 100;
    int mh = s / 12; if (mh < 2) mh = 2;

    switch (id) {
        case 0:  // moon: calm neutral face
            fb_fill_rect(lex, ey, ew, ew, eye);
            fb_fill_rect(rex, ey, ew, ew, eye);
            fb_fill_rect(mx, my, mw, mh, eye);
            break;
        case 1:  // smiley: eyes + a wide smile (bar with lowered ends)
            fb_fill_rect(lex, ey, ew, ew, eye);
            fb_fill_rect(rex, ey, ew, ew, eye);
            fb_fill_rect(mx, my, mw, ew, eye);
            fb_fill_rect(mx - ew / 2, my - ew / 2, ew, ew, eye);
            fb_fill_rect(mx + mw - ew / 2, my - ew / 2, ew, ew, eye);
            break;
        case 2:  // cool: a single visor bar over both eyes + small mouth
            fb_fill_rect(lex, ey, (rex + ew) - lex, ew, eye);
            fb_fill_rect(mx + mw / 4, my, mw / 2, mh, eye);
            break;
        default: // cat: eyes + a little nose
            fb_fill_rect(lex, ey, ew, ew, eye);
            fb_fill_rect(rex, ey, ew, ew, eye);
            fb_fill_rect(x + s / 2 - ew / 2, my, ew, ew, fb_rgb(235, 120, 150));
            break;
    }
}

const char* avatar_name(int id) {
    static const char* names[] = { "Moon", "Smiley", "Cool", "Cat" };
    return names[id & 3];
}

static void draw_field(int x, int y, int w, int active, const char* text, int masked) {
    uint32_t bd = active ? fb_rgb(150, 120, 240) : fb_rgb(68, 58, 92);
    uint32_t bg = active ? fb_rgb(52, 44, 80) : fb_rgb(40, 34, 56);
    fb_fill_rect(x, y, w, 20, bg);
    fb_fill_rect(x, y, w, 1, bd);
    fb_fill_rect(x, y + 19, w, 1, bd);
    fb_fill_rect(x, y, 1, 20, bd);
    fb_fill_rect(x + w - 1, y, 1, 20, bd);
    if (text && text[0]) {
        if (masked) {
            char m[64]; int len = 0;
            while (text[len]) len++;
            for (int i = 0; i < len && i < 63; i++) m[i] = '*';
            m[len < 63 ? len : 63] = '\0';
            font_draw_string(x + 4, y + 2, m, fb_rgb(200, 200, 220), bg);
        } else {
            font_draw_string(x + 4, y + 2, text, fb_rgb(200, 200, 220), bg);
        }
    }
}

#define BOX_W 380
#define BOX_H 300

// Repaint the whole login/create panel for the current state.
static void draw_panel(int px, int py, int mode, int field, int avatar,
                       const char* user, const char* pass, const char* msg) {
    uint32_t bg = fb_rgb(30, 24, 46);
    fb_fill_rect(px, py, BOX_W, BOX_H, bg);
    fb_fill_rect(px - 1, py - 1, BOX_W + 2, 1, fb_rgb(72, 60, 96));
    fb_fill_rect(px - 1, py + BOX_H, BOX_W + 2, 1, fb_rgb(50, 42, 66));
    fb_fill_rect(px - 1, py, 1, BOX_H, fb_rgb(72, 60, 96));
    fb_fill_rect(px + BOX_W, py, 1, BOX_H, fb_rgb(50, 42, 66));

    const char* title = mode ? "Create your account" : "NyxOS Login";
    font_draw_string(px + (BOX_W - (int)strlen(title) * 8) / 2, py + 12, title, fb_rgb(190, 168, 245), bg);
    fb_fill_rect(px + 20, py + 34, BOX_W - 40, 1, fb_rgb(55, 60, 75));

    font_draw_string(px + 20, py + 46, "Username:", fb_rgb(180, 165, 215), bg);
    draw_field(px + 20, py + 62, BOX_W - 40, field == 0, user, 0);
    font_draw_string(px + 20, py + 98, "Password:", fb_rgb(180, 165, 215), bg);
    draw_field(px + 20, py + 114, BOX_W - 40, field == 1, pass, 1);

    if (mode) {
        font_draw_string(px + 20, py + 150, "Choose a profile picture:", fb_rgb(180, 165, 215), bg);
        int s = 46, gap = 30, total = AVATAR_COUNT * s + (AVATAR_COUNT - 1) * gap;
        int ax = px + (BOX_W - total) / 2, ay = py + 172;
        for (int i = 0; i < AVATAR_COUNT; i++) {
            int cx = ax + i * (s + gap);
            draw_avatar(cx, ay, s, i, i == avatar);
            char lbl[8]; snprintf(lbl, sizeof(lbl), "%d.%s", i + 1, avatar_name(i));
            uint32_t lc = (i == avatar) ? fb_rgb(255, 255, 150) : fb_rgb(150, 150, 170);
            font_draw_string(cx + (s - (int)strlen(lbl) * 8) / 2, ay + s + 6, lbl, lc, bg);
        }
        if (field == 2)
            font_draw_string(px + 20, py + 250, "Press 1-4 to pick, Enter to create.", fb_rgb(150, 200, 150), bg);
    } else {
        // Centered and short enough to stay inside the box (a longer, left-aligned
        // line overflowed the right edge and left artifacts when toggling modes).
        const char* h = "New here? Press TAB to create an account.";
        font_draw_string(px + (BOX_W - (int)strlen(h) * 8) / 2, py + 160, h, fb_rgb(150, 150, 180), bg);
    }

    if (msg && msg[0])
        font_draw_string(px + (BOX_W - (int)strlen(msg) * 8) / 2, py + BOX_H - 40, msg, fb_rgb(235, 110, 110), bg);
    const char* hint = mode ? "TAB: back to login" : "TAB: create account    guest: nyx / nyx";
    font_draw_string(px + (BOX_W - (int)strlen(hint) * 8) / 2, py + BOX_H - 20, hint, fb_rgb(130, 120, 160), bg);
}

int login_screen(void) {
    uint32_t fw = fb_get_width(), fh = fb_get_height();

    serial_puts("[LOGIN] Drawing...\n");

    for (uint32_t y = 0; y < fh; y++)
        fb_fill_rect(0, y, fw, 1, login_grad(y, fh));

    int px = ((int)fw - BOX_W) / 2, py = (int)fh / 2 - 140;

    // NyxOS crescent-moon logo above the panel, in brand purple.
    const uint32_t moon = fb_rgb(168, 138, 245);
    int moon_w = NYX_LOGO_COLS * FONT_WIDTH;
    int moon_x = ((int)fw - moon_w) / 2;
    int moon_top = py - NYX_LOGO_ROWS * FONT_HEIGHT - 14;
    if (moon_top < 4) moon_top = 4;
    for (int i = 0; i < NYX_LOGO_ROWS; i++) {
        int y = moon_top + i * FONT_HEIGHT;
        font_draw_string(moon_x, y, NYX_LOGO[i], moon, login_grad((uint32_t)y, fh));
    }

    int mode = 0;         // 0 = login, 1 = create account
    int avatar = 0;       // selected profile picture (create mode)
    int attempts = 0;
    const char* msg = "";

    for (;;) {
        char user[32], pass[64];
        int user_pos = 0, pass_pos = 0, field = 0;
        user[0] = pass[0] = '\0';

        draw_panel(px, py, mode, field, avatar, user, pass, msg);
        serial_puts("[LOGIN] Ready.\n");

        int submitted = 0;
        while (!submitted) {
            __asm__ volatile("cli");
            char c = 0;
            if (kbd_tail != kbd_head) {
                c = kbd_buffer[kbd_tail];
                kbd_tail = (kbd_tail + 1) % 256;
            }
            __asm__ volatile("sti");

            if (!c) { for (volatile int i = 0; i < 10000; i++); continue; }

            if (c == '\t') {                          // toggle login / create
                mode ^= 1; field = 0; user_pos = pass_pos = 0;
                user[0] = pass[0] = '\0'; msg = "";
                draw_panel(px, py, mode, field, avatar, user, pass, msg);
                continue;
            }
            if (c == '\n' || c == '\r') {
                if (field == 0) field = 1;
                else if (field == 1) { if (mode == 0) submitted = 1; else field = 2; }
                else submitted = 1;                   // create: avatar field -> submit
                if (!submitted) draw_panel(px, py, mode, field, avatar, user, pass, msg);
                continue;
            }
            if (field == 2) {                         // avatar picker
                if (c >= '1' && c <= '4') { avatar = c - '1'; draw_panel(px, py, mode, field, avatar, user, pass, msg); }
                else if (c == '\b' || c == 0x7F) { field = 1; draw_panel(px, py, mode, field, avatar, user, pass, msg); }
                continue;
            }
            if (c == '\b' || c == 0x7F) {
                if (field == 0 && user_pos > 0) user[--user_pos] = '\0';
                else if (field == 1 && pass_pos > 0) pass[--pass_pos] = '\0';
                draw_panel(px, py, mode, field, avatar, user, pass, msg);
                continue;
            }
            if (c >= 32 && c < 127) {
                if (field == 0 && user_pos < 31) { user[user_pos++] = c; user[user_pos] = '\0'; }
                else if (field == 1 && pass_pos < 63) { pass[pass_pos++] = c; pass[pass_pos] = '\0'; }
                draw_panel(px, py, mode, field, avatar, user, pass, msg);
                continue;
            }
        }

        if (mode == 1) {                              // ---- create account ----
            serial_puts("[LOGIN] Creating account...\n");
            if (user[0] == '\0') { msg = "Username required"; continue; }
            if ((int)strlen(pass) < LOGIN_MIN_PASS) { msg = "Password too short (min 4)"; field = 1; continue; }
            auth_add_user(user, pass, avatar);
            if (auth_verify(user, pass)) {            // confirm it was actually created
                strncpy(g_login_user, user, sizeof(g_login_user) - 1);
                g_login_user[sizeof(g_login_user) - 1] = '\0';
                g_login_avatar = avatar;
                serial_puts("[LOGIN] OK.\n");
                return 1;
            }
            msg = "Could not create (name taken?)"; mode = 1; continue;
        }

        // ---- login ----
        serial_puts("[LOGIN] Verifying...\n");
        if (auth_verify(user, pass)) {
            strncpy(g_login_user, user, sizeof(g_login_user) - 1);
            g_login_user[sizeof(g_login_user) - 1] = '\0';
            g_login_avatar = auth_get_avatar(user);
            serial_puts("[LOGIN] OK.\n");
            return 1;
        }

        attempts++;
        serial_puts("[LOGIN] FAIL.\n");
        // Brute-force defence: escalate the delay, and after LOGIN_MAX_ATTEMPTS
        // impose a longer cooldown — but NEVER reboot. Returning 0 here made the
        // caller reboot, which under `-no-reboot` just closes the VM (and a user
        // fumbling the new sign-up could trip it). The login stays up instead, so
        // they can retry or press TAB to create an account.
        if (attempts >= LOGIN_MAX_ATTEMPTS) {
            msg = "Too many attempts - locked briefly";
            draw_panel(px, py, mode, 0, avatar, "", "", msg);
            serial_puts("[LOGIN] LOCKED (cooldown).\n");
            for (volatile long i = 0; i < 150000000L; i++);   // cooldown, then reset
            attempts = 0;
            msg = "You can try again, or press TAB to sign up";
            continue;
        }
        msg = (attempts == 1) ? "Invalid credentials (1/3)" : "Invalid credentials (2/3)";
        for (volatile long i = 0; i < 25000000L * attempts; i++);   // graduated throttle
    }
}
