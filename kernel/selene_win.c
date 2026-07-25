// ============================================================
// selene_win.c - Selene, the NyxOS web browser (v5.9.44)
// ============================================================
// A GUI window: a URL bar on top, a rendered page below, a status line at the bottom.
// Pressing Enter fetches the URL with http_get() (which drives the network itself),
// running DHCP first if we have no IP. The HTML is stripped to text (tags removed,
// <script>/<style> dropped, a few entities decoded, <title> pulled out) and word-
// wrapped into lines the view scrolls through. HTTP only - no TLS yet.
#include "kernel.h"
#include "compositor.h"
#include "selene_win.h"
#include "http.h"
#include "font.h"

#define SEL_BAR       34        // top toolbar (URL box) height
#define SEL_STATUS    20        // bottom status strip height
#define SEL_PAD       8
#define SEL_LINE_H    18        // px per rendered text row
#define SEL_WRAP      86        // wrap width in chars (SEL_WRAP*8 < content width)
#define SEL_LINE_COLS 96
#define SEL_MAX_LINES 1200

typedef struct {
    char url[256];
    int  url_len;
    char title[96];
    char status[96];
    int  scroll;                 // top visible line index
    int  num_lines;
    char (*lines)[SEL_LINE_COLS]; // kmalloc'd SEL_MAX_LINES rows
} selene_ctx_t;

// ---- small string helpers (name buffers are already lowercased) ----

static int sel_streq(const char* a, const char* b) { return strcmp(a, b) == 0; }

static int is_block_tag(const char* n) {
    static const char* B[] = { "p","br","div","h1","h2","h3","h4","h5","h6","li","tr",
        "hr","ul","ol","table","section","article","header","footer","nav","form",
        "blockquote","pre","dd","dt","figure",0 };
    for (int i = 0; B[i]; i++) if (sel_streq(n, B[i])) return 1;
    return 0;
}

// Decode an HTML entity at p (len bytes available). On success writes the char to
// *out, sets *adv to the bytes consumed (including the trailing ';' if present),
// and returns 1. Handles &amp; &lt; &gt; &quot; &apos; &nbsp; and numeric &#nn; / &#xhh;.
static int decode_entity(const uint8_t* p, uint32_t len, char* out, uint32_t* adv) {
    if (len < 3 || p[0] != '&') return 0;
    // find the ';' within a short window
    uint32_t semi = 0;
    for (uint32_t i = 1; i < len && i < 10; i++) if (p[i] == ';') { semi = i; break; }
    if (!semi) return 0;
    if (p[1] == '#') {
        int hex = (p[2] == 'x' || p[2] == 'X');
        uint32_t v = 0;
        for (uint32_t i = hex ? 3 : 2; i < semi; i++) {
            char c = (char)p[i];
            if (hex) {
                if (c >= '0' && c <= '9') v = v*16 + (c - '0');
                else if (c >= 'a' && c <= 'f') v = v*16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v = v*16 + (c - 'A' + 10);
                else return 0;
            } else {
                if (c >= '0' && c <= '9') v = v*10 + (c - '0'); else return 0;
            }
        }
        *out = (v >= 0x20 && v < 0x7F) ? (char)v : (v == 0xA0 ? ' ' : '?');
        *adv = semi + 1;
        return 1;
    }
    char name[8]; uint32_t nl = 0;
    for (uint32_t i = 1; i < semi && nl < 7; i++) name[nl++] = (char)p[i];
    name[nl] = '\0';
    char d = 0;
    if (sel_streq(name, "amp")) d = '&';
    else if (sel_streq(name, "lt")) d = '<';
    else if (sel_streq(name, "gt")) d = '>';
    else if (sel_streq(name, "quot")) d = '"';
    else if (sel_streq(name, "apos")) d = '\'';
    else if (sel_streq(name, "nbsp")) d = ' ';
    else return 0;
    *out = d; *adv = semi + 1;
    return 1;
}

// Case-insensitive: does p start with the (lowercase) literal lit?
static int ci_starts(const uint8_t* p, uint32_t len, const char* lit) {
    for (uint32_t i = 0; lit[i]; i++) {
        if (i >= len) return 0;
        char c = (char)p[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != lit[i]) return 0;
    }
    return 1;
}

