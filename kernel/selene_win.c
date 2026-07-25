// ============================================================
// selene_win.c - Selene, the NyxOS web browser (v5.9.44, links + history v5.9.45)
// ============================================================
// A GUI window: a URL bar on top, a rendered page below, a status line at the bottom.
// Pressing Enter fetches the URL with http_get() (which drives the network itself),
// running DHCP first if we have no IP. The HTML is stripped to text (tags removed,
// <script>/<style> dropped, a few entities decoded, <title> pulled out) and word-
// wrapped into lines the view scrolls through.
//
// v5.9.45: <a href> links are parsed and rendered as coloured, underlined text.
// Tab / Shift-Tab move a selection between links, Enter follows the selected one (or
// loads the URL bar when nothing is selected), a click follows the link under the
// cursor, and Backspace goes Back through a per-window history stack. Relative and
// root-relative hrefs are resolved against the current page. HTTP only - no TLS yet.
#include "kernel.h"
#include "compositor.h"
#include "selene_win.h"
#include "http.h"
#include "font.h"

#define SEL_BAR       34        // top toolbar (back button + URL box) height
#define SEL_STATUS    20        // bottom status strip height
#define SEL_PAD       8
#define SEL_LINE_H    18        // px per rendered text row
#define SEL_WRAP      86        // wrap width in chars (SEL_WRAP*8 < content width)
#define SEL_LINE_COLS 96
#define SEL_MAX_LINES 1200
#define SEL_MAX_LINKS 240       // per page; link id stored as a uint8 (index+1)
#define SEL_HIST      32        // Back history depth

typedef struct { char url[192]; } sel_link_t;

typedef struct {
    char url[256];
    int  url_len;
    char cur_url[256];           // the URL of the page currently shown (for history)
    char title[96];
    char status[96];
    int  scroll;                 // top visible line index
    int  num_lines;
    char    (*lines)[SEL_LINE_COLS];   // kmalloc'd SEL_MAX_LINES rows of text
    uint8_t (*link_of)[SEL_LINE_COLS]; // per-char link id (0 = none, else link index+1)
    sel_link_t* links;                 // kmalloc'd SEL_MAX_LINKS
    int  num_links;
    int  sel_link;               // -1 = editing the URL bar; else the selected link index
    // base for resolving relative links (from the loaded URL)
    char     base_host[128];
    uint16_t base_port;
    char     base_path[256];
    // Back history (stack of URLs left behind)
    char hist[SEL_HIST][256];
    int  hist_len;
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

// Resolve an href against the loaded page's base into an absolute http:// URL in out
// (192 bytes). Empty out means "not navigable" (fragment, mailto:, javascript:).
static void selene_resolve(selene_ctx_t* s, const char* href, char* out) {
    while (*href == ' ') href++;
    out[0] = '\0';
    if (strncmp(href, "http://", 7) == 0)  { strncpy(out, href, 191); out[191] = '\0'; return; }
    if (strncmp(href, "https://", 8) == 0) { snprintf(out, 192, "http://%s", href + 8); return; }
    if (href[0] == '#' || href[0] == '\0') return;
    if (strncmp(href, "mailto:", 7) == 0 || strncmp(href, "javascript:", 11) == 0) return;
    if (href[0] == '/' && href[1] == '/')  { snprintf(out, 192, "http:%s", href); return; }

    char hostport[160];
    if (s->base_port != 80) snprintf(hostport, sizeof(hostport), "%s:%u", s->base_host, s->base_port);
    else                    snprintf(hostport, sizeof(hostport), "%s", s->base_host);

    if (href[0] == '/') { snprintf(out, 192, "http://%s%s", hostport, href); return; }

    // relative: keep the base path's directory (everything up to the last '/')
    char dir[200]; int last = -1;
    for (int k = 0; s->base_path[k] && k < 198; k++) if (s->base_path[k] == '/') last = k;
    if (last < 0) { dir[0] = '/'; dir[1] = '\0'; }
    else { int dl = last + 1; __builtin_memcpy(dir, s->base_path, dl); dir[dl] = '\0'; }
    snprintf(out, 192, "http://%s%s%s", hostport, dir, href);
}

// ---- word-wrap a stripped-text buffer into ctx->lines, carrying per-char link ids ----
static void wrap_text(selene_ctx_t* s, const char* txt, const uint8_t* tlink, uint32_t ti) {
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
            if (col > 0 && col < SEL_LINE_COLS - 1) { s->link_of[li][col] = tlink[i]; s->lines[li][col++] = ' '; }
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
            for (int k = 0; k < take && col < SEL_LINE_COLS - 1; k++) {
                s->link_of[li][col] = tlink[st + off + k];
                s->lines[li][col++] = txt[st + off + k];
            }
            off += take; wlen -= take;
        }
    }
    if (li < SEL_MAX_LINES) { s->lines[li][col] = '\0'; s->num_lines = li + 1; }
    else s->num_lines = SEL_MAX_LINES;
}

