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
#include "tls.h"
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
#define SEL_MAX_FORMS  16       // <form>s per page
#define SEL_MAX_FIELDS 64       // form controls per page; field id stored as a uint8 (index+1)
#define SEL_FIELD_W    22       // rendered width (chars) of a text-input box

// Form control kinds.
#define SEL_FLD_TEXT   0        // text/search/email/url/password/tel/number -> editable box
#define SEL_FLD_SUBMIT 1        // submit button (<input type=submit> or <button>)
#define SEL_FLD_HIDDEN 2        // hidden field: carried into the submission, not shown

typedef struct { char url[192]; } sel_link_t;
typedef struct { char action[192]; uint8_t method; } sel_form_t;   // method 0 = GET (only GET so far)
typedef struct { int form; uint8_t kind; char name[64]; char value[160]; } sel_field_t;

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
    // HTML forms: a per-char field-id grid (like link_of), the fields, and their owning forms
    uint8_t (*field_of)[SEL_LINE_COLS]; // per-char field id (0 = none, else field index+1)
    sel_field_t* fields;               // kmalloc'd SEL_MAX_FIELDS
    int  num_fields;
    sel_form_t*  forms;                // kmalloc'd SEL_MAX_FORMS
    int  num_forms;
    int  sel_field;              // focused form field index (-1 = none)
    // base for resolving relative links (from the loaded URL)
    char     base_host[128];
    uint16_t base_port;
    int      base_https;         // the loaded page's scheme (for resolving relative links)
    char     base_path[256];
    // Back history (stack of URLs left behind)
    char hist[SEL_HIST][256];
    int  hist_len;
    // Find-in-page (Ctrl+F): a query, whether the find bar is capturing input, and match tracking.
    char find_q[64];
    int  find_len;
    int  find_active;            // 1 = the find bar is open and taking keystrokes
    int  find_matches;           // total matches of find_q on the page
    int  find_cur;               // index of the current (accented) match, 0-based
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
    const char* scheme = s->base_https ? "https" : "http";
    if (strncmp(href, "http://", 7) == 0)  { strncpy(out, href, 191); out[191] = '\0'; return; }
    if (strncmp(href, "https://", 8) == 0) { strncpy(out, href, 191); out[191] = '\0'; return; }   // keep https — Selene speaks TLS now
    if (href[0] == '#' || href[0] == '\0') return;
    if (strncmp(href, "mailto:", 7) == 0 || strncmp(href, "javascript:", 11) == 0) return;
    if (href[0] == '/' && href[1] == '/')  { snprintf(out, 192, "%s:%s", scheme, href); return; }

    char hostport[160];
    uint16_t defport = s->base_https ? 443 : 80;
    if (s->base_port != defport) snprintf(hostport, sizeof(hostport), "%s:%u", s->base_host, s->base_port);
    else                         snprintf(hostport, sizeof(hostport), "%s", s->base_host);

    if (href[0] == '/') { snprintf(out, 192, "%s://%s%s", scheme, hostport, href); return; }

    // relative: keep the base path's directory (everything up to the last '/')
    char dir[200]; int last = -1;
    for (int k = 0; s->base_path[k] && k < 198; k++) if (s->base_path[k] == '/') last = k;
    if (last < 0) { dir[0] = '/'; dir[1] = '\0'; }
    else { int dl = last + 1; __builtin_memcpy(dir, s->base_path, dl); dir[dl] = '\0'; }
    snprintf(out, 192, "%s://%s%s%s", scheme, hostport, dir, href);
}

