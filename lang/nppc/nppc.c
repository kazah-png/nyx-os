/* nppc — the N++ front-end compiler (M6.2 skeleton).
 *
 * N++ is N's application-layer superset (lang/docs/design-npp.md). Per the
 * section-6 shape decision, nppc lowers .npp source to N SOURCE, and the
 * verified N pipeline — ncc, or the self-hosted toolbox — carries it the
 * rest of the way to C and the OS. The lowered .n is checked by ncc's own
 * type/ownership/capability checker, a soundness net under this front-end.
 *
 * THIS RUNG (M6.2) is the honest skeleton: the accepted dialect is exactly
 * the N subset, and lowering is the identity — the source is written out
 * byte-for-byte. What makes the rung real is the READ side: the whole file
 * is lexed with a faithful copy of ncc's lexer (same tokens, same escapes,
 * same interpolation mode stack, same attributes, same diagnostics), so
 * every .npp file nppc accepts is one the N pipeline will lex identically.
 * Each M6.3+ rung (generics, closures, modules) replaces a slice of the
 * identity with a real transform.
 *
 * Build (hosted, any C99 compiler):  gcc -O2 -Wall -Wextra -o nppc nppc.c
 * Use:                               nppc file.npp -o file.n
 */

#ifdef __TINYC__
#include "libc.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#endif