// Extract the href value from an <a ...> tag spanning body[j..te) into out (192).
static void extract_href(const uint8_t* body, uint32_t j, uint32_t te, char* out) {
    out[0] = '\0';
    for (uint32_t k = j; k + 4 < te; k++) {
        if (ci_starts(body + k, te - k, "href")) {
            uint32_t m = k + 4;
            while (m < te && (body[m] == ' ' || body[m] == '=')) m++;
            char q = 0;
            if (m < te && (body[m] == '"' || body[m] == '\'')) { q = (char)body[m]; m++; }
            uint32_t h = 0;
            while (m < te && h < 191) {
                char ch = (char)body[m];
                if (q ? (ch == q) : (ch == ' ' || ch == '>')) break;
                out[h++] = ch; m++;
            }
            out[h] = '\0';
            return;
        }
    }
}

// Ensure the text buffer ends with `want` newlines (1 = line break, 2 = a blank line
// between paragraphs), collapsing so runs of block tags don't pile up blank lines.
static uint32_t sel_ensure_nl(char* txt, uint8_t* tlink, uint32_t ti, uint32_t cap, int want) {
    int have = 0;
    while ((int)ti - 1 - have >= 0 && txt[ti - 1 - have] == '\n') have++;
    while (have < want && ti < cap) { txt[ti] = '\n'; tlink[ti] = 0; ti++; have++; }
    return ti;
}

