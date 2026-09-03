// ============================================================
// json.c - strict RFC 8259 JSON validator (iterative) + KAT
// ============================================================
// Validates untrusted JSON with O(1) C-stack use: an explicit `is_obj` stack replaces recursion,
// so deep nesting can't exhaust NyxOS's 4 KB kernel task stack (nesting beyond J_MAXDEPTH is
// rejected). Accept/reject is host-verified byte-for-byte against Python json across ~4000 random
// documents + mutation/junk fuzzing before landing (see scratchpad/json_proto.c).
#include "json.h"

#define J_MAXDEPTH 256   // nesting cap (hardening): a fixed local array, not call-stack depth

static int jhex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int jdig(char c) { return c >= '0' && c <= '9'; }

// Scalar scanners: advance *pp past a valid token and return 0, else return -1 (pos at fault).
static int jstring(const char* s, int* pp) {          // s[*pp] == '"'
    int p = *pp + 1;
    for (;;) {
        unsigned char c = (unsigned char)s[p];
        if (c == '"') { *pp = p + 1; return 0; }
        if (c == '\0') { *pp = p; return -1; }
        if (c < 0x20) { *pp = p; return -1; }          // unescaped control char
        if (c == '\\') {
            char e = s[p + 1];
            if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' ||
                e == 'n' || e == 'r' || e == 't') { p += 2; }
            else if (e == 'u') { p += 2; for (int k = 0; k < 4; k++) { if (!jhex(s[p])) { *pp = p; return -1; } p++; } }
            else { *pp = p; return -1; }
        } else { p++; }
    }
}
static int jnumber(const char* s, int* pp) {
    int p = *pp;
    if (s[p] == '-') p++;
    if (s[p] == '0') p++;
    else if (s[p] >= '1' && s[p] <= '9') { while (jdig(s[p])) p++; }
    else { *pp = p; return -1; }
    if (s[p] == '.') { p++; if (!jdig(s[p])) { *pp = p; return -1; } while (jdig(s[p])) p++; }
    if (s[p] == 'e' || s[p] == 'E') {
        p++;
        if (s[p] == '+' || s[p] == '-') p++;
        if (!jdig(s[p])) { *pp = p; return -1; }
        while (jdig(s[p])) p++;
    }
    *pp = p; return 0;
}
static int jlit(const char* s, int* pp, const char* lit) {
    int p = *pp;
    for (int k = 0; lit[k]; k++) { if (s[p] != lit[k]) { *pp = p; return -1; } p++; }
    *pp = p; return 0;
}

enum { READ_VALUE, ARR_FIRST, OBJ_FIRST, OBJ_KEY, OBJ_COLON, AFTER_ARR, AFTER_OBJ, TOP_END };

int json_validate(const char* s, int* errpos) {
    uint8_t is_obj[J_MAXDEPTH];       // per open container: 1=object, 0=array
    int sp = 0, pos = 0, state = READ_VALUE;
    #define AFTER() (sp == 0 ? TOP_END : (is_obj[sp - 1] ? AFTER_OBJ : AFTER_ARR))
    for (;;) {
        char c;
        while ((c = s[pos]) == ' ' || c == '\t' || c == '\n' || c == '\r') pos++;
        if (state == ARR_FIRST) {
            if (c == ']') { pos++; sp--; state = AFTER(); continue; }
            state = READ_VALUE;                                   // fall through: read the first element
        } else if (state == OBJ_FIRST) {
            if (c == '}') { pos++; sp--; state = AFTER(); continue; }
            state = OBJ_KEY;                                      // fall through: read the first key
        }
        if (state == READ_VALUE) {
            if (c == '{') { if (sp >= J_MAXDEPTH) goto fail; is_obj[sp++] = 1; pos++; state = OBJ_FIRST; continue; }
            if (c == '[') { if (sp >= J_MAXDEPTH) goto fail; is_obj[sp++] = 0; pos++; state = ARR_FIRST; continue; }
            int r;
            if (c == '"') r = jstring(s, &pos);
            else if (c == '-' || jdig(c)) r = jnumber(s, &pos);
            else if (c == 't') r = jlit(s, &pos, "true");
            else if (c == 'f') r = jlit(s, &pos, "false");
            else if (c == 'n') r = jlit(s, &pos, "null");
            else goto fail;
            if (r != 0) goto fail;
            state = AFTER(); continue;
        }
        if (state == OBJ_KEY) {
            if (c != '"' || jstring(s, &pos) != 0) goto fail;
            state = OBJ_COLON; continue;
        }
        if (state == OBJ_COLON) {
            if (c != ':') goto fail;
            pos++; state = READ_VALUE; continue;
        }
        if (state == AFTER_ARR) {
            if (c == ',') { pos++; state = READ_VALUE; continue; }
            if (c == ']') { pos++; sp--; state = AFTER(); continue; }
            goto fail;
        }
        if (state == AFTER_OBJ) {
            if (c == ',') { pos++; state = OBJ_KEY; continue; }
            if (c == '}') { pos++; sp--; state = AFTER(); continue; }
            goto fail;
        }
        // TOP_END
        if (c == '\0') return 0;
        goto fail;
    }
    #undef AFTER
fail:
    if (errpos) *errpos = pos;
    return -1;
}

