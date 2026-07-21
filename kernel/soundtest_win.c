#include "theme.h"
#include "kernel.h"
#include "compositor.h"
#include "soundtest_win.h"
#include "speaker.h"
#include "sb16.h"
#include "font.h"

#define TOOLBAR_H 30
#define STATUS_H 20
#define BTN_H 32
#define BTN_W 180
#define BTN_SPACING 8

// Fixed-point sine table: 256 entries, Q16.16 format
static int32_t sin_table[256];
static int sin_table_init = 0;

static void init_sin_table(void) {
    if (sin_table_init) return;
    sin_table_init = 1;
    for (int i = 0; i < 256; i++) {
        double a = (double)i / 256.0 * 3.1415926535 * 2.0;
        // Polynomial approximation for sine [0, 2pi)
        double x = a;
        if (a > 3.1415926535) x = a - 3.1415926535;
        double x2 = x * x;
        double val = x - x2 * x / 6.0 + x2 * x2 * x / 120.0 - x2 * x2 * x2 * x / 5040.0;
        if (a > 3.1415926535) val = -val;
        sin_table[i] = (int32_t)(val * 65536.0);
    }
}

// Q16.16 fixed-point sine
static int32_t sin_fp(uint32_t phase_q16) {
    return sin_table[(phase_q16 >> 8) & 0xFF];
}

static void gen_sine(uint8_t* buf, uint32_t len, uint32_t freq, uint32_t sample_rate) {
    init_sin_table();
    int32_t phase = 0;
    int32_t step = (int32_t)(((uint64_t)freq << 24) / sample_rate);
    for (uint32_t i = 0; i < len; i++) {
        int32_t val = sin_fp(phase);
        phase = (int32_t)((uint32_t)phase + (uint32_t)step);
        buf[i] = (uint8_t)(128 + (val >> 9));
    }
}

static void gen_square(uint8_t* buf, uint32_t len, uint32_t freq, uint32_t sample_rate) {
    uint32_t half_cycle = sample_rate / (freq * 2);
    if (half_cycle == 0) half_cycle = 1;
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = ((i / half_cycle) & 1) ? 200 : 56;
    }
}

static void gen_sweep(uint8_t* buf, uint32_t len, uint32_t sample_rate) {
    init_sin_table();
    uint32_t phase = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t freq = 200 + (i * 2800) / len;
        uint32_t step = (uint32_t)(((uint64_t)freq << 24) / sample_rate);
        int32_t val = sin_fp((int32_t)phase);
        phase += step;
        buf[i] = (uint8_t)(128 + (val >> 8));
    }
}

static uint8_t audio_buf[65536];

soundtest_win_t* soundtest_create_ctx(void) {
    soundtest_win_t* st = (soundtest_win_t*)kmalloc(sizeof(soundtest_win_t));
    if (!st) return NULL;
    memset_asm(st, 0, sizeof(soundtest_win_t));
    snprintf(st->status, sizeof(st->status), "Sound Test — click a button");
    return st;
}