/* ------------------------------------------------------------------ */
/* utils                                                              */
/* ------------------------------------------------------------------ */
static void die(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
static void* xmalloc(size_t n) {
    void* p = calloc(1, n ? n : 1);
    if (!p) die("nppc: out of memory");
    return p;
}
static char* xstrndup(const char* s, size_t n) {
    char* p = xmalloc(n + 1);
    memcpy(p, s, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* lexer — a faithful copy of ncc's (lang/ncc/ncc.c). The N++ dialect */
/* adds no tokens at this rung; when M6.3+ syntax lands, it lands     */
/* HERE first and in the lowering, never in ncc.                      */
/* ------------------------------------------------------------------ */
typedef enum {
    T_EOF, T_INT, T_STR, T_STR_HEAD, T_STR_MID, T_STR_TAIL, T_INTERP_R,
    T_IDENT,
    T_KW_EXTERN, T_KW_SYSCALL, T_KW_FN, T_KW_MUT, T_KW_RETURN,
    T_KW_IF, T_KW_ELSE, T_KW_WHILE, T_KW_BREAK, T_KW_CONTINUE,
    T_KW_AS, T_KW_RAW, T_KW_TRUE, T_KW_FALSE, T_KW_STRUCT, T_KW_DEFER,
    T_KW_ENUM, T_KW_MATCH, T_FATARROW, T_KW_IMPL,
    T_KW_FOR, T_KW_IN, T_DOTDOT, T_ATTR_USER, T_ATTR_CAPS_SYSCALL, T_ATTR_DROP,
    T_LBRACK, T_RBRACK, T_KW_OWN,
    T_LP, T_RP, T_LB, T_RB, T_COMMA, T_SEMI, T_COLON,
    T_WALRUS, T_ARROW, T_ASSIGN,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_PLUSEQ, T_MINUSEQ,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
    T_NOT, T_ANDAND, T_OROR, T_AMP, T_PIPE, T_CARET, T_SHL, T_SHR,
    T_DOT, T_QUESTION
} TK;

typedef struct {
    TK k;
    int line;
    long long ival;
    char* s;
    int slen;
} Tok;

static const char* FILENAME;
static const char* SRC;
static int POS, LEN, LINE = 1, COL = 1;

/* string-interpolation mode stack */
static int istack[16], itop = -1, brace_depth = 0, resume_str = 0;

static int is_idc(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_dg(int c)  { return c >= '0' && c <= '9'; }
static int is_hx(int c)  { return is_dg(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int hxv(int c)    { return is_dg(c) ? c - '0' : (c | 32) - 'a' + 10; }

static Tok mk(TK k, int sl) {
    Tok t;
    memset(&t, 0, sizeof t);
    t.k = k;
    t.line = sl;
    return t;
}

static Tok scan_string_body(int cont) {
    int sl = LINE;
    char* out = xmalloc((size_t)LEN + 1);
    int on = 0;
    for (;;) {
        if (POS >= LEN) die("%s:%d: unterminated string", FILENAME, sl);
        char c = SRC[POS];
        if (c == '"') {
            POS++; COL++;
            Tok t = mk(cont ? T_STR_TAIL : T_STR, sl);
            t.s = out; t.slen = on;
            return t;
        }
        if (c == '{') {
            POS++; COL++;
            if (itop + 1 >= 16) die("%s:%d: interpolation too deep", FILENAME, sl);
            istack[++itop] = brace_depth;
            Tok t = mk(cont ? T_STR_MID : T_STR_HEAD, sl);
            t.s = out; t.slen = on;
            return t;
        }
        if (c == '\\') {
            POS++; COL++;
            if (POS >= LEN) die("%s:%d: bad escape", FILENAME, sl);
            char e = SRC[POS++]; COL++;
            int v;
            switch (e) {
                case 'n': v = '\n'; break;
                case 't': v = '\t'; break;
                case 'r': v = '\r'; break;
                case '0': v = 0;    break;
                case '\\': v = '\\'; break;
                case '"': v = '"';  break;
                case '\'': v = '\''; break;
                case '{': v = '{';  break;
                case '}': v = '}';  break;
                default: die("%s:%d: bad escape '\\%c'", FILENAME, sl, e); v = 0;
            }
            out[on++] = (char)v;
            continue;
        }
        if (c == '\n') { LINE++; COL = 1; } else COL++;
        out[on++] = c;
        POS++;
    }
}

static TK kwlook(const char* s, int n) {
    static const struct { const char* w; TK k; } K[] = {
        {"extern", T_KW_EXTERN}, {"syscall", T_KW_SYSCALL}, {"fn", T_KW_FN},
        {"mut", T_KW_MUT}, {"return", T_KW_RETURN}, {"if", T_KW_IF},
        {"else", T_KW_ELSE}, {"while", T_KW_WHILE}, {"break", T_KW_BREAK},
        {"continue", T_KW_CONTINUE}, {"as", T_KW_AS}, {"raw", T_KW_RAW},
        {"true", T_KW_TRUE}, {"false", T_KW_FALSE}, {"struct", T_KW_STRUCT},
        {"defer", T_KW_DEFER}, {"enum", T_KW_ENUM}, {"match", T_KW_MATCH},
        {"impl", T_KW_IMPL}, {"for", T_KW_FOR}, {"in", T_KW_IN},
        {"own", T_KW_OWN},
    };
    for (size_t i = 0; i < sizeof(K) / sizeof(K[0]); i++)
        if ((int)strlen(K[i].w) == n && !memcmp(K[i].w, s, (size_t)n)) return K[i].k;
    return T_IDENT;
}

static Tok next_token(void) {
    if (resume_str) {
        resume_str = 0;
        return scan_string_body(1);
    }
    for (;;) {   /* skip whitespace + comments */
        if (POS >= LEN) return mk(T_EOF, LINE);
        char c = SRC[POS];
        if (c == ' ' || c == '\t' || c == '\r') { POS++; COL++; continue; }
        if (c == '\n') { POS++; LINE++; COL = 1; continue; }
        if (c == '/' && POS + 1 < LEN && SRC[POS + 1] == '/') {
            while (POS < LEN && SRC[POS] != '\n') POS++;
            continue;
        }
        if (c == '/' && POS + 1 < LEN && SRC[POS + 1] == '*') {
            int d = 1;
            POS += 2; COL += 2;
            while (POS < LEN && d) {
                if (SRC[POS] == '/' && POS + 1 < LEN && SRC[POS + 1] == '*') { d++; POS += 2; continue; }
                if (SRC[POS] == '*' && POS + 1 < LEN && SRC[POS + 1] == '/') { d--; POS += 2; continue; }
                if (SRC[POS] == '\n') { LINE++; COL = 1; }
                POS++;
            }
            continue;
        }
        break;
    }
    int sl = LINE;
    char c = SRC[POS];

    if (is_idc(c)) {
        int st = POS;
        while (POS < LEN && (is_idc(SRC[POS]) || is_dg(SRC[POS]))) { POS++; COL++; }
        int n = POS - st;
        TK k = kwlook(SRC + st, n);
        Tok t = mk(k, sl);
        if (k == T_IDENT) { t.s = xstrndup(SRC + st, (size_t)n); t.slen = n; }
        return t;
    }
    if (is_dg(c)) {
        long long v = 0;
        if (c == '0' && POS + 1 < LEN && SRC[POS + 1] == 'x') {
            POS += 2; COL += 2;
            while (POS < LEN && (is_hx(SRC[POS]) || SRC[POS] == '_')) {
                if (SRC[POS] != '_') v = v * 16 + hxv(SRC[POS]);
                POS++; COL++;
            }
        } else {
            while (POS < LEN && (is_dg(SRC[POS]) || SRC[POS] == '_')) {
                if (SRC[POS] != '_') v = v * 10 + (SRC[POS] - '0');
                POS++; COL++;
            }
        }
        Tok t = mk(T_INT, sl);
        t.ival = v;
        return t;
    }
    if (c == '"') { POS++; COL++; return scan_string_body(0); }

    POS++; COL++;
    char n2 = POS < LEN ? SRC[POS] : 0;
    switch (c) {
        case '(': return mk(T_LP, sl);
        case ')': return mk(T_RP, sl);
        case ',': return mk(T_COMMA, sl);
        case ';': return mk(T_SEMI, sl);
        case '.': if (n2 == '.') { POS++; COL++; return mk(T_DOTDOT, sl); }
                  return mk(T_DOT, sl);
        case '[': return mk(T_LBRACK, sl);
        case ']': return mk(T_RBRACK, sl);
        case '?': return mk(T_QUESTION, sl);
        case '^': return mk(T_CARET, sl);
        case '%': return mk(T_PERCENT, sl);
        case '*': return mk(T_STAR, sl);
        case '/': return mk(T_SLASH, sl);
        case '{': brace_depth++; return mk(T_LB, sl);
        case '}':
            if (itop >= 0 && brace_depth == istack[itop]) {
                itop--;
                resume_str = 1;
                return mk(T_INTERP_R, sl);
            }
            brace_depth--;
            return mk(T_RB, sl);
        case ':': if (n2 == '=') { POS++; COL++; return mk(T_WALRUS, sl); } return mk(T_COLON, sl);
        case '-': if (n2 == '>') { POS++; COL++; return mk(T_ARROW, sl); }
                  if (n2 == '=') { POS++; COL++; return mk(T_MINUSEQ, sl); }
                  return mk(T_MINUS, sl);
        case '+': if (n2 == '=') { POS++; COL++; return mk(T_PLUSEQ, sl); } return mk(T_PLUS, sl);
        case '=': if (n2 == '=') { POS++; COL++; return mk(T_EQ, sl); }
                  if (n2 == '>') { POS++; COL++; return mk(T_FATARROW, sl); }
                  return mk(T_ASSIGN, sl);
        case '!': if (n2 == '=') { POS++; COL++; return mk(T_NE, sl); } return mk(T_NOT, sl);
        case '<': if (n2 == '=') { POS++; COL++; return mk(T_LE, sl); }
                  if (n2 == '<') { POS++; COL++; return mk(T_SHL, sl); }
                  return mk(T_LT, sl);
        case '>': if (n2 == '=') { POS++; COL++; return mk(T_GE, sl); }
                  if (n2 == '>') { POS++; COL++; return mk(T_SHR, sl); }
                  return mk(T_GT, sl);
        case '&': if (n2 == '&') { POS++; COL++; return mk(T_ANDAND, sl); } return mk(T_AMP, sl);
        case '|': if (n2 == '|') { POS++; COL++; return mk(T_OROR, sl); } return mk(T_PIPE, sl);
        case '#':                 /* attributes: #[user] (v0.12), #[caps(syscall)]
                                   * (v0.14), #[drop(fn)] (v0.19) */
            if (POS + 6 <= LEN && !strncmp(&SRC[POS], "[user]", 6)) {
                POS += 6; COL += 6;
                return mk(T_ATTR_USER, sl);
            }
            if (POS + 15 <= LEN && !strncmp(&SRC[POS], "[caps(syscall)]", 15)) {
                POS += 15; COL += 15;
                return mk(T_ATTR_CAPS_SYSCALL, sl);
            }
            if (POS + 6 <= LEN && !strncmp(&SRC[POS], "[drop(", 6)) {
                POS += 6; COL += 6;
                int st = POS;
                while (POS < LEN && (is_idc(SRC[POS]) || is_dg(SRC[POS]))) { POS++; COL++; }
                if (POS == st || POS + 2 > LEN || strncmp(&SRC[POS], ")]", 2) != 0)
                    die("%s:%d: #[drop(...)] names one function, like #[drop(close_file)]",
                        FILENAME, sl);
                Tok t = mk(T_ATTR_DROP, sl);
                t.s = xstrndup(SRC + st, (size_t)(POS - st));
                POS += 2; COL += 2;
                return t;
            }
            die("%s:%d: unknown attribute — #[user], #[caps(syscall)] and #[drop(fn)] are the attributes",
                FILENAME, sl);
    }
    die("%s:%d: unexpected character '%c'", FILENAME, sl, c);
    return mk(T_EOF, sl);
}

/* ------------------------------------------------------------------ */
/* driver: read, validate (full lex), lower (identity), write         */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv) {
    if (argc != 4 || strcmp(argv[2], "-o") != 0)
        die("usage: nppc <file.npp> -o <out.n>");
    FILENAME = argv[1];

    FILE* f = fopen(FILENAME, "rb");
    if (!f) die("nppc: cannot open '%s'", FILENAME);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = xmalloc((size_t)sz + 1);
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        die("nppc: cannot read '%s'", FILENAME);
    fclose(f);
    SRC = buf;
    LEN = (int)sz;

    /* The read side is real: the whole file must lex as the N dialect.
     * A file that fails here would fail identically under ncc — refusing
     * it now keeps the M6.2 contract ("the accepted dialect is the N
     * subset") honest. The tokens are counted and discarded; the parse
     * tree arrives with the first real transform (M6.3). */
    long ntok = 0;
    for (;;) {
        Tok t = next_token();
        if (t.k == T_EOF) break;
        if (t.s) free(t.s);
        ntok++;
    }
    if (itop >= 0) die("%s: unterminated interpolation at end of file", FILENAME);

    /* Lowering, at this rung, is the identity: every valid N program is a
     * valid N++ program with identical behavior, and the skeleton holds
     * that contract byte-for-byte. */
    FILE* out = fopen(argv[3], "wb");
    if (!out) die("nppc: cannot open '%s' for writing", argv[3]);
    if (sz > 0 && fwrite(buf, 1, (size_t)sz, out) != (size_t)sz)
        die("nppc: cannot write '%s'", argv[3]);
    fclose(out);

    fprintf(stderr, "nppc: OK — %ld token(s), lowered to %s (identity at M6.2)\n",
            ntok, argv[3]);
    return 0;
}