// ---- word-wrap a stripped-text buffer into ctx->lines, carrying per-char link ids ----
static void wrap_text(selene_ctx_t* s, const char* txt, const uint8_t* tlink, const uint8_t* tfield, uint32_t ti) {
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
            if (col > 0 && col < SEL_LINE_COLS - 1) { s->link_of[li][col] = tlink[i]; s->field_of[li][col] = tfield[i]; s->lines[li][col++] = ' '; }
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
                s->field_of[li][col] = tfield[st + off + k];
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

// Extract a named attribute value from a tag spanning body[j..te) into out (outcap bytes).
// Case-insensitive attribute name, honours single/double quotes; empty out if absent.
static void extract_attr(const uint8_t* body, uint32_t j, uint32_t te, const char* attr,
                         char* out, uint32_t outcap) {
    out[0] = '\0';
    uint32_t al = 0; while (attr[al]) al++;
    for (uint32_t k = j; k + al < te; k++) {
        if (k > j) { char pc = (char)body[k-1];          // require a word boundary before the name
            if (!(pc==' '||pc=='\t'||pc=='\n'||pc=='\r'||pc=='/')) continue; }
        if (!ci_starts(body + k, te - k, attr)) continue;
        uint32_t m = k + al;
        while (m < te && (body[m]==' '||body[m]=='\t'||body[m]=='\n'||body[m]=='\r')) m++;
        if (m >= te || body[m] != '=') continue;         // must be name=value
        m++;
        while (m < te && (body[m]==' '||body[m]=='\t'||body[m]=='\n'||body[m]=='\r')) m++;
        char q = 0;
        if (m < te && (body[m]=='"' || body[m]=='\'')) { q = (char)body[m]; m++; }
        uint32_t h = 0;
        while (m < te && h + 1 < outcap) {
            char ch = (char)body[m];
            if (q ? (ch == q) : (ch==' '||ch=='>'||ch=='\t'||ch=='\n'||ch=='\r')) break;
            out[h++] = ch; m++;
        }
        out[h] = '\0';
        return;
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

// Emit a run of literal characters into the text stream, tagged with a form-field id (0 = none).
static void sel_emit(char* txt, uint8_t* tlink, uint8_t* tfield, uint32_t* ti, uint32_t cap,
                     const char* str, int fid) {
    for (const char* p = str; *p && *ti < cap; p++) {
        txt[*ti] = *p; tlink[*ti] = 0; tfield[*ti] = (uint8_t)fid; (*ti)++;
    }
}

// ---- strip HTML in `body` to text (capturing <a href> links + form controls), then wrap it ----
static void render_html(selene_ctx_t* s, const uint8_t* body, uint32_t len) {
    s->num_lines = 0; s->scroll = 0; s->title[0] = '\0';
    s->num_links = 0; s->sel_link = -1;
    s->num_fields = 0; s->num_forms = 0; s->sel_field = -1;
    __builtin_memset(s->link_of, 0, SEL_MAX_LINES * SEL_LINE_COLS);
    __builtin_memset(s->field_of, 0, SEL_MAX_LINES * SEL_LINE_COLS);
    if (!body || !len) { s->num_lines = 0; return; }
    char*    txt    = (char*)kmalloc(len + 1);
    uint8_t* tlink  = (uint8_t*)kmalloc(len + 1);
    uint8_t* tfield = (uint8_t*)kmalloc(len + 1);
    if (!txt || !tlink || !tfield) { if (txt) kfree(txt); if (tlink) kfree(tlink); if (tfield) kfree(tfield); return; }
    __builtin_memset(tfield, 0, len + 1);
    uint32_t ti = 0;
    int last_space = 1;
    int cur_link = 0;                                         // link id in progress (0 = none)
    int cur_field = 0;                                        // field id in progress (<button> label text)
    int cur_form = -1;                                        // the <form> currently open (-1 = none)
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
            if (sel_streq(name, "form")) {                     // <form> / </form>
                uint32_t te = j; while (te < len && body[te] != '>') te++;
                if (close) { cur_field = 0; cur_form = -1; }
                else if (s->num_forms < SEL_MAX_FORMS) {
                    char act[192], meth[8], abs[192];
                    extract_attr(body, j, te, "action", act, sizeof(act));
                    extract_attr(body, j, te, "method", meth, sizeof(meth));
                    selene_resolve(s, act[0] ? act : s->base_path, abs);
                    if (!abs[0]) snprintf(abs, sizeof(abs), "%s://%s%s",         // empty action = this page
                        s->base_https ? "https" : "http", s->base_host, s->base_path);
                    strncpy(s->forms[s->num_forms].action, abs, 191); s->forms[s->num_forms].action[191] = '\0';
                    s->forms[s->num_forms].method = (meth[0]=='p'||meth[0]=='P') ? 1 : 0;   // POST=1 (not sent yet)
                    cur_form = s->num_forms; s->num_forms++;
                }
                ti = sel_ensure_nl(txt, tlink, ti, len, 1); last_space = 1;
                i = te; if (i < len) i++;
                continue;
            }
            if (!close && sel_streq(name, "input") && s->num_fields < SEL_MAX_FIELDS) {   // <input ...>
                uint32_t te = j; while (te < len && body[te] != '>') te++;
                char type[16], nm[64], val[160];
                extract_attr(body, j, te, "type",  type, sizeof(type));
                extract_attr(body, j, te, "name",  nm,   sizeof(nm));
                extract_attr(body, j, te, "value", val,  sizeof(val));
                for (int z = 0; type[z]; z++) if (type[z] >= 'A' && type[z] <= 'Z') type[z] += 32;
                int kind = SEL_FLD_TEXT;                        // default (text) if no/unknown type
                if (sel_streq(type, "submit")) kind = SEL_FLD_SUBMIT;
                else if (sel_streq(type, "hidden")) kind = SEL_FLD_HIDDEN;
                else if (sel_streq(type,"checkbox")||sel_streq(type,"radio")||sel_streq(type,"file")||
                         sel_streq(type,"image")||sel_streq(type,"button")||sel_streq(type,"reset")) kind = -1;
                if (kind >= 0) {
                    sel_field_t* f = &s->fields[s->num_fields];
                    f->form = cur_form; f->kind = (uint8_t)kind;
                    strncpy(f->name,  nm,  sizeof(f->name)-1);  f->name[sizeof(f->name)-1]  = '\0';
                    strncpy(f->value, val, sizeof(f->value)-1); f->value[sizeof(f->value)-1] = '\0';
                    int fid = s->num_fields + 1; s->num_fields++;
                    if (kind == SEL_FLD_TEXT) {
                        sel_emit(txt, tlink, tfield, &ti, len, " ", 0);
                        for (int d = 0; d < SEL_FIELD_W && ti < len; d++) { txt[ti]='_'; tlink[ti]=0; tfield[ti]=(uint8_t)fid; ti++; }
                        sel_emit(txt, tlink, tfield, &ti, len, " ", 0);
                        last_space = 1;
                    } else if (kind == SEL_FLD_SUBMIT) {
                        char lbl[44]; int b = 0; lbl[b++]='['; lbl[b++]=' ';
                        const char* t = val[0] ? val : "Submit";
                        for (int z = 0; t[z] && b < 40; z++) lbl[b++] = t[z];
                        lbl[b++]=' '; lbl[b++]=']'; lbl[b]='\0';
                        sel_emit(txt, tlink, tfield, &ti, len, " ", 0);
                        sel_emit(txt, tlink, tfield, &ti, len, lbl, fid);
                        sel_emit(txt, tlink, tfield, &ti, len, " ", 0);
                        last_space = 0;
                    }
                }
                i = te; if (i < len) i++;
                continue;
            }
            if (sel_streq(name, "button")) {                   // <button> ... </button> (submit-style)
                uint32_t te = j; while (te < len && body[te] != '>') te++;
                if (close) { if (cur_field) sel_emit(txt, tlink, tfield, &ti, len, " ]", cur_field); cur_field = 0; last_space = 0; }
                else if (s->num_fields < SEL_MAX_FIELDS) {
                    char type[16]; extract_attr(body, j, te, "type", type, sizeof(type));
                    for (int z = 0; type[z]; z++) if (type[z] >= 'A' && type[z] <= 'Z') type[z] += 32;
                    if (!sel_streq(type, "button") && !sel_streq(type, "reset")) {   // default is submit
                        sel_field_t* f = &s->fields[s->num_fields];
                        f->form = cur_form; f->kind = SEL_FLD_SUBMIT; f->name[0] = '\0'; f->value[0] = '\0';
                        cur_field = s->num_fields + 1; s->num_fields++;
                        sel_emit(txt, tlink, tfield, &ti, len, " [ ", cur_field);
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
                if (dec == ' ') { if (!last_space) { txt[ti] = ' '; tlink[ti] = (uint8_t)cur_link; tfield[ti] = (uint8_t)cur_field; ti++; last_space = 1; } }
                else { if (cur_hd && dec >= 'a' && dec <= 'z') dec -= 32; txt[ti] = dec; tlink[ti] = (uint8_t)cur_link; tfield[ti] = (uint8_t)cur_field; ti++; last_space = 0; }
                i += adv;
            } else { txt[ti] = '&'; tlink[ti] = (uint8_t)cur_link; tfield[ti] = (uint8_t)cur_field; ti++; last_space = 0; i++; }
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!last_space) { txt[ti] = ' '; tlink[ti] = (uint8_t)cur_link; tfield[ti] = (uint8_t)cur_field; ti++; last_space = 1; }
            i++;
            continue;
        }
        if (cur_hd && c >= 'a' && c <= 'z') c -= 32;          // upper-case h1/h2 text
        txt[ti] = c; tlink[ti] = (uint8_t)cur_link; tfield[ti] = (uint8_t)cur_field; ti++; last_space = 0; i++;
    }
    txt[ti] = '\0';
    wrap_text(s, txt, tlink, tfield, ti);
    kfree(txt); kfree(tlink); kfree(tfield);
}

// Parse http[://]host[:port][/path] into host/port/path (same shape as `httpget`).
static void parse_url(const char* url, char* host, uint16_t* port, char* path, int* is_https) {
    const char* p = url;
    while (*p == ' ') p++;
    *is_https = 0;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) { p += 8; *is_https = 1; }
    const char* hs = p;
    while (*p && *p != ':' && *p != '/') p++;
    int hl = (int)(p - hs); if (hl > 127) hl = 127;
    __builtin_memcpy(host, hs, hl); host[hl] = '\0';
    *port = *is_https ? 443 : 80;
    if (*p == ':') { p++; uint16_t v = 0; while (*p >= '0' && *p <= '9') { v = v*10 + (*p - '0'); p++; } *port = v ? v : *port; }
    if (*p == '/') { strncpy(path, p, 255); path[255] = '\0'; }
    else { path[0] = '/'; path[1] = '\0'; }
}

static int find_iface(void) {
    for (int i = 0; i < 8; i++)
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) return i;
    return -1;
}

