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

/* Source span of the token next_token() is about to return: TOK_START is set
 * past leading whitespace/comments, TOK_END is read as POS right after the
 * call. The generic pass (M6.3) needs these to splice edits over the verbatim
 * source, so everything it does not rewrite keeps its exact bytes. */
static int TOK_START;

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
        TOK_START = POS;
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
    TOK_START = POS;
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
/* token buffer — the generic pass (M6.3) needs random access + spans  */
/* ------------------------------------------------------------------ */
typedef struct { TK k; int start; int end; int line; char* s; int slen; } STok;
static STok* TOKS;
static int NTOK;

static void lex_all(void) {
    int cap = 1024;
    TOKS = xmalloc((size_t)cap * sizeof(STok));
    NTOK = 0;
    for (;;) {
        Tok t = next_token();
        if (NTOK >= cap) {
            cap *= 2;
            TOKS = realloc(TOKS, (size_t)cap * sizeof(STok));
            if (!TOKS) die("nppc: out of memory");
        }
        STok s;
        s.k = t.k; s.s = t.s; s.slen = t.slen; s.line = t.line;
        s.start = (t.k == T_EOF) ? LEN : TOK_START;
        s.end = POS;
        TOKS[NTOK++] = s;
        if (t.k == T_EOF) break;
    }
    if (itop >= 0) die("%s: unterminated interpolation at end of file", FILENAME);
}

/* Lex `text` as the current file. The lexer's state is reset per call, so
 * module resolution (M6.5) can lex each file in turn and the generic pass
 * can lex the resolved program afterwards. */
static void lex_text(const char* text, long len) {
    SRC = text; LEN = (int)len;
    POS = 0; LINE = 1; COL = 1; TOK_START = 0;
    itop = -1; brace_depth = 0; resume_str = 0;
    lex_all();
}

static int tokspan_eq(int a, int b) {      /* two IDENT tokens name the same word */
    return TOKS[a].k == T_IDENT && TOKS[b].k == T_IDENT &&
           TOKS[a].slen == TOKS[b].slen &&
           !memcmp(TOKS[a].s, TOKS[b].s, (size_t)TOKS[a].slen);
}

/* ------------------------------------------------------------------ */
/* the generic pass (M6.3a): monomorphize generic structs             */
/*                                                                    */
/* `struct Box<T> { val: T }` used as `Box<i64>` (in a type position   */
/* or a `Box<i64>{...}` literal) becomes a concrete `__g_Box_i64` with */
/* T substituted, and every use is rewritten to the mangled name. The  */
/* transform is span-local: only the generic declaration and its use   */
/* sites are edited, so all other bytes (comments, spacing) pass        */
/* through verbatim, and a program with no generics is the identity.    */
/* ------------------------------------------------------------------ */
#define MAXP 8
#define MAXF 64
#define MAXG 64
#define MAXI 2048
#define MAXNEST 32

/* A generic use inside another generic's template (M6.3g) — `Box<T>` in the
 * body of `fn wrap<T>`. It denotes one instantiation of the inner generic
 * per concrete instantiation of the template: each argument is either a
 * concrete type name or one of the template's type parameters (aparam). */
typedef struct {
    int gi;                               /* the inner generic */
    char* aname[MAXP]; int alen[MAXP];    /* argument names ... */
    int aparam[MAXP];                     /* ... or the template type-param they name (-1 if concrete) */
    int nargs;
    int start, end;                       /* the use's span inside the template */
} Nest;

typedef struct {
    int kind;                             /* 0 = struct, 1 = fn, 2 = enum */
    int nametok;                          /* the IDENT token of the generic name */
    int ptok[MAXP]; int nparams;          /* type-param IDENT tokens */
    int declstart, declend;               /* source span of the whole declaration */
    /* struct-only: the field templates */
    struct { int fname; int ptrs; int base; } fields[MAXF];
    int nfields;                          /* base: token index of the field type name */
    /* fn-only: the token bounds the span-splice emit walks */
    int angleclose;                       /* token index of `>` closing the params */
    int lasttok;                          /* token index of the body's closing `}` */
    /* generic uses inside this template (M6.3g), instantiated per concrete
     * instantiation of it */
    Nest nested[MAXNEST]; int nnested;
} GStruct;
static GStruct GS[MAXG];
static int NGS;

typedef struct {
    int gi;                               /* which GStruct */
    char* aname[MAXP]; int alen[MAXP];    /* type-arg names (a token's text for an
                                           * explicit arg, an inferred type for a
                                           * bare call) */
    int nargs;
    int start, end;                       /* span to rewrite to the mangled name */
} Inst;
static Inst INSTS[MAXI];
static int NINST;

typedef struct { int start, end; char* repl; } Edit;
static Edit EDITS[MAXG + MAXI];
static int NEDIT;
static int edit_cmp(const void* x, const void* y);

static int gfind(int nametok) {           /* generic index whose name == this IDENT */
    for (int i = 0; i < NGS; i++)
        if (tokspan_eq(GS[i].nametok, nametok)) return i;
    return -1;
}

/* The template whose declaration contains byte `pos`, or -1. Templates do
 * not nest, so there is at most one. */
static int enclosing_template(int pos) {
    for (int i = 0; i < NGS; i++)
        if (pos >= GS[i].declstart && pos < GS[i].declend) return i;
    return -1;
}

/* Collect `struct NAME<params> { fields }` declarations. Plain (non-generic)
 * structs have no `<` after the name and are left entirely alone. */
static void collect_generic_decls(void) {
    for (int i = 0; i + 2 < NTOK; i++) {
        if (TOKS[i].k != T_KW_STRUCT) continue;
        if (TOKS[i + 1].k != T_IDENT || TOKS[i + 2].k != T_LT) continue;
        if (NGS >= MAXG) die("%s: too many generic structs", FILENAME);
        GStruct* g = &GS[NGS];
        g->kind = 0;
        g->nametok = i + 1;
        g->nparams = 0;
        g->nnested = 0;
        g->declstart = TOKS[i].start;
        int j = i + 3;                    /* first param */
        for (;;) {
            if (TOKS[j].k != T_IDENT)
                die("%s:%d: generic parameter must be a name", FILENAME, TOKS[j].line);
            if (g->nparams >= MAXP) die("%s: too many type parameters", FILENAME);
            g->ptok[g->nparams++] = j++;
            if (TOKS[j].k == T_COMMA) { j++; continue; }
            if (TOKS[j].k == T_GT) { j++; break; }
            die("%s:%d: expected ',' or '>' in type-parameter list", FILENAME, TOKS[j].line);
        }
        if (TOKS[j].k != T_LB)
            die("%s:%d: expected '{' after generic struct header", FILENAME, TOKS[j].line);
        j++;                              /* into the body */
        g->nfields = 0;
        while (TOKS[j].k != T_RB) {
            if (TOKS[j].k == T_EOF)
                die("%s: unterminated generic struct body", FILENAME);
            if (TOKS[j].k != T_IDENT)
                die("%s:%d: expected a field name", FILENAME, TOKS[j].line);
            int fname = j++;
            if (TOKS[j].k != T_COLON)
                die("%s:%d: expected ':' after field name", FILENAME, TOKS[j].line);
            j++;
            if (TOKS[j].k == T_ATTR_USER || TOKS[j].k == T_KW_RAW)
                die("%s:%d: M6.3a: attributes on generic-struct fields are pending",
                    FILENAME, TOKS[j].line);
            int ptrs = 0;
            while (TOKS[j].k == T_STAR) { ptrs++; j++; }
            if (TOKS[j].k != T_IDENT)
                die("%s:%d: expected a field type", FILENAME, TOKS[j].line);
            if (g->nfields >= MAXF) die("%s: too many fields", FILENAME);
            g->fields[g->nfields].fname = fname;
            g->fields[g->nfields].ptrs = ptrs;
            g->fields[g->nfields].base = j++;
            g->nfields++;
            if (TOKS[j].k == T_LT)
                die("%s:%d: M6.3g: a generic type in a generic struct's field is pending "
                    "(a generic function or enum may use one)", FILENAME, TOKS[j].line);
            if (TOKS[j].k == T_COMMA) j++;
        }
        g->declend = TOKS[j].end;         /* past the closing '}' */
        NGS++;
    }
}