void soundtest_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    soundtest_win_t* st = (soundtest_win_t*)win->reserved;
    if (!st) return;

    fb_fill_rect(cx, cy, cw, ch, fb_rgb(35,37,42));

    int x = cx + 12;
    int y = cy + 12;
    int bw = BTN_W;
    int bh = BTN_H;

    font_draw_string(x, y, "PC Speaker:", fb_rgb(180,200,220), fb_rgb(35,37,42));
    y += FONT_HEIGHT + 6;

    uint32_t btn_color = THEME_SELECTION;
    uint32_t btn_text = fb_rgb(255,255,255);

    fb_fill_rect(x, y, bw, bh, btn_color);
    font_draw_string(x + (bw - 7*FONT_WIDTH)/2, y + (bh - FONT_HEIGHT)/2, "Beep 440Hz", btn_text, btn_color);
    y += bh + 4;

    fb_fill_rect(x, y, bw, bh, btn_color);
    font_draw_string(x + (bw - 6*FONT_WIDTH)/2, y + (bh - FONT_HEIGHT)/2, "Melody C", btn_text, btn_color);
    y += bh + 4;

    fb_fill_rect(x, y, bw, bh, fb_rgb(80,60,60));
    font_draw_string(x + (bw - 6*FONT_WIDTH)/2, y + (bh - FONT_HEIGHT)/2, "Alarm!", btn_text, fb_rgb(80,60,60));
    y += bh + 12;

    font_draw_string(x, y, "Sound Blaster 16:", fb_rgb(180,200,220), fb_rgb(35,37,42));
    y += FONT_HEIGHT + 6;

    int sb16_ok = sb16_is_initialized();

    btn_color = sb16_ok ? THEME_SELECTION : fb_rgb(60,60,60);
    fb_fill_rect(x, y, bw, bh, btn_color);
    font_draw_string(x + (bw - 11*FONT_WIDTH)/2, y + (bh - FONT_HEIGHT)/2,
                     sb16_ok ? "Sine 440Hz" : "N/A (no SB16)", btn_text, btn_color);
    y += bh + 4;

    fb_fill_rect(x, y, bw, bh, btn_color);
    font_draw_string(x + (bw - 12*FONT_WIDTH)/2, y + (bh - FONT_HEIGHT)/2,
                     sb16_ok ? "Square 220Hz" : "", btn_text, btn_color);
    y += bh + 4;

    fb_fill_rect(x, y, bw, bh, sb16_ok ? fb_rgb(80,60,60) : fb_rgb(60,60,60));
    font_draw_string(x + (bw - 12*FONT_WIDTH)/2, y + (bh - FONT_HEIGHT)/2,
                     sb16_ok ? "Sweep 200-3kHz" : "", btn_text, sb16_ok ? fb_rgb(80,60,60) : fb_rgb(60,60,60));

    int status_y = cy + (int)ch - STATUS_H;
    fb_fill_rect(cx, status_y, cw, STATUS_H, THEME_WINDOW_BG);
    font_draw_string(cx + 4, (uint32_t)status_y + 2, st->status, fb_rgb(180,200,220), THEME_WINDOW_BG);
}

void soundtest_win_click(window_t* win, int mx, int my, int btn) {
    soundtest_win_t* st = (soundtest_win_t*)win->reserved;
    if (!st || btn != 1) return;

    int x0 = WIN_CLIENT_X(win) + 12;
    int y0 = WIN_CLIENT_Y(win) + 12 + FONT_HEIGHT + 6;
    int bw = BTN_W;
    int bh = BTN_H;

    // PC Speaker buttons (3)
    for (int i = 0; i < 3; i++) {
        int by = y0 + i * (bh + 4);
        if (my >= by && my < by + bh && mx >= x0 && mx < x0 + bw) {
            if (i == 0) {
                speaker_beep(440, 300);
                snprintf(st->status, sizeof(st->status), "PC Speaker: 440Hz beep");
            } else if (i == 1) {
                snprintf(st->status, sizeof(st->status), "Playing melody...");
                int notes[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_G4, NOTE_E4, NOTE_C4, NOTE_REST};
                int durs[] = {200, 200, 200, 400, 200, 200, 400, 200};
                for (int j = 0; j < 8; j++)
                    speaker_play_note(notes[j], durs[j]);
                snprintf(st->status, sizeof(st->status), "Melody done");
            } else {
                snprintf(st->status, sizeof(st->status), "Alarm!");
                for (int j = 0; j < 5; j++) {
                    speaker_beep(880, 150);
                    sleep(50);
                    speaker_beep(440, 150);
                    sleep(50);
                }
                snprintf(st->status, sizeof(st->status), "Alarm done");
            }
            return;
        }
    }

    // SB16 buttons (3)
    y0 = y0 + 3 * (bh + 4) + 12 + FONT_HEIGHT + 6;
    int sb16_ok = sb16_is_initialized();
    for (int i = 0; i < 3; i++) {
        int by = y0 + i * (bh + 4);
        if (my >= by && my < by + bh && mx >= x0 && mx < x0 + bw) {
            if (!sb16_ok) {
                snprintf(st->status, sizeof(st->status), "SB16 not available");
                return;
            }
            uint32_t len = 44100;
            if (len > sizeof(audio_buf)) len = sizeof(audio_buf);
            if (i == 0) {
                gen_sine(audio_buf, len, 440, 44100);
                sb16_play_sound(audio_buf, len, 44100, 8);
                snprintf(st->status, sizeof(st->status), "Playing sine 440Hz (1s)");
            } else if (i == 1) {
                gen_square(audio_buf, len, 220, 44100);
                sb16_play_sound(audio_buf, len, 44100, 8);
                snprintf(st->status, sizeof(st->status), "Playing square 220Hz (1s)");
            } else {
                gen_sweep(audio_buf, len, 44100);
                sb16_play_sound(audio_buf, len, 44100, 8);
                snprintf(st->status, sizeof(st->status), "Playing sweep 200-3000Hz (1s)");
            }
            return;
        }
    }
}