// ---- strip HTML in `body` to text (capturing <a href> links), then wrap it ----
static void render_html(selene_ctx_t* s, const uint8_t* body, uint32_t len) {
    s->num_lines = 0; s->scroll = 0; s->title[0] = '\0';
    s->num_links = 0; s->sel_link = -1;
    __builtin_memset(s->link_of, 0, SEL_MAX_LINES * SEL_LINE_COLS);
    if (!body || !len) { s->num_lines = 0; return; }
    char*    txt   = (char*)kmalloc(len + 1);
    uint8_t* tlink = (uint8_t*)kmalloc(len + 1);
    if (!txt || !tlink) { if (txt) kfree(txt); if (tlink) kfree(tlink); return; }
    uint32_t ti = 0;
    int last_space = 1;
    int cur_link = 0;                                         // link id in progress (0 = none)
    int cur_hd = 0;                                           // inside an <h1>/<h2> (upper-case its text)
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
            if (sel_streq(name, "a")) {                        // hyperlink open/close
                uint32_t te = j; while (te < len && body[te] != '>') te++;
                if (close) {
                    cur_link = 0;
                } else if (s->num_links < SEL_MAX_LINKS) {
                    char href[192]; extract_href(body, j, te, href);
                    char abs[192]; selene_resolve(s, href, abs);
                    if (abs[0]) {
                        strncpy(s->links[s->num_links].url, abs, 191);
                        s->links[s->num_links].url[191] = '\0';
                        cur_link = s->num_links + 1;
                        s->num_links++;
                    }
                }
                i = te; if (i < len) i++;
                continue;
            }
            // Block-level layout so pages read as structure, not one wall of text:
            // headings (h1/h2 also upper-cased for emphasis), list bullets, rules, and
            // paragraph breaks; br/tr/dd/dt are single line breaks.
            int hlevel = (name[0]=='h' && name[1]>='1' && name[1]<='6' && name[2]=='\0') ? name[1]-'0' : 0;
            if (hlevel) {
                ti = sel_ensure_nl(txt, tlink, ti, len, 2);
                cur_hd = (!close && hlevel <= 2);
                if (close) ti = sel_ensure_nl(txt, tlink, ti, len, 2);
                last_space = 1;
            } else if (sel_streq(name, "hr")) {
                ti = sel_ensure_nl(txt, tlink, ti, len, 1);
                for (int d = 0; d < 64 && ti < len; d++) { txt[ti] = '-'; tlink[ti] = 0; ti++; }
                ti = sel_ensure_nl(txt, tlink, ti, len, 1);
                last_space = 1;
            } else if (!close && sel_streq(name, "li")) {
                ti = sel_ensure_nl(txt, tlink, ti, len, 1);
                if (ti + 2 < len) { txt[ti]='-'; tlink[ti]=0; ti++; txt[ti]=' '; tlink[ti]=0; ti++; }
                last_space = 1;
            } else if (sel_streq(name,"br") || sel_streq(name,"tr") || sel_streq(name,"dd") || sel_streq(name,"dt")) {
                ti = sel_ensure_nl(txt, tlink, ti, len, 1);
                last_space = 1;
            } else if (is_block_tag(name)) {
                ti = sel_ensure_nl(txt, tlink, ti, len, 2);
                last_space = 1;
            }
            i = j;
            while (i < len && body[i] != '>') i++;
            if (i < len) i++;
            continue;
        }
        if (c == '&') {
            char dec; uint32_t adv;
            if (decode_entity(body + i, len - i, &dec, &adv)) {
                if (dec == ' ') { if (!last_space) { txt[ti] = ' '; tlink[ti] = (uint8_t)cur_link; ti++; last_space = 1; } }
                else { if (cur_hd && dec >= 'a' && dec <= 'z') dec -= 32; txt[ti] = dec; tlink[ti] = (uint8_t)cur_link; ti++; last_space = 0; }
                i += adv;
            } else { txt[ti] = '&'; tlink[ti] = (uint8_t)cur_link; ti++; last_space = 0; i++; }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_space) { txt[ti] = ' '; tlink[ti] = (uint8_t)cur_link; ti++; last_space = 1; }
            i++;
            continue;
        }
        if (cur_hd && c >= 'a' && c <= 'z') c -= 32;          // upper-case h1/h2 text
        txt[ti] = c; tlink[ti] = (uint8_t)cur_link; ti++; last_space = 0; i++;
    }
    txt[ti] = '\0';
    wrap_text(s, txt, tlink, ti);
    kfree(txt); kfree(tlink);
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
    s->num_lines = 0; s->scroll = 0; s->title[0] = '\0'; s->num_links = 0; s->sel_link = -1;
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
    // record the base for resolving this page's relative links
    strncpy(s->base_host, host, sizeof(s->base_host)-1); s->base_host[sizeof(s->base_host)-1] = '\0';
    s->base_port = port;
    strncpy(s->base_path, path, sizeof(s->base_path)-1); s->base_path[sizeof(s->base_path)-1] = '\0';
    snprintf(s->status, sizeof(s->status), "Loading %s ...", host);
    compositor_redraw_now();

    http_response_t resp;
    if (http_get(host, port, path, &resp, iface) < 0) {
        snprintf(s->status, sizeof(s->status), "Could not load %s", host);
        return;
    }
    render_html(s, resp.body, resp.body_len);
    strncpy(s->cur_url, s->url, sizeof(s->cur_url)-1); s->cur_url[sizeof(s->cur_url)-1] = '\0';
    snprintf(s->status, sizeof(s->status), "%d %s  -  %s  -  %d links",
             resp.status_code, resp.status_text,
             s->title[0] ? s->title : host, s->num_links);
    http_free(&resp);
}