/* Collect `NAME<args>` use sites of a declared generic. Lenient: a `<` that
 * is not a well-formed type-argument list is left as a comparison operator. */
static void collect_instantiations(void) {
    for (int i = 0; i + 1 < NTOK; i++) {
        if (TOKS[i].k != T_IDENT || TOKS[i + 1].k != T_LT) continue;
        if (i > 0 && (TOKS[i - 1].k == T_KW_STRUCT || TOKS[i - 1].k == T_KW_FN ||
                      TOKS[i - 1].k == T_KW_ENUM))
            continue;                      /* the decl header, not a use site */
        int gi = gfind(i);
        if (gi < 0) continue;             /* `<` after a non-generic name: comparison */
        int atok[MAXP], nargs = 0, ok = 1, j = i + 2;
        for (;;) {
            if (TOKS[j].k != T_IDENT) { ok = 0; break; }
            if (nargs >= MAXP) { ok = 0; break; }
            atok[nargs++] = j++;
            if (TOKS[j].k == T_COMMA) { j++; continue; }
            if (TOKS[j].k == T_GT) { j++; break; }
            ok = 0; break;
        }
        if (!ok || nargs != GS[gi].nparams) continue;   /* not this generic's shape */
        int outer = enclosing_template(TOKS[i].start);
        if (outer >= 0) {
            /* Inside another template (M6.3g): instantiated per concrete
             * instantiation of that template, by expand_nested. */
            GStruct* og = &GS[outer];
            if (og->nnested >= MAXNEST) die("%s: too many nested generic uses", FILENAME);
            Nest* ns = &og->nested[og->nnested++];
            ns->gi = gi;
            ns->nargs = nargs;
            for (int a = 0; a < nargs; a++) {
                ns->aname[a] = TOKS[atok[a]].s;
                ns->alen[a] = TOKS[atok[a]].slen;
                ns->aparam[a] = -1;
                for (int q = 0; q < og->nparams; q++)
                    if (tokspan_eq(og->ptok[q], atok[a])) ns->aparam[a] = q;
            }
            ns->start = TOKS[i].start;
            ns->end = TOKS[j - 1].end;
            i = j - 1;
            continue;
        }
        if (NINST >= MAXI) die("%s: too many generic instantiations", FILENAME);
        Inst* n = &INSTS[NINST++];
        n->gi = gi;
        n->nargs = nargs;
        for (int a = 0; a < nargs; a++) {
            n->aname[a] = TOKS[atok[a]].s;
            n->alen[a] = TOKS[atok[a]].slen;
        }
        n->start = TOKS[i].start;
        n->end = TOKS[j - 1].end;         /* past the closing '>' */
        i = j - 1;                        /* skip past the args */
    }
}

/* Bare generic-function calls `NAME(args)` with no explicit `<...>`: infer
 * each type parameter from a literal argument in the matching position, and
 * record an instantiation that rewrites just the name. Literal arguments only
 * for now (int -> i64, string -> str, bool -> bool); anything else asks for an
 * explicit `NAME<Type>(...)`. */
static void collect_inferred_calls(void) {
    for (int i = 0; i + 1 < NTOK; i++) {
        if (TOKS[i].k != T_IDENT || TOKS[i + 1].k != T_LP) continue;
        if (i > 0 && TOKS[i - 1].k == T_KW_FN) continue;   /* a decl header, not a call */
        int gi = gfind(i);
        if (gi < 0 || GS[gi].kind != 1) continue;          /* generic-fn calls only */
        GStruct* g = &GS[gi];
        /* Walk the fn's own parameter list `(name: type, ...)` from just after
         * `>`, mapping each value parameter to a type-param index (or -1). */
        int vptp[64], nv = 0, j = g->angleclose + 2;       /* first param name */
        while (TOKS[j].k != T_RP && TOKS[j].k != T_EOF) {
            if (TOKS[j].k != T_IDENT) break;
            j++;
            if (TOKS[j].k != T_COLON) break;
            j++;
            while (TOKS[j].k == T_ATTR_USER || TOKS[j].k == T_KW_RAW || TOKS[j].k == T_STAR) j++;
            int tp = -1;
            for (int p = 0; p < g->nparams; p++)
                if (tokspan_eq(g->ptok[p], j)) tp = p;
            if (nv < 64) vptp[nv++] = tp;
            j++;
            if (TOKS[j].k == T_COMMA) { j++; continue; }
            break;
        }
        /* First token of each top-level call argument. */
        int argfirst[64], na = 0, depth = 0, startarg = 1;
        for (int t = i + 1; TOKS[t].k != T_EOF; t++) {
            TK kk = TOKS[t].k;
            if (kk == T_LP || kk == T_LB || kk == T_LBRACK) depth++;
            else if (kk == T_RP || kk == T_RB || kk == T_RBRACK) { depth--; if (depth == 0) break; }
            else if (kk == T_COMMA && depth == 1) startarg = 1;
            else if (depth >= 1 && startarg) { if (na < 64) argfirst[na++] = t; startarg = 0; }
        }
        /* Infer each type parameter from the value parameter that uses it. */
        char* an[MAXP]; int al[MAXP], bound[MAXP];
        for (int p = 0; p < g->nparams; p++) bound[p] = 0;
        for (int v = 0; v < nv && v < na; v++) {
            int tp = vptp[v];
            if (tp < 0) continue;
            char* ty; int tl;
            TK ak = TOKS[argfirst[v]].k;
            if (ak == T_INT) { ty = "i64"; tl = 3; }
            else if (ak == T_STR || ak == T_STR_HEAD) { ty = "str"; tl = 3; }
            else if (ak == T_KW_TRUE || ak == T_KW_FALSE) { ty = "bool"; tl = 4; }
            else die("%s:%d: cannot infer a generic type from argument %d — write it "
                     "explicitly, %.*s<Type>(...)", FILENAME, TOKS[i].line, v + 1,
                     TOKS[g->nametok].slen, TOKS[g->nametok].s);
            if (bound[tp] && (al[tp] != tl || memcmp(an[tp], ty, (size_t)tl)))
                die("%s:%d: conflicting types inferred for one generic parameter",
                    FILENAME, TOKS[i].line);
            an[tp] = ty; al[tp] = tl; bound[tp] = 1;
        }
        for (int p = 0; p < g->nparams; p++)
            if (!bound[p])
                die("%s:%d: cannot infer every generic parameter of '%.*s' — write them "
                    "explicitly", FILENAME, TOKS[i].line, TOKS[g->nametok].slen, TOKS[g->nametok].s);
        int outer = enclosing_template(TOKS[i].start);
        if (outer >= 0) {                  /* inside a template: a nested use (M6.3g) */
            GStruct* og = &GS[outer];
            if (og->nnested >= MAXNEST) die("%s: too many nested generic uses", FILENAME);
            Nest* ns = &og->nested[og->nnested++];
            ns->gi = gi;
            ns->nargs = g->nparams;
            for (int p = 0; p < g->nparams; p++) {
                ns->aname[p] = an[p]; ns->alen[p] = al[p]; ns->aparam[p] = -1;
            }
            ns->start = TOKS[i].start;
            ns->end = TOKS[i].end;
            continue;
        }
        if (NINST >= MAXI) die("%s: too many generic instantiations", FILENAME);
        Inst* n = &INSTS[NINST++];
        n->gi = gi;
        n->nargs = g->nparams;
        for (int p = 0; p < g->nparams; p++) { n->aname[p] = an[p]; n->alen[p] = al[p]; }
        n->start = TOKS[i].start;
        n->end = TOKS[i].end;              /* rewrite just the name to the mangled name */
    }
}