// ---- word-wrap a stripped-text buffer into ctx->lines ----
static void wrap_text(selene_ctx_t* s, const char* txt, uint32_t ti) {
    int li = 0, col = 0;
    s->lines[0][0] = '\0';
    uint32_t i = 0;
    while (i < ti && li < SEL_MAX_LINES) {
        char c = txt[i];
        if (c == '\n') {
            s->lines[li][col] = '\0';
            li++; col = 0;
            if (li < SEL_MAX_LINES) s->lines[li][0] = '\0';
            i++;
            continue;
        }
        if (c == ' ') {
            if (col > 0 && col < SEL_LINE_COLS - 1) s->lines[li][col++] = ' ';
            i++;
            continue;
        }
        uint32_t st = i;
        while (i < ti && txt[i] != ' ' && txt[i] != '\n') i++;
        int wlen = (int)(i - st);
        int off = 0;
        while (wlen > 0 && li < SEL_MAX_LINES) {
            if (col > 0 && col + wlen > SEL_WRAP) {           // word won't fit: next line
                if (col > 0 && s->lines[li][col-1] == ' ') col--;
                s->lines[li][col] = '\0';
                li++; col = 0;
                if (li >= SEL_MAX_LINES) break;
                s->lines[li][0] = '\0';
            }
            int take = wlen;
            if (take > SEL_WRAP - col) take = SEL_WRAP - col; // hard-split an over-long word
            if (take <= 0) { s->lines[li][col] = '\0'; li++; col = 0; if (li < SEL_MAX_LINES) s->lines[li][0]='\0'; continue; }
            for (int k = 0; k < take && col < SEL_LINE_COLS - 1; k++) s->lines[li][col++] = txt[st + off + k];
            off += take; wlen -= take;
        }
    }
    if (li < SEL_MAX_LINES) { s->lines[li][col] = '\0'; s->num_lines = li + 1; }
    else s->num_lines = SEL_MAX_LINES;
}

// ---- strip HTML in `body` to text, then wrap it into the line buffer ----
static void render_html(selene_ctx_t* s, const uint8_t* body, uint32_t len) {
    s->num_lines = 0; s->scroll = 0; s->title[0] = '\0';
    if (!body || !len) { s->num_lines = 0; return; }
    char* txt = (char*)kmalloc(len + 1);
    if (!txt) return;
    uint32_t ti = 0;
    int last_space = 1;                                       // collapse whitespace; drop leading
    for (uint32_t i = 0; i < len && ti < len; ) {
        char c = (char)body[i];
        if (c == '<') {
            uint32_t j = i + 1;
            int close = 0;
            if (j < len && body[j] == '/') { close = 1; j++; }
            char name[10]; int nl = 0;
            while (j < len && nl < 9) {
                char t = (char)body[j];
                if ((t>='a'&&t<='z')||(t>='A'&&t<='Z')||(t>='0'&&t<='9')) { name[nl++] = (t>='A'&&t<='Z')?t+32:t; j++; }
                else break;
            }
            name[nl] = '\0';
            if (!close && (sel_streq(name,"script") || sel_streq(name,"style"))) {
                const char* end = sel_streq(name,"script") ? "</script" : "</style";
                uint32_t k = j;
                while (k < len && !(body[k]=='<' && ci_starts(body+k, len-k, end))) k++;
                i = k;
                while (i < len && body[i] != '>') i++;
                if (i < len) i++;
                continue;
            }
            if (!close && sel_streq(name,"title")) {
                uint32_t k = j; while (k < len && body[k] != '>') k++; if (k < len) k++;
                int tl = 0;
                while (k < len && body[k] != '<' && tl < 95) {
                    char t = (char)body[k]; if (t=='\n'||t=='\r'||t=='\t') t = ' ';
                    s->title[tl++] = t; k++;
                }
                s->title[tl] = '\0';
                i = k;
                continue;
            }
            if (is_block_tag(name) && ti > 0 && txt[ti-1] != '\n') { txt[ti++] = '\n'; last_space = 1; }
            i = j;
            while (i < len && body[i] != '>') i++;
            if (i < len) i++;
            continue;
        }
        if (c == '&') {
            char dec; uint32_t adv;
            if (decode_entity(body + i, len - i, &dec, &adv)) {
                if (dec == ' ') { if (!last_space) { txt[ti++] = ' '; last_space = 1; } }
                else { txt[ti++] = dec; last_space = 0; }
                i += adv;
            } else { txt[ti++] = '&'; last_space = 0; i++; }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_space) { txt[ti++] = ' '; last_space = 1; }
            i++;
            continue;
        }
        txt[ti++] = c; last_space = 0; i++;
    }
    txt[ti] = '\0';
    wrap_text(s, txt, ti);
    kfree(txt);
}