// Navigation helpers manage the Back history around selene_load.
static void push_hist(selene_ctx_t* s, const char* u) {
    if (!u[0]) return;
    if (s->hist_len >= SEL_HIST) {                    // drop the oldest
        for (int i = 1; i < SEL_HIST; i++) strcpy(s->hist[i-1], s->hist[i]);
        s->hist_len = SEL_HIST - 1;
    }
    strncpy(s->hist[s->hist_len], u, 255); s->hist[s->hist_len][255] = '\0';
    s->hist_len++;
}
static void selene_set_url(selene_ctx_t* s, const char* u) {
    strncpy(s->url, u, sizeof(s->url)-1); s->url[sizeof(s->url)-1] = '\0';
    s->url_len = (int)strlen(s->url);
}
static void selene_go(selene_ctx_t* s) {              // load the URL bar (a new navigation)
    if (s->cur_url[0] && strcmp(s->cur_url, s->url) != 0) push_hist(s, s->cur_url);
    selene_load(s);
}
static void selene_follow(selene_ctx_t* s, const char* url) {
    if (!url[0]) return;
    push_hist(s, s->cur_url);
    selene_set_url(s, url);
    selene_load(s);
}
static void selene_back(selene_ctx_t* s) {
    if (s->hist_len <= 0) return;
    s->hist_len--;
    selene_set_url(s, s->hist[s->hist_len]);
    selene_load(s);
}