/* Bare generic-enum constructions `NAME.Variant{...}` / `NAME.Variant` with
 * no `<...>` (M6.3f): the type arguments are read from the enclosing
 * function's declared return type, which must be an explicit instantiation of
 * the same enum (`-> NAME<args>`) — the `Result.Ok{ v: n }` idiom inside a
 * function returning `Result<i64, i64>`. The recorded instantiation rewrites
 * just the name, so it dedups against the return type's own. A construction
 * anywhere else — a function returning something different, no enclosing
 * function, or a generic function's body (emitted per instantiation, so it
 * cannot carry a global edit yet) — asks for the explicit
 * `NAME<Type, ...>.Variant` form. */
static void collect_inferred_constructions(void) {
    int ret_gi = -1;                      /* the enclosing fn's return instantiation */
    char* ran[MAXP]; int ral[MAXP]; int rparam[MAXP];
    int body_end = -1;                    /* token index of its body's closing `}` */
    int generic_fn = 0, outer_gi = -1;    /* a template's own GStruct, when inside one */
    for (int i = 0; i + 1 < NTOK; i++) {
        if (TOKS[i].k == T_KW_FN && TOKS[i + 1].k == T_IDENT) {
            int j = i + 2, depth = 0;
            ret_gi = -1; body_end = -1; generic_fn = 0; outer_gi = -1;
            if (TOKS[j].k == T_LT) {      /* fn NAME<params>(...) */
                generic_fn = 1;
                outer_gi = gfind(i + 1);
                while (TOKS[j].k != T_GT && TOKS[j].k != T_EOF) j++;
                j++;
            }
            if (TOKS[j].k != T_LP) continue;
            for (; TOKS[j].k != T_EOF; j++) {   /* past the parameter list */
                if (TOKS[j].k == T_LP) depth++;
                else if (TOKS[j].k == T_RP && --depth == 0) { j++; break; }
            }
            if (TOKS[j].k == T_ARROW && TOKS[j + 1].k == T_IDENT && TOKS[j + 2].k == T_LT) {
                int gi = gfind(j + 1);
                if (gi >= 0 && GS[gi].kind == 2) {
                    int n = 0, k = j + 3, ok = 1;
                    for (;;) {
                        if (TOKS[k].k != T_IDENT || n >= MAXP) { ok = 0; break; }
                        ran[n] = TOKS[k].s; ral[n] = TOKS[k].slen; rparam[n] = -1;
                        if (outer_gi >= 0)    /* an argument that is the template's own type-param */
                            for (int q = 0; q < GS[outer_gi].nparams; q++)
                                if (tokspan_eq(GS[outer_gi].ptok[q], k)) rparam[n] = q;
                        n++; k++;
                        if (TOKS[k].k == T_COMMA) { k++; continue; }
                        if (TOKS[k].k == T_GT) break;
                        ok = 0; break;
                    }
                    if (ok && n == GS[gi].nparams) ret_gi = gi;
                }
            }
            /* The body, if the function has one (extern fns end in `= N`). */
            while (TOKS[j].k != T_LB && TOKS[j].k != T_ASSIGN && TOKS[j].k != T_SEMI &&
                   TOKS[j].k != T_EOF) j++;
            if (TOKS[j].k == T_LB) {
                depth = 0;
                for (; TOKS[j].k != T_EOF; j++) {
                    if (TOKS[j].k == T_LB) depth++;
                    else if (TOKS[j].k == T_RB && --depth == 0) break;
                }
                body_end = j;
            }
            continue;
        }
        if (TOKS[i].k != T_IDENT || TOKS[i + 1].k != T_DOT) continue;
        if (i > 0 && TOKS[i - 1].k == T_DOT) continue;       /* a field, not a type */
        int gi = gfind(i);
        if (gi < 0 || GS[gi].kind != 2) continue;
        GStruct* g = &GS[gi];
        if (i > body_end)
            die("%s:%d: '%.*s.' names no type arguments and has no enclosing function "
                "to take them from — write %.*s<Type, ...>.Variant",
                FILENAME, TOKS[i].line, TOKS[i].slen, TOKS[i].s,
                TOKS[g->nametok].slen, TOKS[g->nametok].s);
        if (ret_gi != gi)
            die("%s:%d: cannot infer the type arguments of '%.*s.': the enclosing "
                "function does not return %.*s<...> — write %.*s<Type, ...>.Variant",
                FILENAME, TOKS[i].line, TOKS[i].slen, TOKS[i].s,
                TOKS[g->nametok].slen, TOKS[g->nametok].s,
                TOKS[g->nametok].slen, TOKS[g->nametok].s);
        if (generic_fn) {
            /* Inside a template (M6.3g): a nested use carrying the return
             * type's arguments — some of them the template's own parameters —
             * instantiated per concrete instantiation of the template. */
            GStruct* og = &GS[outer_gi];
            if (og->nnested >= MAXNEST) die("%s: too many nested generic uses", FILENAME);
            Nest* ns = &og->nested[og->nnested++];
            ns->gi = gi;
            ns->nargs = g->nparams;
            for (int p = 0; p < g->nparams; p++) {
                ns->aname[p] = ran[p]; ns->alen[p] = ral[p]; ns->aparam[p] = rparam[p];
            }
            ns->start = TOKS[i].start;
            ns->end = TOKS[i].end;
            continue;
        }
        if (NINST >= MAXI) die("%s: too many generic instantiations", FILENAME);
        Inst* n = &INSTS[NINST++];
        n->gi = gi;
        n->nargs = g->nparams;
        for (int p = 0; p < g->nparams; p++) { n->aname[p] = ran[p]; n->alen[p] = ral[p]; }
        n->start = TOKS[i].start;
        n->end = TOKS[i].end;              /* rewrite just the name to the mangled name */
    }
}