// Fetch ctx->url with `method` (+ optional form `body` for POST) and render the reply. Blocks
// (the fetch drives the net) - we paint a status first via compositor_redraw_now for progress.
static void selene_load_ex(selene_ctx_t* s, const char* method, const uint8_t* body, uint32_t body_len) {
    s->num_lines = 0; s->scroll = 0; s->title[0] = '\0'; s->num_links = 0; s->sel_link = -1;
    s->find_active = 0; s->find_matches = 0; s->find_cur = 0;   // close find-in-page on navigation
    int iface = find_iface();
    if (iface < 0) { strncpy(s->status, "No network interface (boot with -nic)", 95); return; }
    if (net_interfaces[iface].ip == 0) {
        strncpy(s->status, "Getting an IP address (DHCP)...", 95);
        compositor_redraw_now();
        dhcp_request(iface);
    }
    char host[128] = {0}, path[256] = {0}; uint16_t port = 80; int is_https = 0;
    parse_url(s->url, host, &port, path, &is_https);
    if (!host[0]) { strncpy(s->status, "Enter a URL, e.g. example.com", 95); return; }
    int is_post = (method && (method[0] == 'P' || method[0] == 'p'));
    // record the base for resolving this page's relative links
    strncpy(s->base_host, host, sizeof(s->base_host)-1); s->base_host[sizeof(s->base_host)-1] = '\0';
    s->base_port = port;
    s->base_https = is_https;
    strncpy(s->base_path, path, sizeof(s->base_path)-1); s->base_path[sizeof(s->base_path)-1] = '\0';
    snprintf(s->status, sizeof(s->status), "%s %s%s ...", is_post ? "Submitting to" : "Loading",
             is_https ? "https://" : "", host);
    compositor_redraw_now();

    http_response_t resp;
    if (is_https) {
        // Secure fetch: a full TLS 1.2 handshake + encrypted request, then parsed like http.
        uint8_t* raw = (uint8_t*)kmalloc(HTTP_MAX_RESPONSE);
        if (!raw) { strncpy(s->status, "Out of memory", 95); return; }
        int rn = tls_https_request(host, path, method, body, body_len, iface, raw, HTTP_MAX_RESPONSE - 1, 0);
        if (rn < 0) { kfree(raw); snprintf(s->status, sizeof(s->status), "Could not load %s (TLS failed)", host); return; }
        raw[rn] = '\0';
        int pr = http_parse_response(raw, (uint32_t)rn, &resp);
        kfree(raw);
        if (pr < 0) { snprintf(s->status, sizeof(s->status), "Bad https response from %s", host); return; }
    } else {
        if (http_request(host, port, path, method, body, body_len, &resp, iface) < 0) {
            snprintf(s->status, sizeof(s->status), "Could not load %s", host);
            return;
        }
    }
    render_html(s, resp.body, resp.body_len);
    strncpy(s->cur_url, s->url, sizeof(s->cur_url)-1); s->cur_url[sizeof(s->cur_url)-1] = '\0';
    snprintf(s->status, sizeof(s->status), "%d %s  -  %s  -  %d links",
             resp.status_code, resp.status_text,
             s->title[0] ? s->title : host, s->num_links);
    http_free(&resp);
}