void* selene_create_ctx(void) {
    selene_ctx_t* s = (selene_ctx_t*)kmalloc(sizeof(selene_ctx_t));
    if (!s) return NULL;
    __builtin_memset(s, 0, sizeof(*s));
    s->lines   = (char(*)[SEL_LINE_COLS])kmalloc(SEL_MAX_LINES * SEL_LINE_COLS);
    s->link_of = (uint8_t(*)[SEL_LINE_COLS])kmalloc(SEL_MAX_LINES * SEL_LINE_COLS);
    s->links   = (sel_link_t*)kmalloc(SEL_MAX_LINKS * sizeof(sel_link_t));
    if (!s->lines || !s->link_of || !s->links) {
        if (s->lines) kfree(s->lines);
        if (s->link_of) kfree(s->link_of);
        if (s->links) kfree(s->links);
        kfree(s); return NULL;
    }
    selene_set_url(s, "example.com");
    s->sel_link = -1;
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

// First line index where link id `lk` (1-based) appears, or -1.
static int link_first_line(selene_ctx_t* s, int lk) {
    for (int li = 0; li < s->num_lines; li++)
        for (int c = 0; c < SEL_LINE_COLS; c++)
            if (s->link_of[li][c] == lk) return li;
    return -1;
}

// Move the link selection by dir (+1/-1); -1 index means "URL bar".
static void select_link(selene_ctx_t* s, int dir) {
    if (s->num_links == 0) { s->sel_link = -1; return; }
    int n = s->num_links;
    s->sel_link += dir;
    if (s->sel_link >= n) s->sel_link = -1;         // past the last -> URL bar
    else if (s->sel_link < -1) s->sel_link = n - 1; // before URL bar -> last link
    if (s->sel_link >= 0) {
        int li = link_first_line(s, s->sel_link + 1);
        if (li >= 0) {                              // scroll it into view
            int rows = visible_rows();
            if (li < s->scroll) s->scroll = li;
            else if (li >= s->scroll + rows) s->scroll = li - rows + 1;
            clamp_scroll(s);
        }
    }
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

// Toolbar geometry, shared by draw + hit-testing.
#define SEL_BACK_X   30
#define SEL_BACK_W   18
#define SEL_URL_X    (SEL_BACK_X + SEL_BACK_W + 4)

void selene_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch) {
    (void)cw; (void)ch;
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (!s) return;

    uint32_t bar = fb_rgb(46, 40, 70);                        // brand-purple toolbar
    fb_fill_rect(cx, cy, SELENE_W, SEL_BAR, bar);
    sel_disc(cx + 15, cy + SEL_BAR/2, 8, fb_rgb(232, 230, 245));  // moon
    sel_disc(cx + 19, cy + SEL_BAR/2 - 1, 7, bar);               // carve crescent

    // Back button "<" — bright when there's history, dim otherwise.
    uint32_t bcol = s->hist_len > 0 ? fb_rgb(220, 215, 240) : fb_rgb(96, 90, 120);
    fb_fill_rect(cx + SEL_BACK_X, cy + 7, SEL_BACK_W, SEL_BAR - 14, fb_rgb(30, 26, 46));
    font_draw_string(cx + SEL_BACK_X + 5, cy + (SEL_BAR - FONT_HEIGHT)/2, "<", bcol, fb_rgb(30, 26, 46));

    int ux = cx + SEL_URL_X, uw = SELENE_W - SEL_URL_X - SEL_PAD, uy = cy + 6, uh = SEL_BAR - 12;
    fb_fill_rect(ux, uy, uw, uh, fb_rgb(22, 22, 30));         // URL box
    fb_fill_rect(ux, uy, uw, 1, fb_rgb(90, 80, 120));
    int uty = uy + (uh - FONT_HEIGHT) / 2;
    font_draw_string(ux + 6, uty, s->url, fb_rgb(235, 235, 245), fb_rgb(22, 22, 30));
    if (s->sel_link < 0) {                                   // caret only while editing the URL
        int caret_x = ux + 6 + s->url_len * FONT_WIDTH;
        if (caret_x < ux + uw - 2) fb_fill_rect(caret_x, uty, 2, FONT_HEIGHT, fb_rgb(180, 160, 230));
    }

    int cyy = cy + SEL_BAR;
    int content_h = SELENE_H - SEL_BAR - SEL_STATUS;
    uint32_t pg = fb_rgb(248, 248, 250);
    fb_fill_rect(cx, cyy, SELENE_W, content_h, pg);          // page (light)

    int rows = visible_rows();
    for (int r = 0; r < rows; r++) {
        int idx = s->scroll + r;
        if (idx >= s->num_lines) break;
        int py = cyy + SEL_PAD + r * SEL_LINE_H;
        font_draw_string(cx + SEL_PAD, py, s->lines[idx], fb_rgb(28, 30, 40), pg);   // base text

        // overlay link runs on this line: colour + underline, highlight the selected one
        int llen = (int)strlen(s->lines[idx]);
        int c0 = 0;
        while (c0 < llen) {
            uint8_t lk = s->link_of[idx][c0];
            int c1 = c0; while (c1 < llen && s->link_of[idx][c1] == lk) c1++;
            if (lk != 0) {
                int px = cx + SEL_PAD + c0 * FONT_WIDTH;
                int wpx = (c1 - c0) * FONT_WIDTH;
                int seld = (lk == s->sel_link + 1);
                uint32_t fg = seld ? fb_rgb(20, 20, 45) : fb_rgb(48, 96, 210);
                uint32_t bg = seld ? fb_rgb(196, 208, 255) : pg;
                if (seld) fb_fill_rect(px - 1, py - 1, wpx + 2, FONT_HEIGHT + 2, bg);
                char sub[SEL_LINE_COLS];
                int k = 0; for (; k < c1 - c0; k++) sub[k] = s->lines[idx][c0 + k];
                sub[k] = '\0';
                font_draw_string(px, py, sub, fg, bg);
                fb_fill_rect(px, py + FONT_HEIGHT - 1, wpx, 1, fg);   // underline
            }
            c0 = c1;
        }
    }
    if (s->num_lines == 0)
        font_draw_string(cx + SEL_PAD, cyy + SEL_PAD, "(no page loaded)", fb_rgb(150,150,160), pg);

    if (s->num_lines > rows) {                               // scrollbar
        int track_h = content_h - 4;
        int thumb_h = track_h * rows / s->num_lines; if (thumb_h < 12) thumb_h = 12;
        int maxs = s->num_lines - rows;
        int thumb_y = cyy + 2 + (maxs ? (track_h - thumb_h) * s->scroll / maxs : 0);
        fb_fill_rect(cx + SELENE_W - 5, cyy + 2, 3, track_h, fb_rgb(225, 225, 232));
        fb_fill_rect(cx + SELENE_W - 5, thumb_y, 3, thumb_h, fb_rgb(150, 130, 200));
    }

    // status: the selected link's target if one is selected, else the page status
    fb_fill_rect(cx, cy + SELENE_H - SEL_STATUS, SELENE_W, SEL_STATUS, fb_rgb(32, 30, 44));
    const char* st = (s->sel_link >= 0) ? s->links[s->sel_link].url : s->status;
    font_draw_string(cx + 6, cy + SELENE_H - SEL_STATUS + 3, st, fb_rgb(200, 200, 220), fb_rgb(32, 30, 44));
}

void selene_win_key(window_t* win, int key) {
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (!s) return;
    int rows = visible_rows();

    if (key == '\t') { select_link(s, +1); return; }         // Tab: next link (wraps to URL bar)
    if (key == 0x1B) { s->sel_link = -1; return; }           // Esc: back to the URL bar

    if (key == '\n' || key == '\r') {
        if (s->sel_link >= 0 && s->sel_link < s->num_links) selene_follow(s, s->links[s->sel_link].url);
        else selene_go(s);
        clamp_scroll(s);
        return;
    }
    if (key == '\b' || key == 0x7F) {
        // Back when a link is selected or the URL bar is untouched (still the current
        // page); once you start editing the URL, Backspace deletes a character instead.
        if (s->sel_link >= 0 || strcmp(s->url, s->cur_url) == 0) selene_back(s);
        else if (s->url_len > 0) s->url[--s->url_len] = '\0';
        return;
    }

    if (key == KEY_UP || key == KEY_WHEEL_UP)     { s->scroll -= 3; clamp_scroll(s); return; }
    if (key == KEY_DOWN || key == KEY_WHEEL_DOWN) { s->scroll += 3; clamp_scroll(s); return; }
    if (key == KEY_PGUP) { s->scroll -= (rows - 1); clamp_scroll(s); return; }
    if (key == KEY_PGDN) { s->scroll += (rows - 1); clamp_scroll(s); return; }
    if (key == KEY_HOME) { s->scroll = 0; return; }
    if (key == KEY_END)  { s->scroll = s->num_lines; clamp_scroll(s); return; }

    if (key >= 0x20 && key < 0x7F) {                          // typing edits the URL bar
        s->sel_link = -1;
        if (s->url_len < (int)sizeof(s->url) - 1) {
            s->url[s->url_len++] = (char)key;
            s->url[s->url_len] = '\0';
        }
    }
}

// Mouse click: the Back button, the URL bar (focus it), or a link in the page.
void selene_win_click(window_t* win, int mx, int my, int btn) {
    (void)btn;
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (!s) return;
    int cx = WIN_CLIENT_X(win), cy = WIN_CLIENT_Y(win);

    if (my >= cy && my < cy + SEL_BAR) {                      // toolbar
        if (mx >= cx + SEL_BACK_X && mx < cx + SEL_BACK_X + SEL_BACK_W) { selene_back(s); return; }
        s->sel_link = -1;                                    // clicking the URL bar edits it
        return;
    }
    int cyy = cy + SEL_BAR;
    int content_h = SELENE_H - SEL_BAR - SEL_STATUS;
    if (my < cyy || my >= cyy + content_h) return;
    int row = (my - cyy - SEL_PAD) / SEL_LINE_H;
    int col = (mx - cx - SEL_PAD) / FONT_WIDTH;
    int idx = s->scroll + row;
    if (idx < 0 || idx >= s->num_lines || col < 0 || col >= SEL_LINE_COLS) return;
    uint8_t lk = s->link_of[idx][col];
    if (lk != 0 && lk - 1 < s->num_links) { s->sel_link = lk - 1; selene_follow(s, s->links[lk-1].url); }
}