/* "__g_Box_i64" for Box<i64>. Args are bare type names, so the result is a
 * valid N/C identifier by construction. */
static char* mangle(Inst* it) {
    GStruct* g = &GS[it->gi];
    char* out = xmalloc(256);
    int n = 0;
    n += sprintf(out + n, "__g_%.*s", TOKS[g->nametok].slen, TOKS[g->nametok].s);
    for (int a = 0; a < g->nparams; a++)
        n += sprintf(out + n, "_%.*s", it->alen[a], it->aname[a]);
    return out;
}

/* The instantiation a nested generic use denotes for one concrete
 * instantiation of its template: the template's arguments substituted for
 * the type parameters the use names; concrete names pass through. It has no
 * use site of its own to rewrite (start = end = -1) — the template's emit
 * rewrites the use. */
static void nested_inst(Inst* outer, Nest* ns, Inst* out) {
    out->gi = ns->gi;
    out->nargs = ns->nargs;
    for (int a = 0; a < ns->nargs; a++) {
        if (ns->aparam[a] >= 0) {
            out->aname[a] = outer->aname[ns->aparam[a]];
            out->alen[a] = outer->alen[ns->aparam[a]];
        } else {
            out->aname[a] = ns->aname[a];
            out->alen[a] = ns->alen[a];
        }
    }
    out->start = -1;
    out->end = -1;
}

/* The concrete N struct for one instantiation: the template body with each
 * type parameter replaced by the matching argument. */
static char* concrete_struct(Inst* it) {
    GStruct* g = &GS[it->gi];
    char* mn = mangle(it);
    char* out = xmalloc(4096);
    int n = 0;
    n += sprintf(out + n, "struct %s {\n", mn);
    for (int fi = 0; fi < g->nfields; fi++) {
        int bt = g->fields[fi].base;
        int sub = -1;                     /* is the field type a type parameter? */
        for (int p = 0; p < g->nparams; p++)
            if (tokspan_eq(g->ptok[p], bt)) { sub = p; break; }
        int stars = g->fields[fi].ptrs;
        n += sprintf(out + n, "    %.*s: ", TOKS[g->fields[fi].fname].slen,
                     TOKS[g->fields[fi].fname].s);
        for (int s = 0; s < stars; s++) out[n++] = '*';
        if (sub >= 0)
            n += sprintf(out + n, "%.*s", it->alen[sub], it->aname[sub]);
        else
            n += sprintf(out + n, "%.*s", TOKS[bt].slen, TOKS[bt].s);
        n += sprintf(out + n, ",\n");
    }
    n += sprintf(out + n, "}");
    return out;
}

/* Is token t a type-parameter of generic g used in a type position (a base
 * type after ':' , '->' or '*')? Returns the param index, or -1. */
static int typaram_at(GStruct* g, int t) {
    if (TOKS[t].k != T_IDENT || t == 0) return -1;
    TK p = TOKS[t - 1].k;
    /* A base type follows ':' (a param/field type), '->' (a return type),
     * '*' (a pointer type), or 'as' (a cast target — the one type slot that
     * appears inside a function body, since N locals are always inferred).
     * An argument of a generic use — `Box<T>`, `Pair<T, U>` (M6.3g) — is
     * recognized by walking back to the `<` with a generic's name before it. */
    if (p == T_LT || p == T_COMMA) {
        int j = t - 1;
        while (j > 0 && (TOKS[j].k == T_COMMA || TOKS[j].k == T_IDENT)) j--;
        if (!(TOKS[j].k == T_LT && j > 0 && gfind(j - 1) >= 0)) return -1;
    } else if (p != T_COLON && p != T_ARROW && p != T_STAR && p != T_KW_AS) return -1;
    for (int q = 0; q < g->nparams; q++)
        if (tokspan_eq(g->ptok[q], t)) return q;
    return -1;
}

/* Collect `fn NAME<params>(...) -> ret { body }` generic functions. Every
 * type-parameter use must sit in a handled type position; one elsewhere is
 * refused (M6.3c will lift that), so the emit substitutes every occurrence. */
static void collect_generic_fns(void) {
    for (int i = 0; i + 2 < NTOK; i++) {
        if (TOKS[i].k != T_KW_FN) continue;
        if (TOKS[i + 1].k != T_IDENT || TOKS[i + 2].k != T_LT) continue;
        if (NGS >= MAXG) die("%s: too many generics", FILENAME);
        GStruct* g = &GS[NGS];
        g->kind = 1;
        g->nametok = i + 1;
        g->nparams = 0;
        g->nfields = 0;
        g->declstart = TOKS[i].start;
        int j = i + 3;                    /* first type parameter */
        for (;;) {
            if (TOKS[j].k != T_IDENT)
                die("%s:%d: generic parameter must be a name", FILENAME, TOKS[j].line);
            if (g->nparams >= MAXP) die("%s: too many type parameters", FILENAME);
            g->ptok[g->nparams++] = j++;
            if (TOKS[j].k == T_COMMA) { j++; continue; }
            if (TOKS[j].k == T_GT) { break; }
            die("%s:%d: expected ',' or '>' in type-parameter list", FILENAME, TOKS[j].line);
        }
        g->angleclose = j;                /* the `>` token */
        j++;
        if (TOKS[j].k != T_LP)
            die("%s:%d: expected '(' after generic function header", FILENAME, TOKS[j].line);
        /* skip to the body's opening brace */
        while (TOKS[j].k != T_LB) {
            if (TOKS[j].k == T_EOF)
                die("%s:%d: unterminated generic function header", FILENAME, TOKS[i].line);
            j++;
        }
        int depth = 0;
        for (;;) {
            if (TOKS[j].k == T_EOF) die("%s: unterminated generic function body", FILENAME);
            if (TOKS[j].k == T_LB) depth++;
            else if (TOKS[j].k == T_RB) { depth--; if (depth == 0) break; }
            j++;
        }
        g->lasttok = j;                   /* the body's closing `}` */
        g->declend = TOKS[j].end;
        g->nnested = 0;                   /* filled by the use collectors */
        NGS++;
    }
}