static void selene_load(selene_ctx_t* s) { selene_load_ex(s, "GET", 0, 0); }

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

// Percent-encode a form value for a URL query (space -> '+', unreserved kept, else %XX).
static void sel_urlencode(char* dst, uint32_t cap, const char* src) {
    static const char* hex = "0123456789ABCDEF";
    uint32_t o = 0;
    for (const char* p = src; *p && o + 3 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~') dst[o++] = (char)c;
        else if (c == ' ') dst[o++] = '+';
        else { dst[o++] = '%'; dst[o++] = hex[c>>4]; dst[o++] = hex[c&0xF]; }
    }
    dst[o] = '\0';
}

// Build the URL-encoded "name=value&..." data for the form `form` from its text/hidden fields
// (named controls only; submit buttons excluded). Returns the length written. Shared by the GET
// query and the POST body — so unit-testable without any navigation.
static uint32_t selene_build_query(selene_ctx_t* s, int form, char* query, uint32_t cap) {
    uint32_t q = 0; query[0] = '\0';
    for (int k = 0; k < s->num_fields; k++) {
        sel_field_t* f = &s->fields[k];
        if (f->form != form || f->kind == SEL_FLD_SUBMIT || !f->name[0]) continue;   // named data only
        char en[128], ev[256];
        sel_urlencode(en, sizeof(en), f->name);
        sel_urlencode(ev, sizeof(ev), f->value);
        uint32_t need = (uint32_t)strlen(en) + (uint32_t)strlen(ev) + 2;
        if (q + need >= cap) break;
        if (q) query[q++] = '&';
        for (const char* p = en; *p; p++) query[q++] = *p;
        query[q++] = '=';
        for (const char* p = ev; *p; p++) query[q++] = *p;
        query[q] = '\0';
    }
    return q;
}

// Build the GET submission URL for the form that owns field `fi`: "action?name=value&...". No
// navigation — so it's unit-testable. Empty out if `fi` is out of range.
static void selene_build_submit_url(selene_ctx_t* s, int fi, char* url, uint32_t cap) {
    url[0] = '\0';
    if (fi < 0 || fi >= s->num_fields) return;
    int form = s->fields[fi].form;
    const char* action = (form >= 0 && form < s->num_forms) ? s->forms[form].action : s->cur_url;
    char query[512]; selene_build_query(s, form, query, sizeof(query));
    const char* sep = "?";
    for (const char* p = action; *p; p++) if (*p == '?') { sep = "&"; break; }
    if (query[0]) snprintf(url, cap, "%s%s%s", action, sep, query);
    else          snprintf(url, cap, "%s", action);
}