// ---- json_query: jq-lite path extractor (iterative, O(1) C-stack) ---------------------------
static int jws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Skip exactly one JSON value at s[*pp] (after leading ws). Container nesting is tracked by an
// integer counter (not recursion), and strings are skipped whole so braces inside them don't
// miscount — so this is O(1) C-stack even for deeply nested input. 0 = ok, -1 = malformed.
static int jskip_value(const char* s, int* pp) {
    int p = *pp;
    while (jws(s[p])) p++;
    char c = s[p];
    if (c == '"') { *pp = p; return jstring(s, pp); }
    if (c == '-' || jdig(c)) { *pp = p; return jnumber(s, pp); }
    if (c == 't') { *pp = p; return jlit(s, pp, "true"); }
    if (c == 'f') { *pp = p; return jlit(s, pp, "false"); }
    if (c == 'n') { *pp = p; return jlit(s, pp, "null"); }
    if (c == '{' || c == '[') {
        int depth = 0;
        for (;;) {
            c = s[p];
            if (c == '\0') { *pp = p; return -1; }
            if (c == '"') { if (jstring(s, &p) != 0) { *pp = p; return -1; } continue; }
            if (c == '{' || c == '[') { depth++; p++; continue; }
            if (c == '}' || c == ']') { depth--; p++; if (depth == 0) { *pp = p; return 0; } continue; }
            p++;
        }
    }
    *pp = p; return -1;
}

static int key_eq(const char* s, int kstart, int kend, const char* n, int nlen) {
    if (kend - kstart != nlen) return 0;
    for (int i = 0; i < nlen; i++) if (s[kstart + i] != n[i]) return 0;
    return 1;
}

int json_query(const char* s, const char* path, int* out_start, int* out_len) {
    int vp = 0;
    while (jws(s[vp])) vp++;                      // vp = start of the value in focus
    const char* q = path;
    while (*q) {
        if (*q == '[') {                          // array-index step [N]
            q++;
            if (!jdig(*q)) return JQ_EPATH;
            int idx = 0;
            while (jdig(*q)) { idx = idx * 10 + (*q - '0'); if (idx > 1000000000) return JQ_EPATH; q++; }
            if (*q != ']') return JQ_EPATH;
            q++;
            int p = vp; while (jws(s[p])) p++;
            if (s[p] != '[') return JQ_ETYPE;
            p++; while (jws(s[p])) p++;
            if (s[p] == ']') return JQ_ENOTFOUND;
            int i = 0;
            for (;;) {
                while (jws(s[p])) p++;
                int estart = p;
                if (i == idx) { vp = estart; break; }
                int eend = estart;
                if (jskip_value(s, &eend) != 0) return JQ_EMALFORMED;
                while (jws(s[eend])) eend++;
                if (s[eend] == ',') { p = eend + 1; i++; continue; }
                if (s[eend] == ']') return JQ_ENOTFOUND;
                return JQ_EMALFORMED;
            }
            continue;
        }
        if (*q == '.') {                          // object-member step .name
            q++;
            const char* nstart = q;
            while (*q && *q != '.' && *q != '[') q++;
            int nlen = (int)(q - nstart);
            if (nlen == 0) {
                if (*q == '.') return JQ_EPATH;   // ".."
                continue;                          // identity: leading '.', or '.' before '['/end
            }
            int p = vp; while (jws(s[p])) p++;
            if (s[p] != '{') return JQ_ETYPE;
            p++; while (jws(s[p])) p++;
            if (s[p] == '}') return JQ_ENOTFOUND;
            for (;;) {
                while (jws(s[p])) p++;
                if (s[p] != '"') return JQ_EMALFORMED;
                int kstart = p + 1;
                int ke = p;
                if (jstring(s, &ke) != 0) return JQ_EMALFORMED;
                int kend = ke - 1;                 // closing-quote index
                while (jws(s[ke])) ke++;
                if (s[ke] != ':') return JQ_EMALFORMED;
                ke++; while (jws(s[ke])) ke++;
                int vstart = ke;
                if (key_eq(s, kstart, kend, nstart, nlen)) { vp = vstart; break; }
                int vend = vstart;
                if (jskip_value(s, &vend) != 0) return JQ_EMALFORMED;
                while (jws(s[vend])) vend++;
                if (s[vend] == ',') { p = vend + 1; continue; }
                if (s[vend] == '}') return JQ_ENOTFOUND;
                return JQ_EMALFORMED;
            }
            continue;
        }
        return JQ_EPATH;
    }
    while (jws(s[vp])) vp++;
    int vend = vp;
    if (jskip_value(s, &vend) != 0) return JQ_EMALFORMED;
    *out_start = vp; *out_len = vend - vp;
    return 0;
}