/* Collect `enum NAME<params> { variants }` generic enums. An enum body is
 * variants with optional `(field: type, ...)` payloads; type parameters live
 * in those payload type slots (after ':'), which the span-splice emit handles
 * exactly as it does a function's — so a generic enum reuses concrete_fn. The
 * body `{...}` follows the `>` directly (no parameter list). */
static void collect_generic_enums(void) {
    for (int i = 0; i + 2 < NTOK; i++) {
        if (TOKS[i].k != T_KW_ENUM) continue;
        if (TOKS[i + 1].k != T_IDENT || TOKS[i + 2].k != T_LT) continue;
        if (NGS >= MAXG) die("%s: too many generics", FILENAME);
        GStruct* g = &GS[NGS];
        g->kind = 2;
        g->nametok = i + 1;
        g->nparams = 0;
        g->nfields = 0;
        g->declstart = TOKS[i].start;
        int j = i + 3;
        for (;;) {
            if (TOKS[j].k != T_IDENT)
                die("%s:%d: generic parameter must be a name", FILENAME, TOKS[j].line);
            if (g->nparams >= MAXP) die("%s: too many type parameters", FILENAME);
            g->ptok[g->nparams++] = j++;
            if (TOKS[j].k == T_COMMA) { j++; continue; }
            if (TOKS[j].k == T_GT) { break; }
            die("%s:%d: expected ',' or '>' in type-parameter list", FILENAME, TOKS[j].line);
        }
        g->angleclose = j;
        j++;
        if (TOKS[j].k != T_LB)
            die("%s:%d: expected '{' after generic enum header", FILENAME, TOKS[j].line);
        int depth = 0;
        for (;;) {
            if (TOKS[j].k == T_EOF) die("%s: unterminated generic enum body", FILENAME);
            if (TOKS[j].k == T_LB) depth++;
            else if (TOKS[j].k == T_RB) { depth--; if (depth == 0) break; }
            j++;
        }
        g->lasttok = j;
        g->declend = TOKS[j].end;
        g->nnested = 0;
        NGS++;
    }
}

/* Every type-parameter occurrence inside a template must sit in a position
 * the emit rewrites — a type slot, or an argument of a generic use — so no
 * parameter survives into a concrete item. Checked once every generic is
 * known, since a use may name a generic declared later in the file. */
static void guard_templates(void) {
    for (int gi = 0; gi < NGS; gi++) {
        GStruct* g = &GS[gi];
        if (g->kind == 0) continue;       /* struct fields are parsed, not spliced */
        for (int t = g->angleclose + 1; t <= g->lasttok; t++) {
            if (TOKS[t].k != T_IDENT) continue;
            int isp = 0;
            for (int q = 0; q < g->nparams; q++)
                if (tokspan_eq(g->ptok[q], t)) { isp = 1; break; }
            if (!isp) continue;
            if (typaram_at(g, t) < 0)
                die("%s:%d: type parameter '%.*s' appears in a position the lowering "
                    "does not rewrite (a type slot after ':' / '->' / '*' / 'as', or a "
                    "generic's argument)", FILENAME, TOKS[t].line, TOKS[t].slen, TOKS[t].s);
        }
    }
}

/* The concrete N function or enum for one instantiation: a span-splice of the
 * source declaration with the header rewritten to the mangled name, every
 * type-parameter type slot replaced by its argument, and every generic use
 * inside the template replaced by the concrete name it denotes for this
 * instantiation — the body's other bytes (spacing and comments) pass through
 * untouched. */
static char* concrete_fn(Inst* it) {
    GStruct* g = &GS[it->gi];
    Edit ed[512]; int ne = 0;
    ed[ne].start = TOKS[g->nametok].start;    /* `NAME<params>` -> mangled name */
    ed[ne].end = TOKS[g->angleclose].end;
    ed[ne].repl = mangle(it); ne++;
    for (int u = 0; u < g->nnested; u++) {    /* `Box<T>` -> `__g_Box_i64` (M6.3g) */
        Inst n;
        nested_inst(it, &g->nested[u], &n);
        if (ne >= 512) die("%s: generic item too large", FILENAME);
        ed[ne].start = g->nested[u].start;
        ed[ne].end = g->nested[u].end;
        ed[ne].repl = mangle(&n); ne++;
    }
    for (int t = g->angleclose + 1; t <= g->lasttok; t++) {
        int q = typaram_at(g, t);
        if (q < 0) continue;
        int covered = 0;                      /* an argument of a nested use: its edit covers it */
        for (int u = 0; u < g->nnested; u++)
            if (TOKS[t].start >= g->nested[u].start && TOKS[t].start < g->nested[u].end)
                covered = 1;
        if (covered) continue;
        if (ne >= 512) die("%s: generic item too large", FILENAME);
        ed[ne].start = TOKS[t].start;
        ed[ne].end = TOKS[t].end;
        ed[ne].repl = xstrndup(it->aname[q], (size_t)it->alen[q]); ne++;
    }
    qsort(ed, (size_t)ne, sizeof(Edit), edit_cmp);
    char* out = xmalloc((size_t)(g->declend - g->declstart) + 512 + (size_t)ne * 64);
    int n = 0, prev = g->declstart;
    for (int e = 0; e < ne; e++) {
        memcpy(out + n, SRC + prev, (size_t)(ed[e].start - prev)); n += ed[e].start - prev;
        n += sprintf(out + n, "%s", ed[e].repl);
        prev = ed[e].end;
    }
    memcpy(out + n, SRC + prev, (size_t)(g->declend - prev)); n += g->declend - prev;
    out[n] = 0;
    return out;
}

static char* concrete_item(Inst* it) {
    return GS[it->gi].kind == 0 ? concrete_struct(it) : concrete_fn(it);
}

static int args_same(Inst* a, Inst* b) {
    if (a->gi != b->gi || a->nargs != b->nargs) return 0;
    for (int i = 0; i < a->nargs; i++)
        if (a->alen[i] != b->alen[i] || memcmp(a->aname[i], b->aname[i], (size_t)a->alen[i]))
            return 0;
    return 1;
}

static int edit_cmp(const void* x, const void* y) {
    return ((const Edit*)x)->start - ((const Edit*)y)->start;
}

/* Nested uses (M6.3g), to a fixpoint: every concrete instantiation of a
 * template instantiates the generics its body uses, with the template's
 * arguments substituted — and those instantiations may use others in turn. */
