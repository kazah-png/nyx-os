#include "theme.h"
#include "kernel.h"
#include "compositor.h"
#include "taskman_win.h"
#include "font.h"

#define HEADER_H 18
#define ROW_H FONT_HEIGHT
#define COL_PID 10
#define COL_NAME 50
#define COL_STATE 220
#define COL_CPU 310
#define COL_KILL 400

taskman_win_t* taskman_create_ctx(void) {
    taskman_win_t* tm = (taskman_win_t*)kmalloc(sizeof(taskman_win_t));
    if (!tm) return NULL;
    memset_asm(tm, 0, sizeof(taskman_win_t));
    return tm;
}

static const char* state_str(uint32_t state) {
    switch (state) {
        case 0: return "Zombie";
        case 1: return "Ready";
        case 2: return "Running";
        case 3: return "Blocked";
        default: return "Unknown";
    }
}

void taskman_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    taskman_win_t* tm = (taskman_win_t*)win->reserved;
    if (!tm) return;

    // Header background
    fb_fill_rect(cx, cy, cw, HEADER_H, THEME_PANEL_HEADER);

    // Header columns
    font_draw_string(cx + 4, cy + 2, "PID", fb_rgb(255,200,100), THEME_PANEL_HEADER);
    font_draw_string(cx + COL_PID + 4, cy + 2, "Name", fb_rgb(255,200,100), THEME_PANEL_HEADER);
    font_draw_string(cx + COL_NAME + 4, cy + 2, "State", fb_rgb(255,200,100), THEME_PANEL_HEADER);
    font_draw_string(cx + COL_STATE + 4, cy + 2, "CPU", fb_rgb(255,200,100), THEME_PANEL_HEADER);

    // Process list area
    int list_y = cy + HEADER_H;
    int avail_h = (int)ch - HEADER_H - HEADER_H; // leave room for status bar
    int max_rows = avail_h / ROW_H;
    if (max_rows < 1) max_rows = 1;

    for (int i = 0; i < max_rows; i++) {
        int idx = i + tm->scroll_offset;
        if (idx >= process_count) break;

        process_t* p = process_table[idx];
        if (!p) continue;

        int ry = list_y + i * ROW_H;
        uint32_t bg = (i & 1) ? fb_rgb(50,50,55) : THEME_WINDOW_BG;
        fb_fill_rect(cx, ry, cw, ROW_H, bg);

        char pid_str[8];
        snprintf(pid_str, sizeof(pid_str), "%u", p->pid);
        font_draw_string(cx + 4, ry + 1, pid_str, THEME_TEXT, bg);

        // Truncate name to fit column
        char name[25];
        strncpy(name, p->comm, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        font_draw_string(cx + COL_PID + 4, ry + 1, name, fb_rgb(180,220,180), bg);

        font_draw_string(cx + COL_NAME + 4, ry + 1, state_str(p->state), fb_rgb(200,200,200), bg);

        char cpu_str[16];
        snprintf(cpu_str, sizeof(cpu_str), "%u", p->cpu_time);
        font_draw_string(cx + COL_STATE + 4, ry + 1, cpu_str, fb_rgb(200,200,200), bg);
    }

    // Status bar at bottom
    int status_y = cy + (int)ch - HEADER_H;
    fb_fill_rect(cx, status_y, cw, HEADER_H, THEME_PANEL_HEADER);

    char status[128];
    uint64_t free_mem = memory_total > memory_used ? memory_total - memory_used : 0;
    snprintf(status, sizeof(status), "Mem: %u MB free / %u MB total  |  Procs: %d  |  Uptime: %u",
             (uint32_t)(free_mem / (1024 * 1024)),
             (uint32_t)(memory_total / (1024 * 1024)),
             process_count, get_ticks() / 1000);
    font_draw_string(cx + 4, status_y + 2, status, fb_rgb(180,200,220), THEME_PANEL_HEADER);
}

void taskman_win_click(window_t* win, int mx, int my, int btn) {
    (void)win;
    (void)mx;
    (void)my;
    (void)btn;
    // Future: select process row, kill button
}

void taskman_win_key(window_t* win, int key) {
    taskman_win_t* tm = (taskman_win_t*)win->reserved;
    if (!tm) return;

    if (key == KEY_UP && tm->scroll_offset > 0)
        tm->scroll_offset--;
    else if (key == KEY_DOWN && tm->scroll_offset < process_count - 1)
        tm->scroll_offset++;
}