int json_selftest(void) {
    static const char* valid[] = {
        "{}", "[]", "0", "-0", "123", "1.5", "-1.5e+10", "1E10", "true", "false", "null", "\"\"",
        "\"a\\u00e9b\"", "{\"a\":1,\"b\":[1,2,3],\"c\":{\"d\":null}}", "  [ 1 , 2 ,\n3 ]  ",
        "[[[[[]]]]]", "[null,true,false,\"x\",1.0]", 0
    };
    static const char* invalid[] = {
        "", "   ", "{", "01", "1.", ".5", "+1", "-", "{\"a\":}", "{\"a\":1,}", "[1,]",
        "{a:1}", "'x'", "nul", "[1 2]", "1 2", "\"a", "\"\\x\"", "{\"a\":1}x", "[1,2,,3]",
        "NaN", "Infinity", 0
    };
    for (int i = 0; valid[i]; i++)   if (json_validate(valid[i], 0) != 0) return 10 + i;
    for (int i = 0; invalid[i]; i++) if (json_validate(invalid[i], 0) == 0) return 40 + i;
    if (json_validate("\"a\x01" "b\"", 0) == 0) return 90;          // control char in a string
    static char deep[601];                                          // 300 nested > 256 cap
    for (int i = 0; i < 300; i++) { deep[i] = '['; deep[300 + i] = ']'; }
    deep[600] = '\0';
    if (json_validate(deep, 0) == 0) return 91;                     // must reject (depth cap)
    return 0;
}

int json_query_selftest(void) {
    static const char* D =
        "{\"name\":\"nyx\",\"ver\":6,\"tags\":[\"os\",\"kernel\",42],"
        "\"meta\":{\"lts\":false,\"nested\":{\"deep\":[0,{\"x\":true}]}},"
        "\"empty\":{},\"arr\":[]}";
    static const struct { const char* path; const char* want; } pos[] = {
        {".name", "\"nyx\""}, {".ver", "6"}, {".tags[0]", "\"os\""}, {".tags[2]", "42"},
        {".meta.lts", "false"}, {".meta.nested.deep[1].x", "true"},
        {".meta.nested.deep", "[0,{\"x\":true}]"}, {".tags", "[\"os\",\"kernel\",42]"}, {0, 0}
    };
    for (int i = 0; pos[i].path; i++) {
        int st, ln;
        if (json_query(D, pos[i].path, &st, &ln) != 0) return 10 + i;
        int wl = 0; while (pos[i].want[wl]) wl++;
        if (ln != wl) return 30 + i;
        for (int k = 0; k < ln; k++) if (D[st + k] != pos[i].want[k]) return 50 + i;
    }
    { int st, ln;                                          // "." selects the whole document
      int dl = 0; while (D[dl]) dl++;
      if (json_query(D, ".", &st, &ln) != 0 || st != 0 || ln != dl) return 69; }
    static const struct { const char* path; int code; } neg[] = {
        {".missing", JQ_ENOTFOUND}, {".tags[9]", JQ_ENOTFOUND}, {".name.x", JQ_ETYPE},
        {".ver[0]", JQ_ETYPE}, {".empty.x", JQ_ENOTFOUND}, {".arr[0]", JQ_ENOTFOUND},
        {"..", JQ_EPATH}, {".tags[", JQ_EPATH}, {0, 0}
    };
    for (int i = 0; neg[i].path; i++) {
        int st, ln;
        if (json_query(D, neg[i].path, &st, &ln) != neg[i].code) return 70 + i;
    }
    return 0;
}