static void expand_nested(void) {
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < NINST; i++) {
            GStruct* g = &GS[INSTS[i].gi];
            for (int u = 0; u < g->nnested; u++) {
                Inst n;
                nested_inst(&INSTS[i], &g->nested[u], &n);
                int dup = 0;
                for (int k = 0; k < NINST; k++)
                    if (args_same(&n, &INSTS[k])) { dup = 1; break; }
                if (dup) continue;
                if (NINST >= MAXI) die("%s: too many generic instantiations", FILENAME);
                INSTS[NINST++] = n;
                changed = 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* modules (M6.5): `use "file.npp";` and `pub`                         */
/*                                                                    */
/* A module is a file. A top-level `use "path";` (the path is relative */
/* to the using file) inlines that file's resolved text in its place — */
/* once per program: a second `use` of the same file leaves only a     */
/* marker comment — and a file still being resolved (a cycle) is       */
/* refused. Resolution runs BEFORE the generic pass, over the combined  */
/* text, so a generic declared in one file and instantiated in another  */
/* monomorphizes and dedups exactly as it would in a single file. `pub` */
/* marks an exported item and is dropped from the lowering; enforcing   */
/* visibility across modules is the next rung.                          */
/* ------------------------------------------------------------------ */
#define MAXUSE 64
static const char* USED[MAXUSE]; static int NUSED;    /* files already inlined */
static const char* CHAIN[MAXUSE]; static int NCHAIN;  /* the use chain being resolved */

/* Where each inlined module landed in the resolved program (byte range),
 * for the visibility pass: an item is visible in its own file, `pub` makes
 * it visible program-wide, and a module's private items are renamed so two
 * modules' privates never meet at the C level. */
typedef struct { const char* name; int id; int start, end; } Mod;
static Mod MODS[MAXUSE]; static int NMOD;

/* The use-graph: one edge per `use` directive, from the using file to the
 * used one, both by file id — the main file is 0, every other file the
 * index it was first inlined under (its USED slot). A `pub` item is visible
 * to the files that use its module directly. */
#define MAXEDGE 256
static int EFROM[MAXEDGE], ETO[MAXEDGE]; static int NEDGE;

static void add_edge(int from, int to) {
    if (NEDGE >= MAXEDGE) die("nppc: too many use directives");
    EFROM[NEDGE] = from;
    ETO[NEDGE] = to;
    NEDGE++;
}

static int uses(int from, int to) {
    for (int e = 0; e < NEDGE; e++)
        if (EFROM[e] == from && ETO[e] == to) return 1;
    return 0;
}

static char* read_file(const char* path, long* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = xmalloc((size_t)sz + 1);
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return NULL; }
    fclose(f);
    buf[sz] = 0;
    *out_len = sz;
    return buf;
}

/* `rel` resolved against the directory of `from` (textually — two spellings
 * of one file are two files to the once-per-program rule). */
static char* join_path(const char* from, const char* rel) {
    int dl = 0;
    for (int i = 0; from[i]; i++)
        if (from[i] == '/' || from[i] == '\\') dl = i + 1;
    char* out = xmalloc((size_t)dl + strlen(rel) + 1);
    memcpy(out, from, (size_t)dl);
    strcpy(out + dl, rel);
    return out;
}

static void app(char** b, size_t* n, size_t* cap, const char* s, size_t l) {
    if (*n + l + 1 > *cap) {
        while (*n + l + 1 > *cap) *cap *= 2;
        *b = realloc(*b, *cap);
        if (!*b) die("nppc: out of memory");
    }
    memcpy(*b + *n, s, l);
    *n += l;
    (*b)[*n] = 0;
}

static char* resolve_file(const char* path, const char* text, long len);

/* The text of one file with every top-level `use` directive replaced by
 * the used file's resolved text. */
static char* resolve_text(const char* path, const char* text, long len, int myid) {
    const char* saved = FILENAME;
    FILENAME = path;
    lex_text(text, len);
    /* Collect the directives first: resolving one re-lexes another file. */
    int us[MAXUSE], ue[MAXUSE], uline[MAXUSE]; char* upath[MAXUSE];
    int nu = 0, depth = 0;
    for (int i = 0; i + 2 < NTOK; i++) {
        if (TOKS[i].k == T_LB) depth++;
        else if (TOKS[i].k == T_RB) depth--;
        if (depth != 0 || TOKS[i].k != T_IDENT || TOKS[i].slen != 3 ||
            memcmp(TOKS[i].s, "use", 3)) continue;
        if (TOKS[i + 1].k != T_STR || TOKS[i + 2].k != T_SEMI)
            die("%s:%d: expected use \"file.npp\";", FILENAME, TOKS[i].line);
        if (nu >= MAXUSE) die("%s: too many use directives", FILENAME);
        us[nu] = TOKS[i].start;
        ue[nu] = TOKS[i + 2].end;
        uline[nu] = TOKS[i].line;
        upath[nu] = xstrndup(TOKS[i + 1].s, (size_t)TOKS[i + 1].slen);
        nu++;
    }
    FILENAME = saved;
    if (nu == 0) return xstrndup(text, (size_t)len);
    /* Splice: verbatim between directives, the used file's text at each. */
    size_t cap = (size_t)len + 1024, n = 0;
    char* out = xmalloc(cap);
    int prev = 0;
    char mark[600];
    for (int u = 0; u < nu; u++) {
        app(&out, &n, &cap, text + prev, (size_t)(us[u] - prev));
        char* full = join_path(path, upath[u]);
        /* A file still on the chain is a cycle (checked before the
         * once-per-program rule, which would otherwise mask it). */
        for (int k = 0; k < NCHAIN; k++)
            if (!strcmp(CHAIN[k], full))
                die("%s:%d: cyclic use of \"%s\"", path, uline[u], upath[u]);
        int seen = -1;
        for (int k = 0; k < NUSED; k++)
            if (!strcmp(USED[k], full)) seen = k;
        if (seen >= 0) {
            add_edge(myid, seen);
            snprintf(mark, sizeof mark, "// use \"%s\" (already inlined)", upath[u]);
            app(&out, &n, &cap, mark, strlen(mark));
        } else {
            long ilen;
            char* itext = read_file(full, &ilen);
            if (!itext)
                die("%s:%d: cannot open \"%s\" (looked for %s)", path, uline[u], upath[u], full);
            int before = NMOD, cid = NUSED;   /* the id resolve_file will assign */
            add_edge(myid, cid);
            char* inner = resolve_file(full, itext, ilen);
            snprintf(mark, sizeof mark, "// use \"%s\" (inlined by nppc)\n", upath[u]);
            app(&out, &n, &cap, mark, strlen(mark));
            int at = (int)n;
            app(&out, &n, &cap, inner, strlen(inner));
            /* Modules inlined inside `inner` were recorded relative to it;
             * shift them to this text, then record `inner` itself. */
            for (int k = before; k < NMOD; k++) { MODS[k].start += at; MODS[k].end += at; }
            if (NMOD >= MAXUSE) die("nppc: too many modules");
            MODS[NMOD].name = upath[u];
            MODS[NMOD].id = cid;
            MODS[NMOD].start = at;
            MODS[NMOD].end = at + (int)strlen(inner);
            NMOD++;
            if (n > 0 && out[n - 1] != '\n') app(&out, &n, &cap, "\n", 1);
            snprintf(mark, sizeof mark, "// end of \"%s\"", upath[u]);
            app(&out, &n, &cap, mark, strlen(mark));
        }
        prev = ue[u];
    }
    app(&out, &n, &cap, text + prev, (size_t)(len - prev));
    return out;
}

/* Resolve one file: it counts as inlined from here on, and sits on the
 * chain while its own uses resolve (that is what a cycle runs into). */
static char* resolve_file(const char* path, const char* text, long len) {
    if (NUSED >= MAXUSE || NCHAIN >= MAXUSE) die("nppc: too many modules");
    int id = NUSED;                        /* the main file is 0 */
    USED[NUSED++] = path;
    CHAIN[NCHAIN++] = path;
    char* out = resolve_text(path, text, len, id);
    NCHAIN--;
    return out;
}

/* The innermost module whose text contains byte `pos`, or -1 for the main
 * file. */
static int mod_owner(int pos) {
    int best = -1;
    for (int m = 0; m < NMOD; m++)
        if (pos >= MODS[m].start && pos < MODS[m].end &&
            (best < 0 || MODS[m].end - MODS[m].start < MODS[best].end - MODS[best].start))
            best = m;
    return best;
}

/* "modlib" for "dir/modlib.npp": the basename without its extension, any
 * other character replaced by '_', so it can sit inside an identifier. */
static char* mod_stem(const char* name) {
    const char* b = name;
    for (const char* p = name; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    char* out = xmalloc(strlen(b) + 1);
    int n = 0;
    for (const char* p = b; *p && *p != '.'; p++)
        out[n++] = (is_idc(*p) || is_dg(*p)) ? *p : '_';
    out[n] = 0;
    return out;
}

/* Is IDENT token t shaped like a reference to a top-level item — a call
 * `x(`, a construction or generic use `x.` / `x{` / `x<`, a type slot after
 * ':' / '->' / '*' / 'as', or the type of an `impl`? A field or method
 * after '.' is not, nor is a binding or parameter name. */
static int names_item(int t) {
    if (TOKS[t].k != T_IDENT) return 0;
    TK p = t > 0 ? TOKS[t - 1].k : T_EOF, nx = TOKS[t + 1].k;
    if (p == T_DOT) return 0;
    if (nx == T_LP || nx == T_DOT || nx == T_LB || nx == T_LT) return 1;
    if (p == T_COLON || p == T_ARROW || p == T_STAR || p == T_KW_AS || p == T_KW_IMPL) return 1;
    /* A type argument — `Box<P>`, `Pair<A, B>`: walk back over the argument
     * list to the `<` and require a generic's name in front of it, so a
     * comparison `a < b` is not mistaken for one. */
    if ((p == T_LT || p == T_COMMA) && (nx == T_GT || nx == T_COMMA)) {
        int j = t - 1;
        while (j > 0 && (TOKS[j].k == T_COMMA || TOKS[j].k == T_IDENT)) j--;
        if (TOKS[j].k == T_LT && j > 0 && gfind(j - 1) >= 0) return 1;
    }
    return 0;
}

/* Visibility (M6.5b), over the resolved program before the generic pass.
 * An item — a top-level fn, struct, or enum — is visible in the file that
 * declares it; `pub` makes it visible program-wide. A name-shaped
 * reference (names_item) from another file to a non-pub item is refused.
 * Every non-pub item of a MODULE is then renamed `__m_<stem>_<name>` at
 * its declaration and every reference inside the module, so two modules'
 * private `helper`s stay two functions in the lowered N. The renaming is a
 * text splice: the generic pass runs on the result and sees the mangled
 * names as ordinary ones (a private generic, or a private type as a type
 * argument, works unchanged). Returns the renamed program, or `prog` itself
 * when nothing needed renaming. */
#define MAXITEMS 1024
static struct { int tok; int owner; int pub; } ITEMS[MAXITEMS];
static int NITEMS;

static char* modules_pass(const char* prog) {
    if (NMOD == 0) return (char*)prog;
    /* The generic declarations are needed to tell a type argument from a
     * comparison (names_item); the generic pass re-collects them after the
     * re-lex, so the table is emptied again before returning. */
    collect_generic_decls();
    collect_generic_fns();
    collect_generic_enums();
    /* Every file's top-level items, with their file and `pub` flag. */
    NITEMS = 0;
    int depth = 0;
    for (int i = 0; i + 1 < NTOK; i++) {
        if (TOKS[i].k == T_LB) depth++;
        else if (TOKS[i].k == T_RB) depth--;
        if (depth != 0) continue;
        if (TOKS[i].k != T_KW_FN && TOKS[i].k != T_KW_STRUCT && TOKS[i].k != T_KW_ENUM) continue;
        if (TOKS[i + 1].k != T_IDENT) continue;
        if (NITEMS >= MAXITEMS) die("%s: too many top-level items", FILENAME);
        ITEMS[NITEMS].tok = i + 1;
        ITEMS[NITEMS].owner = mod_owner(TOKS[i].start);
        ITEMS[NITEMS].pub = i > 0 && TOKS[i - 1].k == T_IDENT && TOKS[i - 1].slen == 3 &&
                            !memcmp(TOKS[i - 1].s, "pub", 3);
        NITEMS++;
    }
    /* References: refuse a cross-file use of a private item; rename a
     * module's own private items. */
    int es[MAXITEMS * 4], ee[MAXITEMS * 4]; char* er[MAXITEMS * 4]; int ne = 0;
    depth = 0;
    for (int t = 0; t + 1 < NTOK; t++) {
        if (TOKS[t].k == T_LB) depth++;
        else if (TOKS[t].k == T_RB) depth--;
        if (TOKS[t].k != T_IDENT) continue;
        int isdecl = t > 0 && (TOKS[t - 1].k == T_KW_FN || TOKS[t - 1].k == T_KW_STRUCT ||
                               TOKS[t - 1].k == T_KW_ENUM);
        if (isdecl && depth != 0) continue;          /* a method: not an item */
        if (!isdecl && !names_item(t)) continue;
        int owner = mod_owner(TOKS[t].start);
        int sid = owner < 0 ? 0 : MODS[owner].id;
        /* Same-named items elsewhere: a private one, an exported one this
         * file may see (its module is used here), or an exported one it may
         * not. */
        int local = -1, foreign = -1, pubk = -1, pubok = 0;
        for (int k = 0; k < NITEMS; k++) {
            if (!tokspan_eq(ITEMS[k].tok, t)) continue;
            int ko = ITEMS[k].owner;
            if (ko == owner) local = k;
            else if (!ITEMS[k].pub) { if (foreign < 0) foreign = k; }
            else if (uses(sid, ko < 0 ? 0 : MODS[ko].id)) pubok = 1;
            else if (pubk < 0) pubk = k;
        }
        if (local < 0) {
            if (isdecl || pubok) continue;
            if (pubk >= 0) {
                int po = ITEMS[pubk].owner;
                die("%s:%d: '%.*s' is declared by %s, which %s does not use (add use \"%s\";)",
                    FILENAME, TOKS[t].line, TOKS[t].slen, TOKS[t].s,
                    po >= 0 ? MODS[po].name : FILENAME,
                    owner < 0 ? FILENAME : MODS[owner].name,
                    po >= 0 ? MODS[po].name : FILENAME);
            }
            if (foreign >= 0) {
                int fo = ITEMS[foreign].owner;
                die("%s:%d: '%.*s' is private to %s (mark it pub to use it here)",
                    FILENAME, TOKS[t].line, TOKS[t].slen, TOKS[t].s,
                    fo >= 0 ? MODS[fo].name : FILENAME);
            }
            continue;
        }
        if (ITEMS[local].pub || owner < 0) continue;  /* exported, or the main file's own */
        if (ne >= MAXITEMS * 4) die("%s: too many private references", FILENAME);
        char* mn = xmalloc(strlen(MODS[owner].name) + (size_t)TOKS[t].slen + 8);
        sprintf(mn, "__m_%s_%.*s", mod_stem(MODS[owner].name), TOKS[t].slen, TOKS[t].s);
        es[ne] = TOKS[t].start; ee[ne] = TOKS[t].end; er[ne] = mn; ne++;
    }
    NGS = 0;
    if (ne == 0) return (char*)prog;
    size_t cap = strlen(prog) + (size_t)ne * 64 + 1, n = 0;
    char* out = xmalloc(cap);
    int prev = 0;
    for (int e = 0; e < ne; e++) {      /* in token order already */
        app(&out, &n, &cap, prog + prev, (size_t)(es[e] - prev));
        app(&out, &n, &cap, er[e], strlen(er[e]));
        prev = ee[e];
    }
    app(&out, &n, &cap, prog + prev, strlen(prog) - (size_t)prev);
    return out;
}

/* `pub` marks an exported item (design §6.2). N has no visibility, so the
 * lowering drops the marker together with the spacing after it. */
static void strip_pub(void) {
    int depth = 0;
    for (int i = 0; i + 1 < NTOK; i++) {
        if (TOKS[i].k == T_LB) depth++;
        else if (TOKS[i].k == T_RB) depth--;
        if (depth != 0 || TOKS[i].k != T_IDENT || TOKS[i].slen != 3 ||
            memcmp(TOKS[i].s, "pub", 3)) continue;
        TK nx = TOKS[i + 1].k;
        if (nx != T_KW_FN && nx != T_KW_STRUCT && nx != T_KW_ENUM && nx != T_KW_IMPL)
            die("%s:%d: pub must precede fn, struct, enum, or impl", FILENAME, TOKS[i].line);
        if (NEDIT >= MAXG + MAXI) die("%s: too many rewrites", FILENAME);
        EDITS[NEDIT].start = TOKS[i].start;
        EDITS[NEDIT].end = TOKS[i + 1].start;
        EDITS[NEDIT].repl = "";
        NEDIT++;
    }
}

/* ------------------------------------------------------------------ */
/* driver                                                             */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv) {
    if (argc != 4 || strcmp(argv[2], "-o") != 0)
        die("usage: nppc <file.npp> -o <out.n>");
    FILENAME = argv[1];

    long sz;
    char* buf = read_file(FILENAME, &sz);
    if (!buf) die("nppc: cannot open '%s'", FILENAME);

    /* Modules first (M6.5): the program is the main file with every `use`
     * inlined. A file with no directives resolves to itself byte-for-byte. */
    char* prog = resolve_file(FILENAME, buf, sz);
    FILENAME = argv[1];

    /* The read side is real: the whole program lexes as the N dialect (a
     * file that fails here would fail identically under ncc), and the token
     * buffer with source spans is what the generic pass rewrites over. */
    lex_text(prog, (long)strlen(prog));

    /* Visibility + private-item renaming (M6.5b), then re-lex the result so
     * the generic pass sees the mangled names as ordinary ones. */
    char* vis = modules_pass(prog);
    if (vis != prog) { prog = vis; lex_text(prog, (long)strlen(prog)); }
    collect_generic_decls();
    collect_generic_fns();
    collect_generic_enums();
    guard_templates();
    collect_instantiations();
    collect_inferred_calls();
    collect_inferred_constructions();
    expand_nested();

    /* Build the edit list: each generic declaration is replaced by the
     * concrete structs its distinct instantiations require (in first-use
     * order), and each use site is replaced by its mangled name. */
    for (int g = 0; g < NGS; g++) {
        char* body = xmalloc(1);
        body[0] = 0;
        int blen = 0, emitted = 0;
        for (int i = 0; i < NINST; i++) {
            if (INSTS[i].gi != g) continue;
            int dup = 0;
            for (int j = 0; j < i; j++)
                if (INSTS[j].gi == g && args_same(&INSTS[i], &INSTS[j])) { dup = 1; break; }
            if (dup) continue;
            char* cs = concrete_item(&INSTS[i]);
            int add = (int)strlen(cs) + 2;
            body = realloc(body, (size_t)blen + (size_t)add + 1);
            if (!body) die("nppc: out of memory");
            blen += sprintf(body + blen, "%s%s", emitted ? "\n\n" : "", cs);
            emitted = 1;
        }
        EDITS[NEDIT].start = GS[g].declstart;
        EDITS[NEDIT].end = GS[g].declend;
        EDITS[NEDIT].repl = body;         /* empty if the generic is never used */
        NEDIT++;
    }
    for (int i = 0; i < NINST; i++) {
        if (INSTS[i].start < 0) continue;  /* a nested instantiation: rewritten by its template's emit */
        EDITS[NEDIT].start = INSTS[i].start;
        EDITS[NEDIT].end = INSTS[i].end;
        EDITS[NEDIT].repl = mangle(&INSTS[i]);
        NEDIT++;
    }
    strip_pub();
    qsort(EDITS, (size_t)NEDIT, sizeof(Edit), edit_cmp);

    /* Splice: verbatim between edits, replacement at each. With no generics
     * there are no edits and the output equals the input byte-for-byte. */
    FILE* out = fopen(argv[3], "wb");
    if (!out) die("nppc: cannot open '%s' for writing", argv[3]);
    int prev = 0;
    for (int e = 0; e < NEDIT; e++) {
        if (EDITS[e].start < prev)
            die("%s: overlapping generic rewrites", FILENAME);
        fwrite(SRC + prev, 1, (size_t)(EDITS[e].start - prev), out);
        fwrite(EDITS[e].repl, 1, strlen(EDITS[e].repl), out);
        prev = EDITS[e].end;
    }
    fwrite(SRC + prev, 1, (size_t)(LEN - prev), out);
    fclose(out);

    fprintf(stderr, "nppc: OK — %d token(s), %d generic(s), %d instantiation(s) -> %s\n",
            NTOK - 1, NGS, NINST, argv[3]);
    return 0;
}