// Parse http[://]host[:port][/path] into host/port/path (same shape as `httpget`).
static void parse_url(const char* url, char* host, uint16_t* port, char* path) {
    const char* p = url;
    while (*p == ' ') p++;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;   // no TLS; try plain :80 anyway
    const char* hs = p;
    while (*p && *p != ':' && *p != '/') p++;
    int hl = (int)(p - hs); if (hl > 127) hl = 127;
    __builtin_memcpy(host, hs, hl); host[hl] = '\0';
    *port = 80;
    if (*p == ':') { p++; uint16_t v = 0; while (*p >= '0' && *p <= '9') { v = v*10 + (*p - '0'); p++; } *port = v ? v : 80; }
    if (*p == '/') { strncpy(path, p, 255); path[255] = '\0'; }
    else { path[0] = '/'; path[1] = '\0'; }
}

static int find_iface(void) {
    for (int i = 0; i < 8; i++)
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) return i;
    return -1;
}

// Fetch ctx->url and render it. Blocks (http_get drives the net) - we paint a
// "Loading" status first via compositor_redraw_now so the UI shows progress.
static void selene_load(selene_ctx_t* s) {
    s->num_lines = 0; s->scroll = 0; s->title[0] = '\0';
    int iface = find_iface();
    if (iface < 0) { strncpy(s->status, "No network interface (boot with -nic)", 95); return; }
    if (net_interfaces[iface].ip == 0) {
        strncpy(s->status, "Getting an IP address (DHCP)...", 95);
        compositor_redraw_now();
        dhcp_request(iface);
    }
    char host[128] = {0}, path[256] = {0}; uint16_t port = 80;
    parse_url(s->url, host, &port, path);
    if (!host[0]) { strncpy(s->status, "Enter a URL, e.g. example.com", 95); return; }
    snprintf(s->status, sizeof(s->status), "Loading %s ...", host);
    compositor_redraw_now();

    http_response_t resp;
    if (http_get(host, port, path, &resp, iface) < 0) {
        snprintf(s->status, sizeof(s->status), "Could not load %s", host);
        return;
    }
    render_html(s, resp.body, resp.body_len);
    snprintf(s->status, sizeof(s->status), "%d %s  -  %s  (%u bytes)",
             resp.status_code, resp.status_text,
             s->title[0] ? s->title : host, (unsigned)resp.body_len);
    http_free(&resp);
}

void* selene_create_ctx(void) {
    selene_ctx_t* s = (selene_ctx_t*)kmalloc(sizeof(selene_ctx_t));
    if (!s) return NULL;
    s->lines = (char(*)[SEL_LINE_COLS])kmalloc(SEL_MAX_LINES * SEL_LINE_COLS);
    if (!s->lines) { kfree(s); return NULL; }
    strncpy(s->url, "example.com", sizeof(s->url) - 1);
    s->url[sizeof(s->url)-1] = '\0';
    s->url_len = (int)strlen(s->url);
    s->title[0] = '\0';
    s->scroll = 0; s->num_lines = 0;
    strncpy(s->status, "Press Enter to load, or edit the URL", 95);
    return s;
}

// launch_selene calls this right after creating the window, so the browser opens
// already showing its default page instead of a blank view.
void selene_first_load(window_t* win) {
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (s) selene_load(s);
}

static int visible_rows(void) {
    return (SELENE_H - SEL_BAR - SEL_STATUS - SEL_PAD) / SEL_LINE_H;
}

static void clamp_scroll(selene_ctx_t* s) {
    int maxs = s->num_lines - visible_rows();
    if (maxs < 0) maxs = 0;
    if (s->scroll > maxs) s->scroll = maxs;
    if (s->scroll < 0) s->scroll = 0;
}

// A little crescent moon for the toolbar (Selene). Light disc, then carve it with an
// offset disc in the toolbar colour.
static void sel_disc(int ccx, int ccy, int r, uint32_t col) {
    for (int dy = -r; dy <= r; dy++) {
        int w = 0;
        while ((w + 1) * (w + 1) + dy * dy <= r * r) w++;
        fb_fill_rect(ccx - w, ccy + dy, 2 * w + 1, 1, col);
    }
}