// Submit the form owning field `fi`. GET forms navigate to "action?query"; POST forms send the
// query as an application/x-www-form-urlencoded request body to the action (over http or TLS).
static void selene_submit(selene_ctx_t* s, int fi) {
    if (fi < 0 || fi >= s->num_fields) return;
    int form = s->fields[fi].form;
    int is_post = (form >= 0 && form < s->num_forms && s->forms[form].method == 1);
    if (is_post) {
        const char* action = (form >= 0 && form < s->num_forms) ? s->forms[form].action : s->cur_url;
        char body[512]; uint32_t bl = selene_build_query(s, form, body, sizeof(body));
        push_hist(s, s->cur_url);
        selene_set_url(s, action);
        selene_load_ex(s, "POST", (const uint8_t*)body, bl);
    } else {
        char url[224];
        selene_build_submit_url(s, fi, url, sizeof(url));
        if (url[0]) selene_follow(s, url);
    }
}

void* selene_create_ctx(void) {
    selene_ctx_t* s = (selene_ctx_t*)kmalloc(sizeof(selene_ctx_t));
    if (!s) return NULL;
    __builtin_memset(s, 0, sizeof(*s));
    s->lines    = (char(*)[SEL_LINE_COLS])kmalloc(SEL_MAX_LINES * SEL_LINE_COLS);
    s->link_of  = (uint8_t(*)[SEL_LINE_COLS])kmalloc(SEL_MAX_LINES * SEL_LINE_COLS);
    s->field_of = (uint8_t(*)[SEL_LINE_COLS])kmalloc(SEL_MAX_LINES * SEL_LINE_COLS);
    s->links    = (sel_link_t*)kmalloc(SEL_MAX_LINKS * sizeof(sel_link_t));
    s->fields   = (sel_field_t*)kmalloc(SEL_MAX_FIELDS * sizeof(sel_field_t));
    s->forms    = (sel_form_t*)kmalloc(SEL_MAX_FORMS * sizeof(sel_form_t));
    if (!s->lines || !s->link_of || !s->field_of || !s->links || !s->fields || !s->forms) {
        if (s->lines) kfree(s->lines);
        if (s->link_of) kfree(s->link_of);
        if (s->field_of) kfree(s->field_of);
        if (s->links) kfree(s->links);
        if (s->fields) kfree(s->fields);
        if (s->forms) kfree(s->forms);
        kfree(s); return NULL;
    }
    s->sel_field = -1;
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

// First (line,col) where a link/field id appears, packed as line*SEL_LINE_COLS+col (or a large
// sentinel if absent). Lets Tab order focus by document position rather than by array index.
static int link_pos(selene_ctx_t* s, int lk) {
    for (int li = 0; li < s->num_lines; li++)
        for (int c = 0; c < SEL_LINE_COLS; c++)
            if (s->link_of[li][c] == lk) return li * SEL_LINE_COLS + c;
    return 1 << 28;
}
static int field_pos(selene_ctx_t* s, int fk) {
    for (int li = 0; li < s->num_lines; li++)
        for (int c = 0; c < SEL_LINE_COLS; c++)
            if (s->field_of[li][c] == fk) return li * SEL_LINE_COLS + c;
    return 1 << 28;
}

// Advance focus (Tab) in DOCUMENT ORDER: URL bar -> the earliest link/field on the page -> the
// next by position -> ... -> back to the URL bar. So a search box reachable a few Tabs in, not
// after every link. Hidden fields are skipped; the focused item is scrolled into view.
static void select_link(selene_ctx_t* s, int dir) {
    (void)dir;                                          // forward only (Tab), for now
    int cur = -1;                                       // the URL bar precedes all page content
    if (s->sel_field >= 0)     cur = field_pos(s, s->sel_field + 1);
    else if (s->sel_link >= 0) cur = link_pos(s, s->sel_link + 1);

    int best = 1 << 30, best_field = 0, best_idx = -1;  // the focusable with the least pos > cur
    for (int i = 0; i < s->num_links; i++) {
        int p = link_pos(s, i + 1); if (p > cur && p < best) { best = p; best_field = 0; best_idx = i; }
    }
    for (int i = 0; i < s->num_fields; i++) {
        if (s->fields[i].kind == SEL_FLD_HIDDEN) continue;
        int p = field_pos(s, i + 1); if (p > cur && p < best) { best = p; best_field = 1; best_idx = i; }
    }
    if (best_idx < 0) { s->sel_link = -1; s->sel_field = -1; return; }   // past the last -> URL bar
    if (best_field) { s->sel_field = best_idx; s->sel_link = -1; }
    else            { s->sel_link = best_idx; s->sel_field = -1; }
    int line = best / SEL_LINE_COLS, rows = visible_rows();
    if (line < s->scroll) s->scroll = line;
    else if (line >= s->scroll + rows) s->scroll = line - rows + 1;
    clamp_scroll(s);
}

// ---- find-in-page (Ctrl+F) ----------------------------------------------------------

// Case-insensitive: does row `line` contain the query `q` (length ql) starting at column c?
static int find_at(const char* line, int c, const char* q, int ql) {
    for (int k = 0; k < ql; k++) {
        char a = line[c + k], b = q[k];
        if (a == '\0') return 0;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

// Count non-overlapping matches of ctx->find_q across all lines; if wline != NULL, store the
// (line, col) of the `want`-th match (0-based). Returns the total match count.
static int find_scan(selene_ctx_t* s, int want, int* wline, int* wcol) {
    int ql = s->find_len;
    if (ql <= 0) return 0;
    int count = 0;
    for (int li = 0; li < s->num_lines; li++) {
        const char* line = s->lines[li];
        int llen = (int)strlen(line);
        for (int c = 0; c + ql <= llen; c++) {
            if (find_at(line, c, s->find_q, ql)) {
                if (count == want && wline) { *wline = li; *wcol = c; }
                count++;
                c += ql - 1;                          // non-overlapping
            }
        }
    }
    return count;
}

// Scroll the current match into view (centred-ish).
static void find_scroll_to_cur(selene_ctx_t* s) {
    int wl = 0, wc = 0;
    if (s->find_matches > 0 && find_scan(s, s->find_cur, &wl, &wc)) {
        int rows = visible_rows();
        if (wl < s->scroll || wl >= s->scroll + rows) { s->scroll = wl - rows / 2; clamp_scroll(s); }
    }
}
// Recompute the match count after the query changed; reset to the first match.
static void find_recount(selene_ctx_t* s) {
    s->find_matches = find_scan(s, -1, 0, 0);
    if (s->find_cur >= s->find_matches) s->find_cur = 0;
    find_scroll_to_cur(s);
}
// Advance to the next match (wraps).
static void find_next(selene_ctx_t* s) {
    if (s->find_matches <= 0) { find_recount(s); return; }
    s->find_cur = (s->find_cur + 1) % s->find_matches;
    find_scroll_to_cur(s);
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
    int find_wl = -1, find_wc = -1;                          // the current find match's (line,col)
    if (s->find_active && s->find_len > 0 && s->find_matches > 0) find_scan(s, s->find_cur, &find_wl, &find_wc);
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

        // overlay form-field runs on this line: editable text boxes and submit buttons
        c0 = 0;
        while (c0 < llen) {
            uint8_t fk = s->field_of[idx][c0];
            int c1f = c0; while (c1f < llen && s->field_of[idx][c1f] == fk) c1f++;
            if (fk != 0 && fk - 1 < s->num_fields) {
                sel_field_t* f = &s->fields[fk - 1];
                int px = cx + SEL_PAD + c0 * FONT_WIDTH;
                int wpx = (c1f - c0) * FONT_WIDTH;
                int focused = (s->sel_field == fk - 1);
                if (f->kind == SEL_FLD_TEXT) {
                    uint32_t box = focused ? fb_rgb(255, 255, 255) : fb_rgb(232, 234, 244);
                    uint32_t brd = focused ? fb_rgb(120, 90, 210) : fb_rgb(96, 104, 130);
                    fb_fill_rect(px, py - 1, wpx, FONT_HEIGHT + 2, box);
                    fb_fill_rect(px, py - 1, wpx, 1, brd); fb_fill_rect(px, py + FONT_HEIGHT, wpx, 1, brd);
                    fb_fill_rect(px, py - 1, 1, FONT_HEIGHT + 2, brd); fb_fill_rect(px + wpx - 1, py - 1, 1, FONT_HEIGHT + 2, brd);
                    int maxc = (wpx - 6) / FONT_WIDTH; if (maxc < 0) maxc = 0;
                    int vlen = (int)strlen(f->value);
                    int start = (vlen > maxc) ? vlen - maxc : 0;         // scroll to show the tail
                    char vis[SEL_LINE_COLS]; int vl = 0;
                    for (int z = start; f->value[z] && vl < SEL_LINE_COLS - 1; z++) vis[vl++] = f->value[z];
                    vis[vl] = '\0';
                    font_draw_string(px + 3, py, vis, fb_rgb(20, 20, 30), box);
                    if (focused) { int caret = px + 3 + vl * FONT_WIDTH;
                        if (caret < px + wpx - 2) fb_fill_rect(caret, py, 1, FONT_HEIGHT, fb_rgb(120, 90, 210)); }
                } else if (f->kind == SEL_FLD_SUBMIT) {
                    uint32_t bg = focused ? fb_rgb(120, 90, 210) : fb_rgb(90, 80, 130);
                    fb_fill_rect(px, py - 1, wpx, FONT_HEIGHT + 2, bg);
                    char sub[SEL_LINE_COLS]; int k = 0;
                    for (; k < c1f - c0 && k < SEL_LINE_COLS - 1; k++) sub[k] = s->lines[idx][c0 + k];
                    sub[k] = '\0';
                    font_draw_string(px, py, sub, fb_rgb(240, 240, 250), bg);
                }
            }
            c0 = c1f;
        }

        // find-in-page: highlight every match on this line, the current one accented orange
        if (s->find_active && s->find_len > 0) {
            int flen = (int)strlen(s->lines[idx]);
            for (int c = 0; c + s->find_len <= flen; c++) {
                if (find_at(s->lines[idx], c, s->find_q, s->find_len)) {
                    int px = cx + SEL_PAD + c * FONT_WIDTH, wpx = s->find_len * FONT_WIDTH;
                    int is_cur = (idx == find_wl && c == find_wc);
                    uint32_t bg = is_cur ? fb_rgb(255, 158, 40) : fb_rgb(250, 236, 130);
                    fb_fill_rect(px, py - 1, wpx, FONT_HEIGHT + 2, bg);
                    char sub[SEL_LINE_COLS]; int k = 0;
                    for (; k < s->find_len && k < SEL_LINE_COLS - 1; k++) sub[k] = s->lines[idx][c + k];
                    sub[k] = '\0';
                    font_draw_string(px, py, sub, fb_rgb(30, 25, 10), bg);
                    c += s->find_len - 1;
                }
            }
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

    // status: the find bar (when active) else the selected link's target else the page status
    int sy = cy + SELENE_H - SEL_STATUS;
    if (s->find_active) {
        fb_fill_rect(cx, sy, SELENE_W, SEL_STATUS, fb_rgb(58, 48, 78));
        char fbuf[128];
        snprintf(fbuf, sizeof(fbuf), "Find: %s_   [%d/%d]   Enter=next  Esc=close",
                 s->find_q, s->find_matches ? s->find_cur + 1 : 0, s->find_matches);
        font_draw_string(cx + 6, sy + 3, fbuf, fb_rgb(255, 238, 180), fb_rgb(58, 48, 78));
    } else {
        fb_fill_rect(cx, sy, SELENE_W, SEL_STATUS, fb_rgb(32, 30, 44));
        const char* st = (s->sel_link >= 0) ? s->links[s->sel_link].url : s->status;
        font_draw_string(cx + 6, sy + 3, st, fb_rgb(200, 200, 220), fb_rgb(32, 30, 44));
    }
}

void selene_win_key(window_t* win, int key) {
    selene_ctx_t* s = (selene_ctx_t*)win->reserved;
    if (!s) return;
    int rows = visible_rows();

    if (key == 0x06) { s->find_active = 1; find_recount(s); return; }   // Ctrl+F: open/refresh find
    if (s->find_active) {                                    // the find bar captures keystrokes
        if (key == 0x1B) { s->find_active = 0; return; }                 // Esc: close find
        if (key == '\n' || key == '\r') { find_next(s); return; }        // Enter: next match
        if (key == '\b' || key == 0x7F) {                                // Backspace: edit query
            if (s->find_len > 0) s->find_q[--s->find_len] = '\0';
            find_recount(s); return;
        }
        if (key >= 0x20 && key < 0x7F) {                                 // type into the query
            if (s->find_len < (int)sizeof(s->find_q) - 1) { s->find_q[s->find_len++] = (char)key; s->find_q[s->find_len] = '\0'; }
            find_recount(s); return;
        }
        // arrows / PgUp / PgDn fall through so the page still scrolls while finding
    }

    if (key == '\t') { select_link(s, +1); return; }         // Tab: next link/field (wraps to URL bar)
    if (key == 0x1B) { s->sel_link = -1; s->sel_field = -1; return; }   // Esc: back to the URL bar

    int in_text = (s->sel_field >= 0 && s->sel_field < s->num_fields &&
                   s->fields[s->sel_field].kind == SEL_FLD_TEXT);

    if (key == '\n' || key == '\r') {
        if (s->sel_field >= 0 && s->sel_field < s->num_fields) selene_submit(s, s->sel_field);  // submit the form
        else if (s->sel_link >= 0 && s->sel_link < s->num_links) selene_follow(s, s->links[s->sel_link].url);
        else selene_go(s);
        clamp_scroll(s);
        return;
    }
    if (key == '\b' || key == 0x7F) {
        if (in_text) {                                       // editing a text field: delete a char
            char* v = s->fields[s->sel_field].value; int vl = (int)strlen(v);
            if (vl > 0) v[vl - 1] = '\0';
        }
        // else: Back when a link is selected or the URL bar is untouched (still the current
        // page); once you start editing the URL, Backspace deletes a character instead.
        else if (s->sel_link >= 0 || strcmp(s->url, s->cur_url) == 0) selene_back(s);
        else if (s->url_len > 0) s->url[--s->url_len] = '\0';
        return;
    }

    if (key == KEY_UP || key == KEY_WHEEL_UP)     { s->scroll -= 3; clamp_scroll(s); return; }
    if (key == KEY_DOWN || key == KEY_WHEEL_DOWN) { s->scroll += 3; clamp_scroll(s); return; }
    if (key == KEY_PGUP) { s->scroll -= (rows - 1); clamp_scroll(s); return; }
    if (key == KEY_PGDN) { s->scroll += (rows - 1); clamp_scroll(s); return; }
    if (key == KEY_HOME) { s->scroll = 0; return; }
    if (key == KEY_END)  { s->scroll = s->num_lines; clamp_scroll(s); return; }

    if (key >= 0x20 && key < 0x7F) {                          // printable character
        if (in_text) {                                        // type into the focused text field
            char* v = s->fields[s->sel_field].value;
            int vl = (int)strlen(v);
            if (vl < (int)sizeof(s->fields[s->sel_field].value) - 1) { v[vl] = (char)key; v[vl + 1] = '\0'; }
        } else {                                              // otherwise edit the URL bar
            s->sel_link = -1; s->sel_field = -1;
            if (s->url_len < (int)sizeof(s->url) - 1) {
                s->url[s->url_len++] = (char)key;
                s->url[s->url_len] = '\0';
            }
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
    if (lk != 0 && lk - 1 < s->num_links) { s->sel_link = lk - 1; s->sel_field = -1; selene_follow(s, s->links[lk-1].url); return; }
    uint8_t fk = s->field_of[idx][col];                      // a form control?
    if (fk != 0 && fk - 1 < s->num_fields) {
        int fi = fk - 1;
        if (s->fields[fi].kind == SEL_FLD_SUBMIT) selene_submit(s, fi);
        else if (s->fields[fi].kind == SEL_FLD_TEXT) { s->sel_field = fi; s->sel_link = -1; }
    }
}

// ---- HTML-forms known-answer self-test (`formtest`) --------------------------------------
// Parse a known form, then build its GET submission URL. Pure logic — no network, no window.
int selene_form_selftest(void) {
    int pass = 0, total = 0;
    selene_ctx_t* s = (selene_ctx_t*)selene_create_ctx();
    if (!s) { printf("selene-form: context alloc failed\n"); return -1; }
    strncpy(s->base_host, "example.com", sizeof(s->base_host)-1); s->base_host[sizeof(s->base_host)-1] = '\0';
    s->base_https = 0; s->base_port = 80;
    strncpy(s->base_path, "/", sizeof(s->base_path)-1); s->base_path[sizeof(s->base_path)-1] = '\0';
    strncpy(s->cur_url, "http://example.com/", sizeof(s->cur_url)-1); s->cur_url[sizeof(s->cur_url)-1] = '\0';

    static const char* HTML =
        "<h1>Search</h1>"
        "<form action=\"/search\" method=\"get\">"
        "<input type=\"text\" name=\"q\" value=\"\">"
        "<input type=\"hidden\" name=\"lang\" value=\"en\">"
        "<input type=\"submit\" value=\"Search\">"
        "</form>";
    render_html(s, (const uint8_t*)HTML, (uint32_t)strlen(HTML));

    total++;
    if (s->num_forms == 1 && s->forms[0].method == 0 && sel_streq(s->forms[0].action, "http://example.com/search"))
        { pass++; printf("selene-form: form parsed (GET action=%s) PASS\n", s->forms[0].action); }
    else printf("selene-form: form parse FAIL (nf=%d)\n", s->num_forms);

    total++;
    if (s->num_fields == 3 &&
        s->fields[0].kind == SEL_FLD_TEXT   && sel_streq(s->fields[0].name, "q") &&
        s->fields[1].kind == SEL_FLD_HIDDEN && sel_streq(s->fields[1].name, "lang") && sel_streq(s->fields[1].value, "en") &&
        s->fields[2].kind == SEL_FLD_SUBMIT)
        { pass++; printf("selene-form: fields parsed (text q, hidden lang=en, submit) PASS\n"); }
    else printf("selene-form: field parse FAIL (nf=%d)\n", s->num_fields);

    total++;
    strncpy(s->fields[0].value, "hello world", sizeof(s->fields[0].value)-1);   // fill the text field
    char url[224]; selene_build_submit_url(s, 2, url, sizeof(url));             // submit via the button
    if (sel_streq(url, "http://example.com/search?q=hello+world&lang=en"))
        { pass++; printf("selene-form: GET url built PASS (%s)\n", url); }
    else printf("selene-form: GET url build FAIL (%s)\n", url);

    // 4) a method=post form: parse it and build the URL-encoded request body.
    total++;
    {
        static const char* PHTML =
            "<form action=\"/login\" method=\"post\">"
            "<input type=\"text\" name=\"user\" value=\"\">"
            "<input type=\"password\" name=\"pass\" value=\"\">"
            "<input type=\"submit\" value=\"Sign in\"></form>";
        render_html(s, (const uint8_t*)PHTML, (uint32_t)strlen(PHTML));
        int okp = (s->num_forms == 1 && s->forms[0].method == 1 && s->num_fields == 3);   // method POST
        if (okp) {
            strncpy(s->fields[0].value, "alice",  sizeof(s->fields[0].value)-1);
            strncpy(s->fields[1].value, "p@ss w", sizeof(s->fields[1].value)-1);          // @ and space encode
        }
        char body[256]; selene_build_query(s, 0, body, sizeof(body));
        if (okp && sel_streq(body, "user=alice&pass=p%40ss+w"))
            { pass++; printf("selene-form: POST body built PASS (%s)\n", body); }
        else printf("selene-form: POST body FAIL (method=%d body=%s)\n",
                    s->num_forms ? s->forms[0].method : -1, body);
    }

    kfree(s->lines); kfree(s->link_of); kfree(s->field_of);
    kfree(s->links); kfree(s->fields); kfree(s->forms); kfree(s);
    printf("selene-form: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
