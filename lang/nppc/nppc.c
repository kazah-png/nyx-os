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

static int tokeq(int i, const char* w) {   /* token i is IDENT with text w */
    return i >= 0 && i < NTOK && TOKS[i].k == T_IDENT && TOKS[i].s &&
           (int)strlen(w) == TOKS[i].slen && !memcmp(w, TOKS[i].s, (size_t)TOKS[i].slen);
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

typedef struct {
    int kind;                             /* 0 = struct, 1 = fn */
    int nametok;                          /* the IDENT token of the generic name */
    int ptok[MAXP]; int nparams;          /* type-param IDENT tokens */
    int declstart, declend;               /* source span of the whole declaration */
    /* struct-only: the field templates */
    struct { int fname; int ptrs; int base; } fields[MAXF];
    int nfields;                          /* base: token index of the field type name */
    /* fn-only: the token bounds the span-splice emit walks */
    int angleclose;                       /* token index of `>` closing the params */
    int lasttok;                          /* token index of the body's closing `}` */
} GStruct;
static GStruct GS[MAXG];
static int NGS;

typedef struct {
    int gi;                               /* which GStruct */
    int atok[MAXP]; int nargs;            /* type-arg IDENT tokens */
    int start, end;                       /* span of `NAME<...>` to rewrite */
} Inst;
static Inst INSTS[MAXI];
static int NINST;

typedef struct { int start, end; char* repl; } Edit;
static Edit EDITS[MAXG + MAXI];
static int NEDIT;

static int gfind(int nametok) {           /* generic index whose name == this IDENT */
    for (int i = 0; i < NGS; i++)
        if (tokspan_eq(GS[i].nametok, nametok)) return i;
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
        if (i > 0 && (TOKS[i - 1].k == T_KW_STRUCT || TOKS[i - 1].k == T_KW_FN))
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
        if (NINST >= MAXI) die("%s: too many generic instantiations", FILENAME);
        Inst* n = &INSTS[NINST++];
        n->gi = gi;
        n->nargs = nargs;
        for (int a = 0; a < nargs; a++) n->atok[a] = atok[a];
        n->start = TOKS[i].start;
        n->end = TOKS[j - 1].end;         /* past the closing '>' */
        i = j - 1;                        /* skip past the args */
    }
}

/* "__g_Box_i64" for Box<i64>. Args are bare type names, so the result is a
 * valid N/C identifier by construction. */
static char* mangle(int gi, int* atok) {
    char* out = xmalloc(256);
    int n = 0;
    n += sprintf(out + n, "__g_%.*s", TOKS[GS[gi].nametok].slen, TOKS[GS[gi].nametok].s);
    for (int a = 0; a < GS[gi].nparams; a++)
        n += sprintf(out + n, "_%.*s", TOKS[atok[a]].slen, TOKS[atok[a]].s);
    return out;
}

/* The concrete N struct for one instantiation: the template body with each
 * type parameter replaced by the matching argument. */
static char* concrete_struct(int gi, int* atok) {
    GStruct* g = &GS[gi];
    char* mn = mangle(gi, atok);
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
            n += sprintf(out + n, "%.*s", TOKS[atok[sub]].slen, TOKS[atok[sub]].s);
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
    if (p != T_COLON && p != T_ARROW && p != T_STAR) return -1;
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
        int depth = 0, body_open = j;
        for (;;) {
            if (TOKS[j].k == T_EOF) die("%s: unterminated generic function body", FILENAME);
            if (TOKS[j].k == T_LB) depth++;
            else if (TOKS[j].k == T_RB) { depth--; if (depth == 0) break; }
            j++;
        }
        g->lasttok = j;                   /* the body's closing `}` */
        g->declend = TOKS[j].end;
        /* Every type-parameter occurrence after the header must be a handled
         * type position, so the emit rewrites all of them. */
        for (int t = g->angleclose + 1; t <= g->lasttok; t++) {
            if (TOKS[t].k != T_IDENT) continue;
            int isp = 0;
            for (int q = 0; q < g->nparams; q++)
                if (tokspan_eq(g->ptok[q], t)) { isp = 1; break; }
            if (!isp) continue;
            if (typaram_at(g, t) < 0) {
                (void)body_open;
                die("%s:%d: M6.3b: type parameter '%.*s' appears in an unsupported "
                    "position (only ':' / '->' / '*' type slots are lowered yet)",
                    FILENAME, TOKS[t].line, TOKS[t].slen, TOKS[t].s);
            }
        }
        NGS++;
    }
}

/* The concrete N function for one instantiation: a span-splice of the source
 * declaration with the header rewritten to the mangled name and every
 * type-parameter type slot replaced by its argument — the body's exact bytes
 * (spacing and comments) pass through untouched. */
static char* concrete_fn(int gi, int* atok) {
    GStruct* g = &GS[gi];
    char* mn = mangle(gi, atok);
    int es[512], ee[512]; char* er[512]; int ne = 0;
    es[ne] = TOKS[g->nametok].start;      /* `NAME<params>` -> mangled name */
    ee[ne] = TOKS[g->angleclose].end;
    er[ne] = mn; ne++;
    for (int t = g->angleclose + 1; t <= g->lasttok; t++) {
        int q = typaram_at(g, t);
        if (q < 0) continue;
        if (ne >= 512) die("%s: generic function too large", FILENAME);
        es[ne] = TOKS[t].start;
        ee[ne] = TOKS[t].end;
        er[ne] = xstrndup(TOKS[atok[q]].s, (size_t)TOKS[atok[q]].slen);
        ne++;
    }
    /* Edits are already in source order (header first, then increasing token
     * index), so splice directly. */
    char* out = xmalloc((size_t)(g->declend - g->declstart) + 512);
    int n = 0, prev = g->declstart;
    for (int e = 0; e < ne; e++) {
        memcpy(out + n, SRC + prev, (size_t)(es[e] - prev)); n += es[e] - prev;
        n += sprintf(out + n, "%s", er[e]);
        prev = ee[e];
    }
    memcpy(out + n, SRC + prev, (size_t)(g->declend - prev)); n += g->declend - prev;
    out[n] = 0;
    return out;
}

static char* concrete_item(int gi, int* atok) {
    return GS[gi].kind == 0 ? concrete_struct(gi, atok) : concrete_fn(gi, atok);
}

static int args_same(Inst* a, Inst* b) {
    if (a->gi != b->gi || a->nargs != b->nargs) return 0;
    for (int i = 0; i < a->nargs; i++)
        if (!tokspan_eq(a->atok[i], b->atok[i])) return 0;
    return 1;
}

static int edit_cmp(const void* x, const void* y) {
    return ((const Edit*)x)->start - ((const Edit*)y)->start;
}

/* ------------------------------------------------------------------ */
/* driver                                                             */
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

    /* The read side is real: the whole file lexes as the N dialect (a file
     * that fails here would fail identically under ncc), and the token
     * buffer with source spans is what the generic pass rewrites over. */
    lex_all();
    collect_generic_decls();
    collect_generic_fns();
    collect_instantiations();

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
            char* cs = concrete_item(g, INSTS[i].atok);
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
        EDITS[NEDIT].start = INSTS[i].start;
        EDITS[NEDIT].end = INSTS[i].end;
        EDITS[NEDIT].repl = mangle(INSTS[i].gi, INSTS[i].atok);
        NEDIT++;
    }
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