// ---- json_format: pretty-print a VALIDATED JSON string (2-space indent) -----------------
// Re-indents well-formed JSON — call json_validate() first, this assumes valid input and
// does no error checking. Strings are copied verbatim (respecting \" and \\ escapes so a
// quote inside a string never ends it); `{`/`[` open a block (an empty {}/[]] stays inline),
// `}`/`]` close one, `,` breaks to a new line, `:` becomes ": ". Existing whitespace outside
// strings is dropped and rebuilt. Emits char-by-char via emit(c, ctx) so the caller can print
// to stdout or collect into a buffer; a trailing newline is emitted.
void json_format(const char* s, int len, void (*emit)(char, void*), void* ctx) {
    int depth = 0;
    for (int i = 0; i < len && s[i]; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;   // drop existing ws
        if (c == '"') {                                                  // string: copy verbatim
            emit(c, ctx);
            for (i++; i < len && s[i]; i++) {
                emit(s[i], ctx);
                if (s[i] == '\\') { if (i + 1 < len) { emit(s[i + 1], ctx); i++; } }
                else if (s[i] == '"') break;
            }
            continue;
        }
        if (c == '{' || c == '[') {
            char close = (c == '{') ? '}' : ']';
            int j = i + 1;                                               // peek: empty container?
            while (j < len && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r')) j++;
            if (j < len && s[j] == close) { emit(c, ctx); emit(close, ctx); i = j; continue; }
            emit(c, ctx); emit('\n', ctx); depth++;
            for (int k = 0; k < depth * 2; k++) emit(' ', ctx);
            continue;
        }
        if (c == '}' || c == ']') {
            emit('\n', ctx); depth--;
            for (int k = 0; k < depth * 2; k++) emit(' ', ctx);
            emit(c, ctx);
            continue;
        }
        if (c == ',') {
            emit(c, ctx); emit('\n', ctx);
            for (int k = 0; k < depth * 2; k++) emit(' ', ctx);
            continue;
        }
        if (c == ':') { emit(':', ctx); emit(' ', ctx); continue; }
        emit(c, ctx);                                                    // scalar char
    }
    emit('\n', ctx);
}

// KAT: json_format on a minified doc (nested array/object, empty {}/[], a string with an
// escaped quote) -> the exact 2-space-indented text. 0 = pass, else the failing case.
typedef struct { char* buf; int n; int cap; } jfmt_sink;
static void jfmt_collect(char c, void* ctx) {
    jfmt_sink* k = (jfmt_sink*)ctx;
    if (k->n < k->cap - 1) k->buf[k->n++] = c;
}
int json_fmt_selftest(void) {
    static const char* IN   = "{\"a\":1,\"b\":[2,3],\"c\":{},\"d\":[],\"e\":\"x\\\"y\"}";
    static const char* WANT =
        "{\n  \"a\": 1,\n  \"b\": [\n    2,\n    3\n  ],\n  \"c\": {},\n  \"d\": [],\n  \"e\": \"x\\\"y\"\n}\n";
    char out[128]; jfmt_sink k; k.buf = out; k.n = 0; k.cap = (int)sizeof(out);
    int inlen = 0; while (IN[inlen]) inlen++;
    json_format(IN, inlen, jfmt_collect, &k);
    out[k.n] = '\0';
    int wl = 0; while (WANT[wl]) wl++;
    if (k.n != wl) return 1;
    for (int i = 0; i < wl; i++) if (out[i] != WANT[i]) return 2;
    return 0;
}