void selene_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (!s) return;

    uint32_t bar = fb_rgb(46, 40, 70);                        // brand-purple toolbar
    fb_fill_rect(cx, cy, SELENE_W, SEL_BAR, bar);
    sel_disc(cx + 17, cy + SEL_BAR/2, 9, fb_rgb(232, 230, 245));  // moon
    sel_disc(cx + 21, cy + SEL_BAR/2 - 1, 8, bar);               // carve crescent

    int ux = cx + 34, uw = SELENE_W - 34 - SEL_PAD, uy = cy + 6, uh = SEL_BAR - 12;
    fb_fill_rect(ux, uy, uw, uh, fb_rgb(22, 22, 30));         // URL box
    fb_fill_rect(ux, uy, uw, 1, fb_rgb(90, 80, 120));
    int uty = uy + (uh - FONT_HEIGHT) / 2;
    font_draw_string(ux + 6, uty, s->url, fb_rgb(235, 235, 245), fb_rgb(22, 22, 30));
    int caret_x = ux + 6 + s->url_len * FONT_WIDTH;           // caret at end of URL
    if (caret_x < ux + uw - 2) fb_fill_rect(caret_x, uty, 2, FONT_HEIGHT, fb_rgb(180, 160, 230));

    int cyy = cy + SEL_BAR;
    int content_h = SELENE_H - SEL_BAR - SEL_STATUS;
    fb_fill_rect(cx, cyy, SELENE_W, content_h, fb_rgb(248, 248, 250));   // page (light)

    int rows = visible_rows();
    for (int r = 0; r < rows; r++) {
        int idx = s->scroll + r;
        if (idx >= s->num_lines) break;
        font_draw_string(cx + SEL_PAD, cyy + SEL_PAD + r * SEL_LINE_H,
                         s->lines[idx], fb_rgb(28, 30, 40), fb_rgb(248, 248, 250));
    }
    if (s->num_lines == 0) {
        font_draw_string(cx + SEL_PAD, cyy + SEL_PAD, "(no page loaded)",
                         fb_rgb(150, 150, 160), fb_rgb(248, 248, 250));
    }

    // scroll indicator
    if (s->num_lines > rows) {
        int track_h = content_h - 4;
        int thumb_h = track_h * rows / s->num_lines; if (thumb_h < 12) thumb_h = 12;
        int maxs = s->num_lines - rows;
        int thumb_y = cyy + 2 + (maxs ? (track_h - thumb_h) * s->scroll / maxs : 0);
        fb_fill_rect(cx + SELENE_W - 5, cyy + 2, 3, track_h, fb_rgb(225, 225, 232));
        fb_fill_rect(cx + SELENE_W - 5, thumb_y, 3, thumb_h, fb_rgb(150, 130, 200));
    }

    fb_fill_rect(cx, cy + SELENE_H - SEL_STATUS, SELENE_W, SEL_STATUS, fb_rgb(32, 30, 44));
    font_draw_string(cx + 6, cy + SELENE_H - SEL_STATUS + 3, s->status,
                     fb_rgb(200, 200, 220), fb_rgb(32, 30, 44));
}

void selene_win_key(window_t* win, int key) {
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (!s) return;
    int rows = visible_rows();

    if (key == '\n' || key == '\r') { selene_load(s); clamp_scroll(s); return; }
    if (key == '\b' || key == 0x7F) { if (s->url_len > 0) s->url[--s->url_len] = '\0'; return; }

    if (key == KEY_UP || key == KEY_WHEEL_UP)     { s->scroll -= 3; clamp_scroll(s); return; }
    if (key == KEY_DOWN || key == KEY_WHEEL_DOWN) { s->scroll += 3; clamp_scroll(s); return; }
    if (key == KEY_PGUP) { s->scroll -= (rows - 1); clamp_scroll(s); return; }
    if (key == KEY_PGDN) { s->scroll += (rows - 1); clamp_scroll(s); return; }
    if (key == KEY_HOME) { s->scroll = 0; return; }
    if (key == KEY_END)  { s->scroll = s->num_lines; clamp_scroll(s); return; }

    if (key >= 0x20 && key < 0x7F) {                          // edit the URL
        if (s->url_len < (int)sizeof(s->url) - 1) {
            s->url[s->url_len++] = (char)key;
            s->url[s->url_len] = '\0';
        }
    }
}
