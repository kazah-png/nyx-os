/* ncc — bootstrap compiler for N, the native language of NyxOS.
 * Transpiles .n source to C targeting the nyxrt runtime (user/nyxrt.h).
 * Hosted tool (runs on the dev machine); single file, no dependencies.
 * See lang/docs/spec-n.md for the language specification this implements.
 *
 * Supported subset (currently N v0.10):
 *   - extern syscall { fn name(params) [-> T] = N }
 *   - fn decls with block bodies, params, return types
 *   - statements: let (:=, mut), assignment (= += -=), return, while,
 *     if/else, break, continue, expression statements, block tail-expr
 *   - expressions: int (dec/hex), bool, string literals, string
 *     interpolation "{expr}" (typed: str inserts text, integers decimal),
 *     path, call, field access, unary - !, binary arith/cmp/logic/bit,
 *     `as` casts, parens
 *   - types: primitive names, *T, raw *T
 *   - minimal type inference (v0.2): `x := e` bindings get a concrete C
 *     type from the initializer (integer literals default to i64), `mut`
 *     is enforced, and the generated C is plain C99 (no __auto_type), so
 *     TinyCC — the in-OS compiler — can build it.
 *   - static checks (v0.3 + v0.4, the N++ P1 checker): undeclared variables,
 *     unknown callees, wrong call arity, argument/parameter mismatches,
 *     binary-operand type errors (str in operators, pointer arithmetic,
 *     int/pointer mixing), return-value vs declared return type, and
 *     assignment value vs target type — all compile errors with file:line
 *     diagnostics.
 *   - struct declarations (v0.5, the first N++ P2 construct): named field
 *     records with literal construction `Rect{ w: 1, h: 2 }`, typed field
 *     access/assignment, by-value passing/returning — fully checked
 *     (unknown structs/fields, missing literal fields, mut-through-field).
 *   - enum + match (v0.7): tagged-union sum types with named payload
 *     fields, variant construction `Shape.Circle{ r: 5 }` (payload-less:
 *     `Shape.Empty`), and an exhaustive `match` statement that binds
 *     payload fields positionally — the Result foundation.
 *   - impl methods (v0.8, closing N++ P2): `impl Type { fn m(self, ...) }`
 *     attaches functions to structs and enums; calls dispatch statically
 *     as `expr.m(args)` and lower to plain C functions Type_m(self, ...).
 *     The receiver is by value and immutable.
 *   - match as an expression (v0.9, the P3 groundwork): `match` may appear
 *     as the value of a binding, an assignment, or a return; arms yield
 *     single expressions (`Variant(binds) => expr`), all arms must agree
 *     on one result type, and exhaustiveness is enforced as in the
 *     statement form. This is the shape `Result`/`?` will lower through.
 *   - the `?` operator (v0.10, N++ P3): `x := e?;` / `x = e?;` / `e?;`
 *     over "result enums" — any enum shaped exactly { Ok, Err } with at
 *     most one payload field each. On Err the function returns the error
 *     (rewrapped if the return type is a different result enum, defers
 *     honored); on Ok the payload is bound/assigned/discarded.
 * Not yet (N++ territory): for, closures, generic Result<T,E>, modules/use.
 */
/* Platform includes. In-OS builds (milestone M2) compile this file with
 * NyxOS's ported TinyCC, whose libc surface is the single header libc.h
 * (stdio/stdlib/string/va_* over the fd syscalls) — the same pattern the
 * self-hosted coreutils use. tcc always defines __TINYC__, and NyxOS is the
 * only tcc environment we target, so that macro selects the in-OS build:
 *     cc /mnt/ncc.c -I/usr/src/nyx -o /mnt/bin/ncc
 * Hosted builds (any C99 compiler) take the standard-headers branch. */
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
    if (!p) die("ncc: out of memory");
    return p;
}
static char* xstrndup(const char* s, size_t n) {
    char* p = xmalloc(n + 1);
    memcpy(p, s, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* lexer                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    T_EOF, T_INT, T_STR, T_STR_HEAD, T_STR_MID, T_STR_TAIL, T_INTERP_R,
    T_IDENT,
    T_KW_EXTERN, T_KW_SYSCALL, T_KW_FN, T_KW_MUT, T_KW_RETURN,
    T_KW_IF, T_KW_ELSE, T_KW_WHILE, T_KW_BREAK, T_KW_CONTINUE,
    T_KW_AS, T_KW_RAW, T_KW_TRUE, T_KW_FALSE, T_KW_STRUCT, T_KW_DEFER,
    T_KW_ENUM, T_KW_MATCH, T_FATARROW, T_KW_IMPL,
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
        {"impl", T_KW_IMPL},
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
        case '.': return mk(T_DOT, sl);
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
    }
    die("%s:%d: unexpected character '%c'", FILENAME, sl, c);
    return mk(T_EOF, sl);
}

/* ------------------------------------------------------------------ */
/* parser                                                             */
/* ------------------------------------------------------------------ */
static Tok CUR, AHEAD;
static int HAS_AHEAD;

static Tok padv(void) {
    Tok p = CUR;
    if (HAS_AHEAD) { CUR = AHEAD; HAS_AHEAD = 0; }
    else CUR = next_token();
    return p;
}
static Tok ppeek2(void) {
    if (!HAS_AHEAD) { AHEAD = next_token(); HAS_AHEAD = 1; }
    return AHEAD;
}
static int pchk(TK k) { return CUR.k == k; }
static int pacc(TK k) { if (pchk(k)) { padv(); return 1; } return 0; }
static Tok pexp(TK k, const char* what) {
    if (!pchk(k)) die("%s:%d: expected %s", FILENAME, CUR.line, what);
    return padv();
}

/* --- AST --- */
typedef struct { char* name; int ptrs; } Ty;   /* name==NULL means "no type" (void) */

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;

typedef enum { E_INT, E_BOOL, E_STR, E_INTERP, E_PATH, E_CALL, E_FIELD,
               E_UN, E_BIN, E_CAST, E_SLIT, E_ELIT } EK;

typedef struct { char* text; int tlen; Expr* e; } Frag;

struct Expr {
    EK k;
    int line;
    long long ival;
    char* s; int slen;
    Frag* frags; int nfrags; int iid;
    char* name;
    Expr* callee; Expr** args; int nargs;   /* E_SLIT/E_ELIT reuse args as values */
    char** snames;                          /* E_SLIT/E_ELIT: field name per value */
    Expr* base; char* field;                /* E_ELIT: name = enum, field = variant */
    const char* op; Expr* l; Expr* r;
    Ty cast_to;
};

typedef enum { S_LET, S_ASSIGN, S_RET, S_EXPR, S_WHILE, S_IF, S_BREAK, S_CONT,
               S_DEFER, S_MATCH } SK;

typedef struct {
    char* variant;
    char** binds; int nbinds;   /* positional: bind i <- payload field i */
    struct Block* body;         /* statement form: arm body is a block */
    Expr* val;                  /* expression form (v0.9): arm yields this value */
    int line;
} MatchArm;

/* Where a v0.9 match-expression delivers its value. MT_STMT is the plain
 * statement form (arm bodies are blocks, no value). The expression form is
 * deliberately restricted to these three positions — each has a single,
 * obvious lowering into strict C99 (see gen_stmt), which keeps the
 * generated C readable and needs no statement-expressions. */
typedef enum { MT_STMT, MT_LET, MT_ASSIGN, MT_RET } MTarget;
struct Stmt {
    SK k;
    int line;
    int is_mut; char* name; Expr* e;
    Expr* lhs; const char* aop;
    Expr* cond; Block* body; Block* els;
    MatchArm* arms; int narms;              /* S_MATCH (subject in e) */
    int mtarget;                            /* S_MATCH: an MTarget value */
    int is_try;                             /* v0.10: statement value ends in `?` */
};
struct Block { Stmt** st; int n; Expr* tail; };

typedef struct { char* name; Ty ty; } Param;
typedef struct { char* name; Param* ps; int np; Ty ret; long long num; } XFn;
typedef struct { char* name; Param* ps; int np; Ty ret; Block* body; } Fn;

static XFn XFNS[64]; static int NXFN;
static Fn  FNS[64];  static int NFN;

/* struct declarations (v0.5): named field records, C-struct layout */
typedef struct { char* name; Param* fs; int nf; } StructDef;
static StructDef STRUCTS[32]; static int NSTRUCTS;

static StructDef* struct_lookup(const char* name) {
    for (int i = 0; i < NSTRUCTS; i++)
        if (!strcmp(STRUCTS[i].name, name)) return &STRUCTS[i];
    return NULL;
}

/* enum declarations (v0.7): tagged unions. Each variant has an optional
 * named-field payload; the C lowering is { int tag; union { ... } u; }. */
typedef struct { char* name; Param* fs; int nf; } VariantDef;
typedef struct { char* name; VariantDef* vs; int nv; } EnumDef;
static EnumDef ENUMS[16]; static int NENUMS;

static EnumDef* enum_lookup(const char* name) {
    for (int i = 0; i < NENUMS; i++)
        if (!strcmp(ENUMS[i].name, name)) return &ENUMS[i];
    return NULL;
}
static int variant_index(EnumDef* ed, const char* v) {
    for (int i = 0; i < ed->nv; i++)
        if (!strcmp(ed->vs[i].name, v)) return i;
    return -1;
}
/* A "result enum" (v0.10, spec 5.9) is the structural shape `?` operates
 * on: exactly the two variants Ok and Err, each carrying zero or one
 * payload field. Any enum matching the shape qualifies — N has no
 * generics yet, so Result<T, E> is a convention, not a type former. */
static int enum_is_result(EnumDef* ed) {
    if (ed->nv != 2) return 0;
    if (variant_index(ed, "Ok") < 0 || variant_index(ed, "Err") < 0) return 0;
    return ed->vs[0].nf <= 1 && ed->vs[1].nf <= 1;
}
/* Is this expression a payload-less variant reference (`Shape.Empty`)?
 * Returns the enum and sets *idx, or NULL. */
static EnumDef* variant_ref(Expr* e, int* idx) {
    if (e->k != E_FIELD || e->base->k != E_PATH) return NULL;
    EnumDef* ed = enum_lookup(e->base->name);
    if (!ed) return NULL;
    *idx = variant_index(ed, e->field);
    return ed;
}

/* impl methods (v0.8): functions attached to a struct or enum. Dispatch is
 * static — the receiver's inferred type picks the method at compile time;
 * the lowering is a plain C function Type_name(Type self, ...). */
typedef struct { char* type; char* name; Param* ps; int np; Ty ret; Block* body; } Method;
static Method METHODS[64]; static int NMETHODS;

static Method* method_lookup(const char* type, const char* name) {
    for (int i = 0; i < NMETHODS; i++)
        if (!strcmp(METHODS[i].type, type) && !strcmp(METHODS[i].name, name))
            return &METHODS[i];
    return NULL;
}

/* Struct literals `Name{ ... }` are valid anywhere EXCEPT directly in an
 * if/while condition, where `{` must open the body block instead (the same
 * rule Rust uses). The parser sets this flag around condition parsing. */
static int NO_SLIT;

static Expr* newe(EK k) { Expr* e = xmalloc(sizeof(Expr)); e->k = k; e->line = CUR.line; return e; }
static Stmt* news(SK k) { Stmt* s = xmalloc(sizeof(Stmt)); s->k = k; s->line = CUR.line; return s; }

static Ty parse_type(void) {
    Ty t; t.ptrs = 0; t.name = NULL;
    pacc(T_KW_RAW);                       /* raw *T and *T lower identically in C */
    while (pacc(T_STAR)) t.ptrs++;
    Tok id = pexp(T_IDENT, "type name");
    t.name = id.s;
    return t;
}

static Expr* parse_expr(void);

static Expr* parse_interp(void) {
    Expr* e = newe(E_INTERP);
    Frag* fr = xmalloc(sizeof(Frag) * 64);
    int nf = 0;
    Tok h = padv();                       /* T_STR_HEAD */
    fr[nf].text = h.s; fr[nf].tlen = h.slen; fr[nf].e = NULL; nf++;
    for (;;) {
        if (nf + 2 >= 64) die("%s:%d: too many interpolations", FILENAME, e->line);
        fr[nf].text = NULL; fr[nf].tlen = 0; fr[nf].e = parse_expr(); nf++;
        pexp(T_INTERP_R, "'}' closing interpolation");
        if (pchk(T_STR_MID)) {
            Tok m = padv();
            fr[nf].text = m.s; fr[nf].tlen = m.slen; fr[nf].e = NULL; nf++;
            continue;
        }
        if (pchk(T_STR_TAIL)) {
            Tok t2 = padv();
            fr[nf].text = t2.s; fr[nf].tlen = t2.slen; fr[nf].e = NULL; nf++;
            break;
        }
        die("%s:%d: expected string continuation after interpolation", FILENAME, CUR.line);
    }
    e->frags = fr; e->nfrags = nf;
    return e;
}

static Expr* parse_primary(void) {
    if (pchk(T_INT))  { Tok t = padv(); Expr* e = newe(E_INT);  e->ival = t.ival; return e; }
    if (pchk(T_KW_TRUE) || pchk(T_KW_FALSE)) {
        Expr* e = newe(E_BOOL);
        e->ival = pchk(T_KW_TRUE);
        padv();
        return e;
    }
    if (pchk(T_STR))  { Tok t = padv(); Expr* e = newe(E_STR);  e->s = t.s; e->slen = t.slen; return e; }
    if (pchk(T_STR_HEAD)) return parse_interp();
    if (pchk(T_LP)) {                 /* parens re-enable struct/enum literals
                                       * inside if/while/match headers (v0.9,
                                       * the same escape hatch Rust uses) */
        padv();
        int slit_save = NO_SLIT;
        NO_SLIT = 0;
        Expr* e = parse_expr();
        NO_SLIT = slit_save;
        pexp(T_RP, "')'");
        return e;
    }
    if (pchk(T_IDENT)) {
        Tok t = padv();
        if (pchk(T_LB) && !NO_SLIT) {           /* struct literal: Name{ f: e, ... } */
            padv();
            Expr* e = newe(E_SLIT);
            e->name = t.s;
            e->args = xmalloc(sizeof(Expr*) * 16);
            e->snames = xmalloc(sizeof(char*) * 16);
            e->nargs = 0;
            if (!pchk(T_RB)) {
                do {
                    if (pchk(T_RB)) break;      /* trailing comma */
                    if (e->nargs >= 16) die("%s:%d: too many fields in literal", FILENAME, CUR.line);
                    Tok f = pexp(T_IDENT, "field name");
                    pexp(T_COLON, "':' after field name");
                    e->snames[e->nargs] = f.s;
                    e->args[e->nargs] = parse_expr();
                    e->nargs++;
                } while (pacc(T_COMMA));
            }
            pexp(T_RB, "'}' closing struct literal");
            return e;
        }
        Expr* e = newe(E_PATH);
        e->name = t.s;
        return e;
    }
    die("%s:%d: expected an expression", FILENAME, CUR.line);
    return NULL;
}

static Expr* parse_postfix(void) {
    Expr* e = parse_primary();
    for (;;) {
        if (pchk(T_DOT)) {
            padv();
            Tok f = pexp(T_IDENT, "field name");
            if (pchk(T_LP)) {                   /* method call: recv.m(args) */
                padv();
                Expr* fe = newe(E_FIELD);
                fe->base = e; fe->field = f.s;
                Expr* c = newe(E_CALL);
                c->callee = fe;
                Expr** as = xmalloc(sizeof(Expr*) * 16);
                int na = 0;
                if (!pchk(T_RP)) {
                    do {
                        if (na >= 16) die("%s:%d: too many call arguments", FILENAME, CUR.line);
                        as[na++] = parse_expr();
                    } while (pacc(T_COMMA));
                }
                pexp(T_RP, "')'");
                c->args = as; c->nargs = na;
                e = c;
                continue;
            }
            if (pchk(T_LB) && !NO_SLIT && e->k == E_PATH) {
                /* enum-variant literal: Enum.Variant{ field: value, ... } */
                padv();
                Expr* v = newe(E_ELIT);
                v->name = e->name;              /* enum */
                v->field = f.s;                 /* variant */
                v->args = xmalloc(sizeof(Expr*) * 16);
                v->snames = xmalloc(sizeof(char*) * 16);
                v->nargs = 0;
                if (!pchk(T_RB)) {
                    do {
                        if (pchk(T_RB)) break;
                        if (v->nargs >= 16) die("%s:%d: too many fields", FILENAME, CUR.line);
                        Tok fn_ = pexp(T_IDENT, "field name");
                        pexp(T_COLON, "':' after field name");
                        v->snames[v->nargs] = fn_.s;
                        v->args[v->nargs] = parse_expr();
                        v->nargs++;
                    } while (pacc(T_COMMA));
                }
                pexp(T_RB, "'}' closing variant literal");
                e = v;
                continue;
            }
            Expr* fe = newe(E_FIELD);
            fe->base = e; fe->field = f.s;
            e = fe;
            continue;
        }
        if (pchk(T_LP)) {
            padv();
            Expr* c = newe(E_CALL);
            c->callee = e;
            Expr** as = xmalloc(sizeof(Expr*) * 16);
            int na = 0;
            if (!pchk(T_RP)) {
                do {
                    if (na >= 16) die("%s:%d: too many call arguments", FILENAME, CUR.line);
                    as[na++] = parse_expr();
                } while (pacc(T_COMMA));
            }
            pexp(T_RP, "')'");
            c->args = as; c->nargs = na;
            e = c;
            continue;
        }
        break;
    }
    return e;
}

static Expr* parse_unary(void) {
    if (pchk(T_MINUS) || pchk(T_NOT)) {
        const char* op = pchk(T_MINUS) ? "-" : "!";
        padv();
        Expr* e = newe(E_UN);
        e->op = op; e->r = parse_unary();
        return e;
    }
    return parse_postfix();
}

static Expr* parse_cast(void) {
    Expr* e = parse_unary();
    while (pacc(T_KW_AS)) {
        Expr* c = newe(E_CAST);
        c->l = e; c->cast_to = parse_type();
        e = c;
    }
    return e;
}

static Expr* mkbin(const char* op, Expr* l, Expr* r) {
    Expr* e = newe(E_BIN);
    e->op = op; e->l = l; e->r = r;
    return e;
}

static Expr* parse_mul(void) {
    Expr* l = parse_cast();
    for (;;) {
        const char* op = pchk(T_STAR) ? "*" : pchk(T_SLASH) ? "/" : pchk(T_PERCENT) ? "%" : NULL;
        if (!op) return l;
        padv();
        l = mkbin(op, l, parse_cast());
    }
}
static Expr* parse_add(void) {
    Expr* l = parse_mul();
    for (;;) {
        const char* op = pchk(T_PLUS) ? "+" : pchk(T_MINUS) ? "-" : NULL;
        if (!op) return l;
        padv();
        l = mkbin(op, l, parse_mul());
    }
}
static Expr* parse_shift(void) {
    Expr* l = parse_add();
    for (;;) {
        const char* op = pchk(T_SHL) ? "<<" : pchk(T_SHR) ? ">>" : NULL;
        if (!op) return l;
        padv();
        l = mkbin(op, l, parse_add());
    }
}
static Expr* parse_band(void) {
    Expr* l = parse_shift();
    while (pchk(T_AMP)) { padv(); l = mkbin("&", l, parse_shift()); }
    return l;
}
static Expr* parse_bxor(void) {
    Expr* l = parse_band();
    while (pchk(T_CARET)) { padv(); l = mkbin("^", l, parse_band()); }
    return l;
}
static Expr* parse_bor(void) {
    Expr* l = parse_bxor();
    while (pchk(T_PIPE)) { padv(); l = mkbin("|", l, parse_bxor()); }
    return l;
}
static Expr* parse_cmp(void) {
    Expr* l = parse_bor();
    for (;;) {
        const char* op = pchk(T_EQ) ? "==" : pchk(T_NE) ? "!=" :
                         pchk(T_LE) ? "<=" : pchk(T_GE) ? ">=" :
                         pchk(T_LT) ? "<"  : pchk(T_GT) ? ">"  : NULL;
        if (!op) return l;
        padv();
        l = mkbin(op, l, parse_bor());
    }
}
static Expr* parse_and(void) {
    Expr* l = parse_cmp();
    while (pchk(T_ANDAND)) { padv(); l = mkbin("&&", l, parse_cmp()); }
    return l;
}
static Expr* parse_or(void) {
    Expr* l = parse_and();
    while (pchk(T_OROR)) { padv(); l = mkbin("||", l, parse_and()); }
    return l;
}
static Expr* parse_expr(void) { return parse_or(); }

/* --- statements / blocks --- */
static Block* parse_block(void);

static Stmt* parse_if_stmt(void) {
    padv();                               /* 'if' */
    Stmt* s = news(S_IF);
    NO_SLIT = 1;                          /* `{` after the condition opens the body */
    s->cond = parse_expr();
    NO_SLIT = 0;
    s->body = parse_block();
    if (pacc(T_KW_ELSE)) {
        if (pchk(T_KW_IF)) {              /* else-if: wrap in a one-stmt block */
            Block* b = xmalloc(sizeof(Block));
            b->st = xmalloc(sizeof(Stmt*));
            b->st[0] = parse_if_stmt();
            b->n = 1; b->tail = NULL;
            s->els = b;
        } else {
            s->els = parse_block();
        }
    }
    return s;
}

/* Parse `match <subject> { arms }` with the `match` keyword already
 * consumed. Shared by the statement form (expr_arms == 0, arm bodies are
 * blocks) and the v0.9 expression form (expr_arms == 1, each arm yields a
 * single expression). The caller sets s->mtarget and the target fields. */
static Stmt* parse_match(int expr_arms) {
    Stmt* s = news(S_MATCH);
    NO_SLIT = 1;                  /* `{` after the subject opens the arms */
    s->e = parse_expr();
    NO_SLIT = 0;
    pexp(T_LB, "'{' opening match arms");
    MatchArm* arms = xmalloc(sizeof(MatchArm) * 16);
    int na = 0;
    while (!pchk(T_RB)) {
        if (pchk(T_EOF)) die("%s: unexpected EOF in match", FILENAME);
        if (na >= 16) die("%s:%d: too many match arms", FILENAME, CUR.line);
        MatchArm* a = &arms[na];
        a->line = CUR.line;
        Tok v = pexp(T_IDENT, "variant name");
        a->variant = v.s;
        a->binds = xmalloc(sizeof(char*) * 8);
        a->nbinds = 0;
        if (pacc(T_LP)) {
            do {
                if (a->nbinds >= 8) die("%s:%d: too many binds", FILENAME, CUR.line);
                a->binds[a->nbinds++] = pexp(T_IDENT, "binding name").s;
            } while (pacc(T_COMMA));
            pexp(T_RP, "')'");
        }
        pexp(T_FATARROW, "'=>' after the pattern");
        if (expr_arms) a->val = parse_expr();
        else           a->body = parse_block();
        na++;
        if (!pacc(T_COMMA)) break;
    }
    pexp(T_RB, "'}' closing match");
    s->arms = arms;
    s->narms = na;
    return s;
}

static Block* parse_block(void) {
    pexp(T_LB, "'{'");
    Block* b = xmalloc(sizeof(Block));
    Stmt** st = xmalloc(sizeof(Stmt*) * 256);
    int n = 0;
    while (!pchk(T_RB)) {
        if (pchk(T_EOF)) die("%s:%d: unexpected EOF inside block", FILENAME, CUR.line);
        if (n >= 256) die("%s:%d: too many statements in block", FILENAME, CUR.line);

        if (pchk(T_KW_MUT) || (pchk(T_IDENT) && ppeek2().k == T_WALRUS)) {
            int is_mut = pacc(T_KW_MUT);
            Tok id = pexp(T_IDENT, "variable name");
            pexp(T_WALRUS, "':='");
            if (pchk(T_KW_MATCH)) {       /* v0.9: x := match subj { ... }; */
                padv();
                Stmt* m = parse_match(1);
                m->mtarget = MT_LET;
                m->name = id.s;
                m->is_mut = is_mut;
                pexp(T_SEMI, "';'");
                st[n++] = m;
                continue;
            }
            Stmt* s = news(S_LET);
            s->is_mut = is_mut;
            s->name = id.s;
            s->e = parse_expr();
            s->is_try = pacc(T_QUESTION);     /* v0.10: x := e?; */
            pexp(T_SEMI, "';'");
            st[n++] = s;
            continue;
        }
        if (pacc(T_KW_RETURN)) {
            if (pchk(T_KW_MATCH)) {       /* v0.9: return match subj { ... }; */
                padv();
                Stmt* m = parse_match(1);
                m->mtarget = MT_RET;
                pexp(T_SEMI, "';'");
                st[n++] = m;
                continue;
            }
            Stmt* s = news(S_RET);
            if (!pchk(T_SEMI)) s->e = parse_expr();
            pexp(T_SEMI, "';'");
            st[n++] = s;
            continue;
        }
        if (pacc(T_KW_BREAK))    { pexp(T_SEMI, "';'"); st[n++] = news(S_BREAK); continue; }
        if (pacc(T_KW_CONTINUE)) { pexp(T_SEMI, "';'"); st[n++] = news(S_CONT);  continue; }
        if (pacc(T_KW_DEFER)) {
            Stmt* s = news(S_DEFER);
            s->e = parse_expr();
            pexp(T_SEMI, "';'");
            st[n++] = s;
            continue;
        }
        if (pchk(T_KW_WHILE)) {
            padv();
            Stmt* s = news(S_WHILE);
            NO_SLIT = 1;                  /* `{` after the condition opens the body */
            s->cond = parse_expr();
            NO_SLIT = 0;
            s->body = parse_block();
            st[n++] = s;
            continue;
        }
        if (pchk(T_KW_IF)) { st[n++] = parse_if_stmt(); continue; }
        if (pchk(T_KW_MATCH)) {
            padv();
            st[n++] = parse_match(0);     /* statement form (mtarget = MT_STMT) */
            continue;
        }

        /* expression, assignment, or block tail */
        Expr* e = parse_expr();
        if (pchk(T_ASSIGN) || pchk(T_PLUSEQ) || pchk(T_MINUSEQ)) {
            const char* op = pchk(T_ASSIGN) ? "=" : pchk(T_PLUSEQ) ? "+=" : "-=";
            padv();
            if (pchk(T_KW_MATCH)) {       /* v0.9: x = match subj { ... }; */
                padv();
                Stmt* m = parse_match(1);
                m->mtarget = MT_ASSIGN;
                m->lhs = e;
                m->aop = op;
                pexp(T_SEMI, "';'");
                st[n++] = m;
                continue;
            }
            Stmt* s = news(S_ASSIGN);
            s->lhs = e; s->aop = op; s->e = parse_expr();
            s->is_try = pacc(T_QUESTION);     /* v0.10: x = e?; */
            pexp(T_SEMI, "';'");
            st[n++] = s;
            continue;
        }
        if (pacc(T_QUESTION)) {               /* v0.10: e?; — propagate only */
            Stmt* s = news(S_EXPR);
            s->e = e;
            s->is_try = 1;
            pexp(T_SEMI, "';'");
            st[n++] = s;
            continue;
        }
        if (pacc(T_SEMI)) {
            Stmt* s = news(S_EXPR);
            s->e = e;
            st[n++] = s;
            continue;
        }
        if (pchk(T_RB)) { b->tail = e; break; }
        die("%s:%d: expected ';' or '}' after expression", FILENAME, CUR.line);
    }
    pexp(T_RB, "'}'");
    b->st = st; b->n = n;
    return b;
}

/* --- items --- */
static Param* parse_params(int* out_n) {
    pexp(T_LP, "'('");
    Param* ps = xmalloc(sizeof(Param) * 16);
    int np = 0;
    if (!pchk(T_RP)) {
        do {
            if (np >= 16) die("%s:%d: too many parameters", FILENAME, CUR.line);
            Tok id = pexp(T_IDENT, "parameter name");
            pexp(T_COLON, "':'");
            ps[np].name = id.s;
            ps[np].ty = parse_type();
            np++;
        } while (pacc(T_COMMA));
    }
    pexp(T_RP, "')'");
    *out_n = np;
    return ps;
}

static void parse_extern_block(void) {
    pexp(T_KW_SYSCALL, "'syscall'");
    pexp(T_LB, "'{'");
    while (pacc(T_KW_FN)) {
        if (NXFN >= 64) die("too many extern syscalls");
        XFn* x = &XFNS[NXFN++];
        Tok id = pexp(T_IDENT, "syscall name");
        x->name = id.s;
        x->ps = parse_params(&x->np);
        x->ret.name = NULL; x->ret.ptrs = 0;
        if (pacc(T_ARROW)) x->ret = parse_type();
        pexp(T_ASSIGN, "'=' before the syscall number");
        Tok num = pexp(T_INT, "syscall number");
        x->num = num.ival;
        pacc(T_SEMI);                     /* trailing ';' is optional here */
    }
    pexp(T_RB, "'}'");
}

static void parse_fn(void) {
    if (NFN >= 64) die("too many functions");
    Fn* f = &FNS[NFN++];
    Tok id = pexp(T_IDENT, "function name");
    f->name = id.s;
    f->ps = parse_params(&f->np);
    f->ret.name = NULL; f->ret.ptrs = 0;
    if (pacc(T_ARROW)) f->ret = parse_type();
    f->body = parse_block();
}

static void parse_struct(void) {
    if (NSTRUCTS >= 32) die("too many structs");
    StructDef* sd = &STRUCTS[NSTRUCTS++];
    Tok id = pexp(T_IDENT, "struct name");
    sd->name = id.s;
    pexp(T_LB, "'{'");
    Param* fs = xmalloc(sizeof(Param) * 16);
    int nf = 0;
    while (!pchk(T_RB)) {
        if (pchk(T_EOF)) die("%s: unexpected EOF in struct '%s'", FILENAME, sd->name);
        if (nf >= 16) die("%s: too many fields in struct '%s'", FILENAME, sd->name);
        Tok f = pexp(T_IDENT, "field name");
        pexp(T_COLON, "':' after field name");
        fs[nf].name = f.s;
        fs[nf].ty = parse_type();
        nf++;
        if (!pacc(T_COMMA)) break;
    }
    pexp(T_RB, "'}' closing struct");
    sd->fs = fs;
    sd->nf = nf;
    if (nf == 0) die("%s: struct '%s' has no fields", FILENAME, sd->name);
}

static void parse_enum(void) {
    if (NENUMS >= 16) die("too many enums");
    EnumDef* ed = &ENUMS[NENUMS++];
    Tok id = pexp(T_IDENT, "enum name");
    ed->name = id.s;
    pexp(T_LB, "'{'");
    VariantDef* vs = xmalloc(sizeof(VariantDef) * 16);
    int nv = 0;
    while (!pchk(T_RB)) {
        if (pchk(T_EOF)) die("%s: unexpected EOF in enum '%s'", FILENAME, ed->name);
        if (nv >= 16) die("%s: too many variants in enum '%s'", FILENAME, ed->name);
        Tok v = pexp(T_IDENT, "variant name");
        vs[nv].name = v.s;
        vs[nv].fs = NULL;
        vs[nv].nf = 0;
        if (pacc(T_LP)) {                        /* optional named-field payload */
            Param* fs = xmalloc(sizeof(Param) * 8);
            int nf = 0;
            do {
                if (nf >= 8) die("%s: too many payload fields", FILENAME);
                Tok f = pexp(T_IDENT, "payload field name");
                pexp(T_COLON, "':'");
                fs[nf].name = f.s;
                fs[nf].ty = parse_type();
                nf++;
            } while (pacc(T_COMMA));
            pexp(T_RP, "')'");
            vs[nv].fs = fs;
            vs[nv].nf = nf;
        }
        nv++;
        if (!pacc(T_COMMA)) break;
    }
    pexp(T_RB, "'}' closing enum");
    ed->vs = vs;
    ed->nv = nv;
    if (nv == 0) die("%s: enum '%s' has no variants", FILENAME, ed->name);
}

static void parse_impl(void) {
    Tok id = pexp(T_IDENT, "type name after 'impl'");
    pexp(T_LB, "'{'");
    while (pacc(T_KW_FN)) {
        if (NMETHODS >= 64) die("too many methods");
        Method* m = &METHODS[NMETHODS++];
        m->type = id.s;
        Tok mn = pexp(T_IDENT, "method name");
        m->name = mn.s;
        pexp(T_LP, "'('");
        Tok self_ = pexp(T_IDENT, "'self' as the first parameter");
        if (strcmp(self_.s, "self") != 0)
            die("%s:%d: the first parameter of a method must be 'self'", FILENAME, self_.line);
        Param* ps = xmalloc(sizeof(Param) * 16);
        int np = 0;
        while (pacc(T_COMMA)) {
            if (np >= 16) die("%s: too many parameters in method '%s'", FILENAME, m->name);
            Tok pn = pexp(T_IDENT, "parameter name");
            pexp(T_COLON, "':'");
            ps[np].name = pn.s;
            ps[np].ty = parse_type();
            np++;
        }
        pexp(T_RP, "')'");
        m->ps = ps; m->np = np;
        m->ret.name = NULL; m->ret.ptrs = 0;
        if (pacc(T_ARROW)) m->ret = parse_type();
        m->body = parse_block();
    }
    pexp(T_RB, "'}' closing impl");
}

static void parse_program(void) {
    for (;;) {
        if (pchk(T_EOF)) return;
        if (pacc(T_KW_EXTERN)) { parse_extern_block(); continue; }
        if (pacc(T_KW_FN))     { parse_fn(); continue; }
        if (pacc(T_KW_STRUCT)) { parse_struct(); continue; }
        if (pacc(T_KW_ENUM))   { parse_enum(); continue; }
        if (pacc(T_KW_IMPL))   { parse_impl(); continue; }
        die("%s:%d: expected 'extern', 'fn', 'struct', 'enum' or 'impl' at top level",
            FILENAME, CUR.line);
    }
}

/* ------------------------------------------------------------------ */
/* codegen                                                            */
/* ------------------------------------------------------------------ */
static FILE* OUT;
static int IID;                            /* per-function interp counter */
static Ty CUR_RET;

/* defer (v0.6): function-scoped, Go-style. Deferred expressions register in
 * textual order and run in LIFO order on EVERY exit path — each early
 * `return`, the body's tail expression, and (for void functions) falling
 * off the end. Restricted to the function's outermost block so that a
 * static lowering is exact (no runtime defer stack needed). */
static Expr* FN_DEFERS[16];
static int   NDEFERS;
static int   BLOCK_DEPTH;                  /* 1 = the function's own block */

static const char* base_ctype(const char* n) {
    static const struct { const char* a; const char* b; } M[] = {
        {"i8", "nyx_i8"}, {"u8", "nyx_u8"}, {"i16", "nyx_i16"}, {"u16", "nyx_u16"},
        {"i32", "nyx_i32"}, {"u32", "nyx_u32"}, {"i64", "nyx_i64"}, {"u64", "nyx_u64"},
        {"isize", "nyx_isize"}, {"usize", "nyx_usize"}, {"addr", "nyx_addr"},
        {"bool", "nyx_bool"}, {"str", "nyx_str"}, {"never", "void"}, {"void", "void"},
    };
    for (size_t i = 0; i < sizeof(M) / sizeof(M[0]); i++)
        if (!strcmp(n, M[i].a)) return M[i].b;
    return n;                             /* user types pass through */
}
static void emit_type(Ty t) {
    if (!t.name) { fputs("void", OUT); return; }
    fputs(base_ctype(t.name), OUT);
    for (int i = 0; i < t.ptrs; i++) fputc('*', OUT);
}
static int is_never(Ty t) { return t.name && !strcmp(t.name, "never"); }

/* --- minimal type inference (v0.2) ------------------------------------ */
/* A flat per-function symbol table with block scoping via save/restore of
 * the counter (gen_block records NVARS on entry and truncates on exit).
 * This is deliberately not a full checker: it exists to give `:=` bindings
 * a concrete C type, to dispatch interpolation by type, and to enforce
 * `mut` — the three things codegen cannot do blind. The full checker is
 * N++ P1 (lang/docs/design-npp.md). */
typedef struct { char* name; Ty ty; int is_mut; } VarInfo;
static VarInfo VARS[256];
static int NVARS;

static void vars_add(char* name, Ty ty, int is_mut) {
    if (NVARS >= 256) die("ncc: too many locals in one function");
    VARS[NVARS].name = name;
    VARS[NVARS].ty = ty;
    VARS[NVARS].is_mut = is_mut;
    NVARS++;
}
static VarInfo* vars_find(const char* name) {
    for (int i = NVARS - 1; i >= 0; i--)        /* innermost shadows */
        if (!strcmp(VARS[i].name, name)) return &VARS[i];
    return NULL;
}

static Ty ty_named(const char* n) { Ty t; t.name = (char*)n; t.ptrs = 0; return t; }
static Ty ty_ptr_u8(void)         { Ty t; t.name = (char*)"u8"; t.ptrs = 1; return t; }
static int ty_is(Ty t, const char* n) { return t.name && t.ptrs == 0 && !strcmp(t.name, n); }

/* Look up a named function's signature (extern syscalls and user fns are all
 * parsed before codegen runs, so forward references resolve). Returns 1 and
 * fills the out-params on success, 0 for an unknown name. */
static int fn_lookup(const char* name, Param** ps, int* np, Ty* ret) {
    for (int i = 0; i < NXFN; i++)
        if (!strcmp(XFNS[i].name, name)) {
            *ps = XFNS[i].ps; *np = XFNS[i].np; *ret = XFNS[i].ret;
            return 1;
        }
    for (int i = 0; i < NFN; i++)
        if (!strcmp(FNS[i].name, name)) {
            *ps = FNS[i].ps; *np = FNS[i].np; *ret = FNS[i].ret;
            return 1;
        }
    return 0;
}

static Ty ret_type_of(const char* name) {
    Param* ps; int np; Ty ret;
    if (fn_lookup(name, &ps, &np, &ret)) return ret;
    return ty_named("i64");    /* soft fallback; check_expr rejects unknowns */
}

/* Infer the N type of an expression. Integer literals default to i64 (the
 * language rule); comparisons and logic yield bool; `as` is authoritative. */
static Ty infer_type(Expr* e) {
    switch (e->k) {
        case E_INT:    return ty_named("i64");
        case E_BOOL:   return ty_named("bool");
        case E_STR:
        case E_INTERP: return ty_named("str");
        case E_PATH: {
            VarInfo* v = vars_find(e->name);
            return v ? v->ty : ty_named("i64");
        }
        case E_CALL:
            if (e->callee->k == E_PATH) return ret_type_of(e->callee->name);
            if (e->callee->k == E_FIELD) {      /* method: type from the receiver */
                Ty rt = infer_type(e->callee->base);
                if (rt.name && rt.ptrs == 0) {
                    Method* m = method_lookup(rt.name, e->callee->field);
                    if (m) return m->ret;
                }
            }
            return ty_named("i64");
        case E_FIELD: {
            int vi;
            EnumDef* ed = variant_ref(e, &vi);  /* Shape.Empty: a variant, not a field */
            if (ed) return ty_named(ed->name);
            Ty b = infer_type(e->base);
            if (ty_is(b, "str")) {
                if (!strcmp(e->field, "ptr")) return ty_ptr_u8();
                if (!strcmp(e->field, "len")) return ty_named("u64");
            }
            if (b.name && b.ptrs == 0) {        /* struct field access */
                StructDef* sd = struct_lookup(b.name);
                if (sd)
                    for (int i = 0; i < sd->nf; i++)
                        if (!strcmp(sd->fs[i].name, e->field)) return sd->fs[i].ty;
            }
            return ty_named("i64");
        }
        case E_SLIT: return ty_named(e->name);
        case E_ELIT: return ty_named(e->name);
        case E_UN:
            if (e->op[0] == '!') return ty_named("bool");
            return infer_type(e->r);
        case E_BIN: {
            const char* o = e->op;
            if (!strcmp(o, "==") || !strcmp(o, "!=") || !strcmp(o, "<") ||
                !strcmp(o, "<=") || !strcmp(o, ">")  || !strcmp(o, ">=") ||
                !strcmp(o, "&&") || !strcmp(o, "||"))
                return ty_named("bool");
            return infer_type(e->l);            /* arithmetic: left operand rules */
        }
        case E_CAST:   return e->cast_to;
    }
    return ty_named("i64");
}

/* --- static checks (v0.3): the first N++ P1 slice ---------------------- */
/* Render a type for diagnostics: "i64", "*u8", "raw"-ness is not tracked. */
static const char* ty_str(Ty t) {
    static char buf[2][40];
    static int flip;
    char* b = buf[flip ^= 1];
    int n = 0;
    for (int i = 0; i < t.ptrs && n < 8; i++) b[n++] = '*';
    const char* nm = t.name ? t.name : "void";
    while (*nm && n < 38) b[n++] = *nm++;
    b[n] = '\0';
    return b;
}

static int ty_is_int(Ty t) {
    if (!t.name || t.ptrs) return 0;
    static const char* I[] = { "i8", "u8", "i16", "u16", "i32", "u32",
                               "i64", "u64", "isize", "usize", "addr", "bool" };
    for (size_t i = 0; i < sizeof(I) / sizeof(I[0]); i++)
        if (!strcmp(t.name, I[i])) return 1;
    return 0;
}

/* Argument/parameter compatibility: integers inter-convert (C semantics);
 * `str` only matches `str`; pointers must agree in depth and base, except
 * that *u8/*void act as byte pointers and match any base. `as` casts are
 * authoritative because they change the inferred type itself. */
static int ty_compat(Ty a, Ty p) {
    if (a.name && p.name && a.ptrs == p.ptrs && !strcmp(a.name, p.name)) return 1;
    if (ty_is_int(a) && ty_is_int(p)) return 1;
    if (a.ptrs && p.ptrs && a.ptrs == p.ptrs) {
        if (!strcmp(a.name, "u8") || !strcmp(p.name, "u8")) return 1;
        if (!strcmp(a.name, "void") || !strcmp(p.name, "void")) return 1;
    }
    return 0;
}

/* Recursive static check of one expression tree, run against the live
 * symbol table right before the enclosing statement is emitted. Rejects
 * undeclared variables, unknown callees, arity errors, and argument type
 * mismatches — each with a file:line diagnostic. */
static void check_expr(Expr* e) {
    if (!e) return;
    switch (e->k) {
        case E_PATH:
            if (!vars_find(e->name))
                die("%s:%d: undeclared variable '%s'", FILENAME, e->line, e->name);
            break;
        case E_CALL: {
            if (e->callee->k == E_FIELD) {      /* method call: recv.m(args) */
                Expr* recv = e->callee->base;
                check_expr(recv);
                Ty rt = infer_type(recv);
                Method* m = (rt.name && rt.ptrs == 0)
                            ? method_lookup(rt.name, e->callee->field) : NULL;
                if (!m)
                    die("%s:%d: %s has no method '%s'",
                        FILENAME, e->line, ty_str(rt), e->callee->field);
                if (e->nargs != m->np)
                    die("%s:%d: method '%s.%s' takes %d argument(s), got %d",
                        FILENAME, e->line, m->type, m->name, m->np, e->nargs);
                for (int i = 0; i < e->nargs; i++) {
                    check_expr(e->args[i]);
                    Ty at = infer_type(e->args[i]);
                    if (!ty_compat(at, m->ps[i].ty))
                        die("%s:%d: argument %d to '%s.%s': expected %s, got %s",
                            FILENAME, e->line, i + 1, m->type, m->name,
                            ty_str(m->ps[i].ty), ty_str(at));
                }
                break;
            }
            if (e->callee->k != E_PATH)
                die("%s:%d: only named functions can be called", FILENAME, e->line);
            Param* ps; int np; Ty ret;
            if (!fn_lookup(e->callee->name, &ps, &np, &ret))
                die("%s:%d: unknown function '%s'", FILENAME, e->line, e->callee->name);
            if (e->nargs != np)
                die("%s:%d: '%s' takes %d argument(s), got %d",
                    FILENAME, e->line, e->callee->name, np, e->nargs);
            for (int i = 0; i < e->nargs; i++) {
                check_expr(e->args[i]);
                Ty at = infer_type(e->args[i]);
                if (!ty_compat(at, ps[i].ty))
                    die("%s:%d: argument %d to '%s': expected %s, got %s",
                        FILENAME, e->line, i + 1, e->callee->name,
                        ty_str(ps[i].ty), ty_str(at));
            }
            break;
        }
        case E_FIELD: {
            {   /* payload-less variant reference: Shape.Empty */
                int vi;
                EnumDef* ed = variant_ref(e, &vi);
                if (ed) {
                    if (vi < 0)
                        die("%s:%d: enum '%s' has no variant '%s'",
                            FILENAME, e->line, ed->name, e->field);
                    if (ed->vs[vi].nf != 0)
                        die("%s:%d: variant '%s.%s' carries a payload — construct it "
                            "with %s.%s{ ... }", FILENAME, e->line, ed->name, e->field,
                            ed->name, e->field);
                    break;
                }
            }
            check_expr(e->base);
            Ty b = infer_type(e->base);
            if (ty_is(b, "str")) {
                if (strcmp(e->field, "ptr") != 0 && strcmp(e->field, "len") != 0)
                    die("%s:%d: str has no field '%s' (only .ptr and .len)",
                        FILENAME, e->line, e->field);
            } else if (b.name && b.ptrs == 0 && struct_lookup(b.name)) {
                StructDef* sd = struct_lookup(b.name);
                int ok = 0;
                for (int i = 0; i < sd->nf; i++)
                    if (!strcmp(sd->fs[i].name, e->field)) ok = 1;
                if (!ok)
                    die("%s:%d: struct '%s' has no field '%s'",
                        FILENAME, e->line, b.name, e->field);
            } else {
                die("%s:%d: %s has no fields", FILENAME, e->line, ty_str(b));
            }
            break;
        }
        case E_SLIT: {
            StructDef* sd = struct_lookup(e->name);
            if (!sd)
                die("%s:%d: unknown struct '%s'", FILENAME, e->line, e->name);
            /* every declared field exactly once; every value type-compatible */
            for (int i = 0; i < sd->nf; i++) {
                int seen = 0;
                for (int j = 0; j < e->nargs; j++)
                    if (!strcmp(e->snames[j], sd->fs[i].name)) seen++;
                if (seen != 1)
                    die("%s:%d: literal for '%s' must initialize field '%s' exactly once",
                        FILENAME, e->line, e->name, sd->fs[i].name);
            }
            if (e->nargs != sd->nf)
                die("%s:%d: literal for '%s' names a field it does not have",
                    FILENAME, e->line, e->name);
            for (int j = 0; j < e->nargs; j++) {
                check_expr(e->args[j]);
                Ty ft = ty_named("i64");
                for (int i = 0; i < sd->nf; i++)
                    if (!strcmp(sd->fs[i].name, e->snames[j])) ft = sd->fs[i].ty;
                Ty at = infer_type(e->args[j]);
                if (!ty_compat(at, ft))
                    die("%s:%d: field '%s' of '%s': expected %s, got %s",
                        FILENAME, e->line, e->snames[j], e->name,
                        ty_str(ft), ty_str(at));
            }
            break;
        }
        case E_ELIT: {
            EnumDef* ed = enum_lookup(e->name);
            if (!ed)
                die("%s:%d: unknown enum '%s'", FILENAME, e->line, e->name);
            int vi = variant_index(ed, e->field);
            if (vi < 0)
                die("%s:%d: enum '%s' has no variant '%s'",
                    FILENAME, e->line, e->name, e->field);
            VariantDef* vd = &ed->vs[vi];
            for (int i = 0; i < vd->nf; i++) {
                int seen = 0;
                for (int j = 0; j < e->nargs; j++)
                    if (!strcmp(e->snames[j], vd->fs[i].name)) seen++;
                if (seen != 1)
                    die("%s:%d: literal for '%s.%s' must initialize field '%s' exactly once",
                        FILENAME, e->line, e->name, e->field, vd->fs[i].name);
            }
            if (e->nargs != vd->nf)
                die("%s:%d: literal for '%s.%s' names a field it does not have",
                    FILENAME, e->line, e->name, e->field);
            for (int j = 0; j < e->nargs; j++) {
                check_expr(e->args[j]);
                Ty ft = ty_named("i64");
                for (int i = 0; i < vd->nf; i++)
                    if (!strcmp(vd->fs[i].name, e->snames[j])) ft = vd->fs[i].ty;
                Ty at = infer_type(e->args[j]);
                if (!ty_compat(at, ft))
                    die("%s:%d: field '%s' of '%s.%s': expected %s, got %s",
                        FILENAME, e->line, e->snames[j], e->name, e->field,
                        ty_str(ft), ty_str(at));
            }
            break;
        }
        case E_UN: {
            check_expr(e->r);
            Ty rt = infer_type(e->r);
            if (!ty_is_int(rt))
                die("%s:%d: unary '%s' expects an integer operand (got %s)",
                    FILENAME, e->line, e->op, ty_str(rt));
            break;
        }
        case E_BIN: {
            check_expr(e->l);
            check_expr(e->r);
            Ty lt = infer_type(e->l);
            Ty rt = infer_type(e->r);
            const char* o = e->op;
            int is_cmp = !strcmp(o, "==") || !strcmp(o, "!=") || !strcmp(o, "<") ||
                         !strcmp(o, "<=") || !strcmp(o, ">")  || !strcmp(o, ">=");
            if (ty_is(lt, "str") || ty_is(rt, "str"))
                die("%s:%d: operator '%s' cannot be applied to str values "
                    "(build strings with interpolation)", FILENAME, e->line, o);
            if (lt.ptrs || rt.ptrs) {
                /* Pointers compare; they do not do arithmetic. Cast to `addr`
                 * to compute on an address explicitly. */
                if (!(is_cmp && lt.ptrs && rt.ptrs && ty_compat(lt, rt)))
                    die("%s:%d: operator '%s': pointers only support comparison "
                        "against a compatible pointer (cast to addr for arithmetic)",
                        FILENAME, e->line, o);
            } else if (!(ty_is_int(lt) && ty_is_int(rt))) {
                die("%s:%d: operator '%s' expects integer operands (got %s and %s)",
                    FILENAME, e->line, o, ty_str(lt), ty_str(rt));
            }
            break;
        }
        case E_CAST:   check_expr(e->l); break;
        case E_INTERP:
            for (int i = 0; i < e->nfrags; i++)
                if (e->frags[i].e) {
                    check_expr(e->frags[i].e);
                    Ty ft = infer_type(e->frags[i].e);
                    if (!ty_is(ft, "str") && !ty_is_int(ft))
                        die("%s:%d: cannot interpolate a %s value (only str and integers)",
                            FILENAME, e->line, ty_str(ft));
                }
            break;
        default: break;
    }
}
/* ---------------------------------------------------------------------- */

static void indentf(int ind) { for (int i = 0; i < ind; i++) fputs("    ", OUT); }

static void emit_cstr(const char* s, int n) {
    fputc('"', OUT);
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '\n': fputs("\\n", OUT); break;
            case '\t': fputs("\\t", OUT); break;
            case '\r': fputs("\\r", OUT); break;
            case '"':  fputs("\\\"", OUT); break;
            case '\\': fputs("\\\\", OUT); break;
            case 0:    fputs("\\0", OUT); break;
            default:
                if (c < 32 || c > 126) fprintf(OUT, "\\x%02x", c);
                else fputc(c, OUT);
        }
    }
    fputc('"', OUT);
}

static void gen_expr(Expr* e) {
    switch (e->k) {
        /* %ld, not %lld: both targets are LP64 (long == 64-bit) and the
         * NyxOS libc formatter implements %l[dux] but not %ll. */
        case E_INT:  fprintf(OUT, "%ld", (long)e->ival); break;
        case E_BOOL: fprintf(OUT, "%d", (int)e->ival); break;
        case E_STR:
            fputs("((nyx_str){", OUT);
            emit_cstr(e->s, e->slen);
            fprintf(OUT, ", %d})", e->slen);
            break;
        case E_INTERP: fprintf(OUT, "__s%d", e->iid); break;
        case E_PATH:   fputs(e->name, OUT); break;
        case E_FIELD: {
            int vi;
            EnumDef* ed = variant_ref(e, &vi);
            if (ed) {                   /* Shape.Empty -> tag-only value */
                fprintf(OUT, "((%s){ .tag = %d })", ed->name, vi);
                break;
            }
            gen_expr(e->base);
            fprintf(OUT, ".%s", e->field);
            break;
        }
        case E_ELIT: {                  /* Shape.Circle{ r: 5 } */
            EnumDef* ed = enum_lookup(e->name);
            int vi = ed ? variant_index(ed, e->field) : 0;
            fprintf(OUT, "((%s){ .tag = %d", e->name, vi);
            if (e->nargs) {
                fprintf(OUT, ", .u = { .%s = { ", e->field);
                for (int i = 0; i < e->nargs; i++) {
                    if (i) fputs(", ", OUT);
                    fprintf(OUT, ".%s = ", e->snames[i]);
                    gen_expr(e->args[i]);
                }
                fputs(" } }", OUT);
            }
            fputs(" })", OUT);
            break;
        }
        case E_CALL:
            if (e->callee->k == E_FIELD) {      /* recv.m(a) -> Type_m(recv, a) */
                Ty rt = infer_type(e->callee->base);
                fprintf(OUT, "%s_%s(", rt.name, e->callee->field);
                gen_expr(e->callee->base);
                for (int i = 0; i < e->nargs; i++) {
                    fputs(", ", OUT);
                    gen_expr(e->args[i]);
                }
                fputc(')', OUT);
                break;
            }
            gen_expr(e->callee);
            fputc('(', OUT);
            for (int i = 0; i < e->nargs; i++) {
                if (i) fputs(", ", OUT);
                gen_expr(e->args[i]);
            }
            fputc(')', OUT);
            break;
        case E_UN:
            fprintf(OUT, "%s(", e->op);
            gen_expr(e->r);
            fputc(')', OUT);
            break;
        case E_BIN:
            fputc('(', OUT);
            gen_expr(e->l);
            fprintf(OUT, " %s ", e->op);
            gen_expr(e->r);
            fputc(')', OUT);
            break;
        case E_CAST:
            fputc('(', OUT);
            emit_type(e->cast_to);
            fputs(")(", OUT);
            gen_expr(e->l);
            fputc(')', OUT);
            break;
        case E_SLIT:                    /* C99 compound literal, designated */
            fprintf(OUT, "((%s){", e->name);
            for (int i = 0; i < e->nargs; i++) {
                if (i) fputs(", ", OUT);
                fprintf(OUT, ".%s = ", e->snames[i]);
                gen_expr(e->args[i]);
            }
            fputs("})", OUT);
            break;
    }
}

/* Lower every string interpolation in the expr tree to prelude statements:
 * hoisted buffer + nyx_str built with __nyx_fmt_*. The expression slot then
 * references __sN. Interpolated exprs are formatted as i64 in N v0.1. */
static void gen_preludes(Expr* e, int ind) {
    if (!e) return;
    switch (e->k) {
        case E_INTERP: {
            for (int i = 0; i < e->nfrags; i++)
                if (e->frags[i].e) gen_preludes(e->frags[i].e, ind);
            e->iid = IID++;
            indentf(ind); fprintf(OUT, "char __b%d[256];\n", e->iid);
            indentf(ind); fprintf(OUT, "nyx_str __s%d = __nyx_fmt_begin(__b%d, 256);\n", e->iid, e->iid);
            for (int i = 0; i < e->nfrags; i++) {
                Frag* f = &e->frags[i];
                if (f->e) {
                    indentf(ind);
                    if (ty_is(infer_type(f->e), "str")) {   /* str: insert as text */
                        fprintf(OUT, "__nyx_fmt_str(&__s%d, __b%d, 256, ", e->iid, e->iid);
                        gen_expr(f->e);
                        fputs(");\n", OUT);
                    } else {                                /* everything else: i64 */
                        fprintf(OUT, "__nyx_fmt_i64(&__s%d, __b%d, 256, (nyx_i64)(", e->iid, e->iid);
                        gen_expr(f->e);
                        fputs("));\n", OUT);
                    }
                } else if (f->tlen > 0) {
                    indentf(ind);
                    fprintf(OUT, "__nyx_fmt_str(&__s%d, __b%d, 256, (nyx_str){", e->iid, e->iid);
                    emit_cstr(f->text, f->tlen);
                    fprintf(OUT, ", %d});\n", f->tlen);
                }
            }
            break;
        }
        case E_CALL:
            gen_preludes(e->callee, ind);
            for (int i = 0; i < e->nargs; i++) gen_preludes(e->args[i], ind);
            break;
        case E_FIELD: gen_preludes(e->base, ind); break;
        case E_UN:    gen_preludes(e->r, ind); break;
        case E_BIN:   gen_preludes(e->l, ind); gen_preludes(e->r, ind); break;
        case E_CAST:  gen_preludes(e->l, ind); break;
        default: break;
    }
}

static void gen_block(Block* b, int ind, int fn_tail);

/* Emit every pending defer, most recently registered first. */
static void gen_defers(int ind) {
    for (int i = NDEFERS - 1; i >= 0; i--) {
        gen_preludes(FN_DEFERS[i], ind);
        indentf(ind);
        gen_expr(FN_DEFERS[i]);
        fputs(";\n", OUT);
    }
}

/* Emit a zero initializer for a declaration of type t. Used by the v0.9
 * match-expression lowering: the switch that follows is exhaustive (the
 * checker proved it), but a C compiler cannot see that, so the slot is
 * zeroed to keep the generated C warning-free. Aggregates (str, structs,
 * enums) take {0}; scalars and pointers take 0. */
static void emit_zero(Ty t) {
    if (t.ptrs == 0 && t.name &&
        (ty_is(t, "str") || struct_lookup(t.name) || enum_lookup(t.name)))
        fputs("{0}", OUT);
    else
        fputs("0", OUT);
}

/* v0.10 error propagation — lower `x := e?;`, `x = e?;`, and `e?;`.
 * The operand and the enclosing function's return type must both be
 * result enums (enum_is_result). On Err the function returns at once:
 * the same value when the two types are identical, otherwise the Err
 * payload rewrapped into the return type — and the defers run before
 * the return, exactly like any other return (spec 5.7). On Ok the
 * statement consumes the payload (bind/assign) or discards it (`e?;`). */
static void gen_try(Stmt* s, int ind) {
    check_expr(s->e);
    Ty ot = infer_type(s->e);
    EnumDef* oe = (ot.name && ot.ptrs == 0) ? enum_lookup(ot.name) : NULL;
    if (!oe || !enum_is_result(oe))
        die("%s:%d: operand of '?' must be a result enum (exactly the variants Ok and Err), got %s",
            FILENAME, s->line, ty_str(ot));
    EnumDef* re = (CUR_RET.name && CUR_RET.ptrs == 0) ? enum_lookup(CUR_RET.name) : NULL;
    if (!re || !enum_is_result(re))
        die("%s:%d: '?' propagates by returning, so the function must return a result enum (it returns %s)",
            FILENAME, s->line, ty_str(CUR_RET));
    VariantDef* ov_ok  = &oe->vs[variant_index(oe, "Ok")];
    VariantDef* ov_err = &oe->vs[variant_index(oe, "Err")];
    VariantDef* rv_err = &re->vs[variant_index(re, "Err")];
    int same = !strcmp(oe->name, re->name);
    if (!same) {
        if (ov_err->nf != rv_err->nf)
            die("%s:%d: cannot propagate: %s.Err and %s.Err disagree on carrying a payload",
                FILENAME, s->line, oe->name, re->name);
        if (ov_err->nf == 1 && !ty_compat(ov_err->fs[0].ty, rv_err->fs[0].ty))
            die("%s:%d: cannot propagate %s.Err (%s) as %s.Err (%s)",
                FILENAME, s->line, oe->name, ty_str(ov_err->fs[0].ty),
                re->name, ty_str(rv_err->fs[0].ty));
    }
    if (s->k != S_EXPR && ov_ok->nf != 1)
        die("%s:%d: %s.Ok carries no payload to bind — use the statement form `expr?;`",
            FILENAME, s->line, oe->name);
    Ty okty; okty.name = NULL; okty.ptrs = 0;
    if (ov_ok->nf == 1) okty = ov_ok->fs[0].ty;
    if (s->k == S_ASSIGN) {                  /* same checks as plain S_ASSIGN */
        check_expr(s->lhs);
        Expr* root = s->lhs;
        while (root->k == E_FIELD) root = root->base;
        if (root->k == E_PATH) {
            VarInfo* v = vars_find(root->name);
            if (v && !v->is_mut)
                die("%s:%d: cannot assign to immutable '%s' (declare it with 'mut')",
                    FILENAME, s->line, root->name);
        }
        Ty lt = infer_type(s->lhs);
        if (!ty_compat(okty, lt))
            die("%s:%d: cannot assign %s to a %s target",
                FILENAME, s->line, ty_str(okty), ty_str(lt));
        if (strcmp(s->aop, "=") != 0 && !ty_is_int(lt))
            die("%s:%d: '%s' requires an integer target (got %s)",
                FILENAME, s->line, s->aop, ty_str(lt));
    }
    int tid = IID++;
    if (s->k == S_LET) {                     /* zero init: dead store, the Err
                                              * branch below always returns */
        indentf(ind);
        emit_type(okty);
        fprintf(OUT, " %s = ", s->name);
        emit_zero(okty);
        fputs(";\n", OUT);
    }
    indentf(ind); fputs("{\n", OUT);
    gen_preludes(s->e, ind + 1);
    indentf(ind + 1);
    fprintf(OUT, "%s __t%d = ", oe->name, tid);
    gen_expr(s->e);
    fputs(";\n", OUT);
    indentf(ind + 1);
    fprintf(OUT, "if (__t%d.tag == %d) {\n", tid, variant_index(oe, "Err"));
    if (same) {
        gen_defers(ind + 2);
        indentf(ind + 2); fprintf(OUT, "return __t%d;\n", tid);
    } else {
        indentf(ind + 2);
        fprintf(OUT, "%s __e%d = {0};\n", re->name, tid);
        indentf(ind + 2);
        fprintf(OUT, "__e%d.tag = %d;\n", tid, variant_index(re, "Err"));
        if (rv_err->nf == 1) {
            indentf(ind + 2);
            fprintf(OUT, "__e%d.u.Err.%s = __t%d.u.Err.%s;\n",
                    tid, rv_err->fs[0].name, tid, ov_err->fs[0].name);
        }
        gen_defers(ind + 2);
        indentf(ind + 2); fprintf(OUT, "return __e%d;\n", tid);
    }
    indentf(ind + 1); fputs("}\n", OUT);
    if (s->k == S_LET) {
        indentf(ind + 1);
        fprintf(OUT, "%s = __t%d.u.Ok.%s;\n", s->name, tid, ov_ok->fs[0].name);
    } else if (s->k == S_ASSIGN) {
        indentf(ind + 1);
        gen_expr(s->lhs);
        fprintf(OUT, " %s __t%d.u.Ok.%s;\n", s->aop, tid, ov_ok->fs[0].name);
    }
    indentf(ind); fputs("}\n", OUT);
    if (s->k == S_LET) vars_add(s->name, okty, s->is_mut);
}

static void gen_stmt(Stmt* s, int ind) {
    if (s->is_try) { gen_try(s, ind); return; }
    switch (s->k) {
        case S_LET: {
            check_expr(s->e);
            Ty t = infer_type(s->e);
            if (!t.name || is_never(t))
                die("%s:%d: cannot bind '%s' to an expression with no value",
                    FILENAME, s->line, s->name);
            gen_preludes(s->e, ind);
            indentf(ind);
            emit_type(t);
            fprintf(OUT, " %s = ", s->name);
            gen_expr(s->e);
            fputs(";\n", OUT);
            vars_add(s->name, t, s->is_mut);
            break;
        }
        case S_ASSIGN: {
            check_expr(s->lhs);
            check_expr(s->e);
            {                                   /* mut enforcement (spec 5.1) —
                                                 * field writes mutate the root binding */
                Expr* root = s->lhs;
                while (root->k == E_FIELD) root = root->base;
                if (root->k == E_PATH) {
                    VarInfo* v = vars_find(root->name);
                    if (v && !v->is_mut)
                        die("%s:%d: cannot assign to immutable '%s' (declare it with 'mut')",
                            FILENAME, s->line, root->name);
                }
            }
            {                                    /* value/target type check (v0.4) */
                Ty lt = infer_type(s->lhs);
                Ty rt = infer_type(s->e);
                if (!ty_compat(rt, lt))
                    die("%s:%d: cannot assign %s to a %s target",
                        FILENAME, s->line, ty_str(rt), ty_str(lt));
                if (strcmp(s->aop, "=") != 0 && !ty_is_int(lt))
                    die("%s:%d: '%s' requires an integer target (got %s)",
                        FILENAME, s->line, s->aop, ty_str(lt));
            }
            gen_preludes(s->e, ind);
            indentf(ind);
            gen_expr(s->lhs);
            fprintf(OUT, " %s ", s->aop);
            gen_expr(s->e);
            fputs(";\n", OUT);
            break;
        }
        case S_RET:
            check_expr(s->e);
            if (s->e) {
                if (!CUR_RET.name || is_never(CUR_RET))
                    die("%s:%d: 'return' with a value in a function with no return type",
                        FILENAME, s->line);
                Ty vt = infer_type(s->e);
                if (!ty_compat(vt, CUR_RET))
                    die("%s:%d: return type mismatch: expected %s, got %s",
                        FILENAME, s->line, ty_str(CUR_RET), ty_str(vt));
            } else if (CUR_RET.name && !is_never(CUR_RET)) {
                die("%s:%d: 'return' without a value in a function returning %s",
                    FILENAME, s->line, ty_str(CUR_RET));
            }
            if (NDEFERS && s->e) {
                /* Semantics: the return value is computed BEFORE the defers
                 * run, so a defer cannot change what gets returned. */
                indentf(ind); fputs("{\n", OUT);
                gen_preludes(s->e, ind + 1);
                indentf(ind + 1);
                emit_type(infer_type(s->e));
                fputs(" __ret = ", OUT);
                gen_expr(s->e);
                fputs(";\n", OUT);
                gen_defers(ind + 1);
                indentf(ind + 1); fputs("return __ret;\n", OUT);
                indentf(ind); fputs("}\n", OUT);
                break;
            }
            if (NDEFERS) {                          /* void return */
                gen_defers(ind);
                indentf(ind); fputs("return;\n", OUT);
                break;
            }
            gen_preludes(s->e, ind);
            indentf(ind);
            if (s->e) { fputs("return ", OUT); gen_expr(s->e); fputs(";\n", OUT); }
            else fputs("return;\n", OUT);
            break;
        case S_EXPR:
            check_expr(s->e);
            gen_preludes(s->e, ind);
            indentf(ind);
            gen_expr(s->e);
            fputs(";\n", OUT);
            break;
        case S_BREAK: indentf(ind); fputs("break;\n", OUT); break;
        case S_CONT:  indentf(ind); fputs("continue;\n", OUT); break;
        case S_DEFER:
            if (BLOCK_DEPTH != 1)
                die("%s:%d: defer is only allowed in the function's outermost block",
                    FILENAME, s->line);
            if (NDEFERS >= 16)
                die("%s:%d: too many defers in one function", FILENAME, s->line);
            check_expr(s->e);           /* names resolve at registration point */
            FN_DEFERS[NDEFERS++] = s->e;
            break;
        case S_MATCH: {
            check_expr(s->e);
            Ty st_ = infer_type(s->e);
            EnumDef* ed = (st_.name && st_.ptrs == 0) ? enum_lookup(st_.name) : NULL;
            if (!ed)
                die("%s:%d: match subject must be an enum value (got %s)",
                    FILENAME, s->line, ty_str(st_));
            /* exhaustive, no duplicates, binds match the payload */
            for (int v = 0; v < ed->nv; v++) {
                int seen = 0;
                for (int a = 0; a < s->narms; a++)
                    if (!strcmp(s->arms[a].variant, ed->vs[v].name)) seen++;
                if (seen != 1)
                    die("%s:%d: match must cover variant '%s' exactly once",
                        FILENAME, s->line, ed->vs[v].name);
            }
            if (s->narms != ed->nv)
                die("%s:%d: match names a variant '%s' does not have",
                    FILENAME, s->line, ed->name);
            for (int a = 0; a < s->narms; a++) {
                int vi = variant_index(ed, s->arms[a].variant);
                if (s->arms[a].nbinds != ed->vs[vi].nf)
                    die("%s:%d: arm '%s' binds %d name(s), but the payload has %d field(s)",
                        FILENAME, s->arms[a].line, s->arms[a].variant,
                        s->arms[a].nbinds, ed->vs[vi].nf);
            }
            if (s->mtarget != MT_STMT) {
                /* v0.9 expression form: every arm yields a value. Each arm
                 * is typed with its binds in scope; the first arm fixes the
                 * result type and every later arm must agree with it. */
                Ty res; res.name = NULL; res.ptrs = 0;
                for (int a = 0; a < s->narms; a++) {
                    int vi = variant_index(ed, s->arms[a].variant);
                    int vsave = NVARS;
                    for (int b2 = 0; b2 < s->arms[a].nbinds; b2++)
                        vars_add(s->arms[a].binds[b2], ed->vs[vi].fs[b2].ty, 0);
                    check_expr(s->arms[a].val);
                    Ty at = infer_type(s->arms[a].val);
                    NVARS = vsave;
                    if (!at.name || is_never(at))
                        die("%s:%d: match arm '%s' must yield a value",
                            FILENAME, s->arms[a].line, s->arms[a].variant);
                    if (a == 0) res = at;
                    else if (!ty_compat(at, res))
                        die("%s:%d: match arms disagree: arm '%s' yields %s, expected %s",
                            FILENAME, s->arms[a].line, s->arms[a].variant,
                            ty_str(at), ty_str(res));
                }
                if (s->mtarget == MT_ASSIGN) {   /* same checks as S_ASSIGN */
                    check_expr(s->lhs);
                    Expr* root = s->lhs;
                    while (root->k == E_FIELD) root = root->base;
                    if (root->k == E_PATH) {
                        VarInfo* v = vars_find(root->name);
                        if (v && !v->is_mut)
                            die("%s:%d: cannot assign to immutable '%s' (declare it with 'mut')",
                                FILENAME, s->line, root->name);
                    }
                    Ty lt = infer_type(s->lhs);
                    if (!ty_compat(res, lt))
                        die("%s:%d: cannot assign %s to a %s target",
                            FILENAME, s->line, ty_str(res), ty_str(lt));
                    if (strcmp(s->aop, "=") != 0 && !ty_is_int(lt))
                        die("%s:%d: '%s' requires an integer target (got %s)",
                            FILENAME, s->line, s->aop, ty_str(lt));
                }
                if (s->mtarget == MT_RET) {      /* same checks as S_RET */
                    if (!CUR_RET.name || is_never(CUR_RET))
                        die("%s:%d: 'return' with a value in a function with no return type",
                            FILENAME, s->line);
                    if (!ty_compat(res, CUR_RET))
                        die("%s:%d: return type mismatch: expected %s, got %s",
                            FILENAME, s->line, ty_str(CUR_RET), ty_str(res));
                }
                /* Lowering: the selected arm assigns its value into a
                 * __mres temp; the target is consumed AFTER the switch, so
                 * an arm bind may freely shadow the target's name. For
                 * `return match` the value is computed before the defers
                 * run, exactly like a plain `return` (spec 5.7). */
                int mid = IID++;
                Ty rty = (s->mtarget == MT_RET) ? CUR_RET : res;
                if (s->mtarget == MT_LET) {
                    indentf(ind);
                    emit_type(res);
                    fprintf(OUT, " %s = ", s->name);
                    emit_zero(res);
                    fputs(";\n", OUT);
                }
                indentf(ind); fputs("{\n", OUT);
                gen_preludes(s->e, ind + 1);
                indentf(ind + 1);
                fprintf(OUT, "%s __m%d = ", ed->name, mid);
                gen_expr(s->e);
                fputs(";\n", OUT);
                indentf(ind + 1);
                emit_type(rty);
                fprintf(OUT, " __mres%d = ", mid);
                emit_zero(rty);                  /* dead store: the switch is exhaustive */
                fputs(";\n", OUT);
                indentf(ind + 1); fprintf(OUT, "switch (__m%d.tag) {\n", mid);
                for (int a = 0; a < s->narms; a++) {
                    int vi = variant_index(ed, s->arms[a].variant);
                    indentf(ind + 1);
                    fprintf(OUT, "case %d: {\n", vi);
                    int vsave = NVARS;
                    for (int b2 = 0; b2 < s->arms[a].nbinds; b2++) {
                        Param* pf = &ed->vs[vi].fs[b2];
                        indentf(ind + 2);
                        emit_type(pf->ty);
                        fprintf(OUT, " %s = __m%d.u.%s.%s;\n",
                                s->arms[a].binds[b2], mid, s->arms[a].variant, pf->name);
                        vars_add(s->arms[a].binds[b2], pf->ty, 0);
                    }
                    gen_preludes(s->arms[a].val, ind + 2);
                    indentf(ind + 2);
                    fprintf(OUT, "__mres%d = ", mid);
                    gen_expr(s->arms[a].val);
                    fputs(";\n", OUT);
                    indentf(ind + 2); fputs("break;\n", OUT);
                    indentf(ind + 1); fputs("}\n", OUT);
                    NVARS = vsave;
                }
                indentf(ind + 1); fputs("}\n", OUT);
                if (s->mtarget == MT_LET) {
                    indentf(ind + 1);
                    fprintf(OUT, "%s = __mres%d;\n", s->name, mid);
                } else if (s->mtarget == MT_ASSIGN) {
                    indentf(ind + 1);
                    gen_expr(s->lhs);
                    fprintf(OUT, " %s __mres%d;\n", s->aop, mid);
                } else {
                    gen_defers(ind + 1);
                    indentf(ind + 1); fprintf(OUT, "return __mres%d;\n", mid);
                }
                indentf(ind); fputs("}\n", OUT);
                if (s->mtarget == MT_LET)
                    vars_add(s->name, res, s->is_mut);
                break;
            }
            /* lowering: subject once into a temp, switch on the tag, binds
             * become typed locals initialized from the payload fields */
            int mid = IID++;
            indentf(ind); fputs("{\n", OUT);
            gen_preludes(s->e, ind + 1);
            indentf(ind + 1);
            fprintf(OUT, "%s __m%d = ", ed->name, mid);
            gen_expr(s->e);
            fputs(";\n", OUT);
            indentf(ind + 1); fprintf(OUT, "switch (__m%d.tag) {\n", mid);
            for (int a = 0; a < s->narms; a++) {
                int vi = variant_index(ed, s->arms[a].variant);
                indentf(ind + 1);
                fprintf(OUT, "case %d: {\n", vi);
                int vsave = NVARS;
                for (int b2 = 0; b2 < s->arms[a].nbinds; b2++) {
                    Param* pf = &ed->vs[vi].fs[b2];
                    indentf(ind + 2);
                    emit_type(pf->ty);
                    fprintf(OUT, " %s = __m%d.u.%s.%s;\n",
                            s->arms[a].binds[b2], mid, s->arms[a].variant, pf->name);
                    vars_add(s->arms[a].binds[b2], pf->ty, 0);
                }
                indentf(ind + 2);
                gen_block(s->arms[a].body, ind + 2, 0);
                fputs("\n", OUT);
                indentf(ind + 2); fputs("break;\n", OUT);
                indentf(ind + 1); fputs("}\n", OUT);
                NVARS = vsave;
            }
            indentf(ind + 1); fputs("}\n", OUT);
            indentf(ind); fputs("}\n", OUT);
            break;
        }
        case S_WHILE:
            check_expr(s->cond);
            gen_preludes(s->cond, ind);
            indentf(ind);
            fputs("while (", OUT);
            gen_expr(s->cond);
            fputs(") ", OUT);
            gen_block(s->body, ind, 0);
            fputs("\n", OUT);
            break;
        case S_IF:
            check_expr(s->cond);
            gen_preludes(s->cond, ind);
            indentf(ind);
            fputs("if (", OUT);
            gen_expr(s->cond);
            fputs(") ", OUT);
            gen_block(s->body, ind, 0);
            if (s->els) { fputs(" else ", OUT); gen_block(s->els, ind, 0); }
            fputs("\n", OUT);
            break;
    }
}

static void gen_block(Block* b, int ind, int fn_tail) {
    int vsave = NVARS;                  /* block scope: locals die with the block */
    BLOCK_DEPTH++;
    fputs("{\n", OUT);
    for (int i = 0; i < b->n; i++) gen_stmt(b->st[i], ind + 1);
    if (b->tail) {
        check_expr(b->tail);
        int returns = fn_tail && CUR_RET.name && !is_never(CUR_RET);
        if (returns) {
            Ty vt = infer_type(b->tail);         /* tail value = return value */
            if (!ty_compat(vt, CUR_RET))
                die("%s: tail expression type mismatch: expected %s, got %s",
                    FILENAME, ty_str(CUR_RET), ty_str(vt));
        }
        if (returns && NDEFERS) {                /* tail value, then defers, then return */
            gen_preludes(b->tail, ind + 1);
            indentf(ind + 1);
            emit_type(infer_type(b->tail));
            fputs(" __ret = ", OUT);
            gen_expr(b->tail);
            fputs(";\n", OUT);
            gen_defers(ind + 1);
            indentf(ind + 1); fputs("return __ret;\n", OUT);
        } else {
            gen_preludes(b->tail, ind + 1);
            indentf(ind + 1);
            if (returns) fputs("return ", OUT);
            gen_expr(b->tail);
            fputs(";\n", OUT);
            if (fn_tail && !returns && NDEFERS) gen_defers(ind + 1);
        }
    } else if (fn_tail && NDEFERS && (!CUR_RET.name || is_never(CUR_RET))) {
        gen_defers(ind + 1);                     /* void fn falling off the end */
    }
    indentf(ind);
    fputs("}", OUT);
    BLOCK_DEPTH--;
    NVARS = vsave;
}

static void gen_extern_fns(void) {
    for (int i = 0; i < NXFN; i++) {
        XFn* x = &XFNS[i];
        fputs("static inline ", OUT);
        if (is_never(x->ret)) fputs("void", OUT);
        else emit_type(x->ret);
        fprintf(OUT, " %s(", x->name);
        if (!x->np) fputs("void", OUT);
        for (int k = 0; k < x->np; k++) {
            if (k) fputs(", ", OUT);
            emit_type(x->ps[k].ty);
            fprintf(OUT, " %s", x->ps[k].name);
        }
        fputs(") {\n", OUT);
        if (is_never(x->ret) || !x->ret.name) {
            fprintf(OUT, "    (void)__nyx_syscall6(%ld", (long)x->num);
            for (int k = 0; k < 6; k++) {
                if (k < x->np) fprintf(OUT, ", (nyx_i64)%s", x->ps[k].name);
                else fputs(", 0", OUT);
            }
            fputs(");\n", OUT);
            if (is_never(x->ret)) fputs("    for (;;) {}\n", OUT);
        } else {
            fputs("    return (", OUT);
            emit_type(x->ret);
            fprintf(OUT, ")__nyx_syscall6(%ld", (long)x->num);
            for (int k = 0; k < 6; k++) {
                if (k < x->np) fprintf(OUT, ", (nyx_i64)%s", x->ps[k].name);
                else fputs(", 0", OUT);
            }
            fputs(");\n", OUT);
        }
        fputs("}\n", OUT);
    }
    if (NXFN) fputs("\n", OUT);
}

static void gen_fn_sig(Fn* f) {
    if (!f->ret.name || is_never(f->ret)) fputs("void", OUT);
    else emit_type(f->ret);
    fprintf(OUT, " %s(", f->name);
    if (!f->np) fputs("void", OUT);
    for (int i = 0; i < f->np; i++) {
        if (i) fputs(", ", OUT);
        emit_type(f->ps[i].ty);
        fprintf(OUT, " %s", f->ps[i].name);
    }
    fputc(')', OUT);
}

/* Every non-primitive type name in a signature or struct field must be a
 * declared struct — catches typos before they become C errors. */
static void validate_ty(Ty t, const char* where) {
    if (!t.name) return;
    if (strcmp(base_ctype(t.name), t.name) != 0) return;   /* mapped: primitive */
    if (!struct_lookup(t.name) && !enum_lookup(t.name))
        die("%s: unknown type '%s' in %s", FILENAME, t.name, where);
}

static void gen_program(const char* srcname) {
    for (int i = 0; i < NSTRUCTS; i++)
        for (int f = 0; f < STRUCTS[i].nf; f++)
            validate_ty(STRUCTS[i].fs[f].ty, STRUCTS[i].name);
    for (int i = 0; i < NXFN; i++) {
        validate_ty(XFNS[i].ret, XFNS[i].name);
        for (int p = 0; p < XFNS[i].np; p++) validate_ty(XFNS[i].ps[p].ty, XFNS[i].name);
    }
    for (int i = 0; i < NFN; i++) {
        validate_ty(FNS[i].ret, FNS[i].name);
        for (int p = 0; p < FNS[i].np; p++) validate_ty(FNS[i].ps[p].ty, FNS[i].name);
    }

    for (int i = 0; i < NENUMS; i++)
        for (int v = 0; v < ENUMS[i].nv; v++)
            for (int f = 0; f < ENUMS[i].vs[v].nf; f++)
                validate_ty(ENUMS[i].vs[v].fs[f].ty, ENUMS[i].name);
    for (int i = 0; i < NMETHODS; i++) {
        Method* m = &METHODS[i];
        if (!struct_lookup(m->type) && !enum_lookup(m->type))
            die("%s: impl for unknown type '%s'", FILENAME, m->type);
        validate_ty(m->ret, m->name);
        for (int p = 0; p < m->np; p++) validate_ty(m->ps[p].ty, m->name);
    }

    fprintf(OUT, "/* Generated by ncc from %s */\n#include \"nyxrt.h\"\n\n", srcname);
    for (int i = 0; i < NSTRUCTS; i++) {        /* struct layouts first */
        fprintf(OUT, "typedef struct {\n");
        for (int f = 0; f < STRUCTS[i].nf; f++) {
            fputs("    ", OUT);
            emit_type(STRUCTS[i].fs[f].ty);
            fprintf(OUT, " %s;\n", STRUCTS[i].fs[f].name);
        }
        fprintf(OUT, "} %s;\n\n", STRUCTS[i].name);
    }
    for (int i = 0; i < NENUMS; i++) {          /* enum = tagged union */
        EnumDef* ed = &ENUMS[i];
        int has_payload = 0;
        for (int v = 0; v < ed->nv; v++)
            if (ed->vs[v].nf) has_payload = 1;
        fprintf(OUT, "typedef struct {\n    int tag;    /*");
        for (int v = 0; v < ed->nv; v++)
            fprintf(OUT, " %d=%s", v, ed->vs[v].name);
        fputs(" */\n", OUT);
        if (has_payload) {                      /* an empty union is not valid C */
            fputs("    union {\n", OUT);
            for (int v = 0; v < ed->nv; v++) {
                if (!ed->vs[v].nf) continue;
                fputs("        struct { ", OUT);
                for (int f = 0; f < ed->vs[v].nf; f++) {
                    emit_type(ed->vs[v].fs[f].ty);
                    fprintf(OUT, " %s; ", ed->vs[v].fs[f].name);
                }
                fprintf(OUT, "} %s;\n", ed->vs[v].name);
            }
            fputs("    } u;\n", OUT);
        }
        fprintf(OUT, "} %s;\n\n", ed->name);
    }
    gen_extern_fns();
    for (int i = 0; i < NFN; i++) { gen_fn_sig(&FNS[i]); fputs(";\n", OUT); }
    for (int i = 0; i < NMETHODS; i++) {        /* method prototypes */
        Method* m = &METHODS[i];
        fputs("static ", OUT);
        if (!m->ret.name || is_never(m->ret)) fputs("void", OUT);
        else emit_type(m->ret);
        fprintf(OUT, " %s_%s(%s self", m->type, m->name, m->type);
        for (int p = 0; p < m->np; p++) {
            fputs(", ", OUT);
            emit_type(m->ps[p].ty);
            fprintf(OUT, " %s", m->ps[p].name);
        }
        fputs(");\n", OUT);
    }
    if (NFN || NMETHODS) fputs("\n", OUT);
    for (int i = 0; i < NMETHODS; i++) {        /* method bodies */
        Method* m = &METHODS[i];
        IID = 0;
        CUR_RET = m->ret;
        NVARS = 0;
        NDEFERS = 0;
        BLOCK_DEPTH = 0;
        vars_add((char*)"self", ty_named(m->type), 0);   /* receiver: immutable */
        for (int p = 0; p < m->np; p++) vars_add(m->ps[p].name, m->ps[p].ty, 0);
        fputs("static ", OUT);
        if (!m->ret.name || is_never(m->ret)) fputs("void", OUT);
        else emit_type(m->ret);
        fprintf(OUT, " %s_%s(%s self", m->type, m->name, m->type);
        for (int p = 0; p < m->np; p++) {
            fputs(", ", OUT);
            emit_type(m->ps[p].ty);
            fprintf(OUT, " %s", m->ps[p].name);
        }
        fputs(") ", OUT);
        gen_block(m->body, 0, 1);
        fputs("\n\n", OUT);
    }
    for (int i = 0; i < NFN; i++) {
        Fn* f = &FNS[i];
        IID = 0;
        CUR_RET = f->ret;
        NVARS = 0;                      /* fresh symbol table per function */
        NDEFERS = 0;                    /* defers are function-scoped */
        BLOCK_DEPTH = 0;
        for (int p = 0; p < f->np; p++) /* parameters are immutable bindings */
            vars_add(f->ps[p].name, f->ps[p].ty, 0);
        gen_fn_sig(f);
        fputc(' ', OUT);
        gen_block(f->body, 0, 1);
        fputs("\n\n", OUT);
    }
}

/* ------------------------------------------------------------------ */
/* driver                                                             */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv) {
    const char* in = NULL;
    const char* out = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (argv[i][0] == '-') die("ncc: unknown option '%s'", argv[i]);
        else if (!in) in = argv[i];
        else die("ncc: multiple input files");
    }
    if (!in) die("usage: ncc <input.n> [-o output.c]");

    FILE* f = fopen(in, "rb");
    if (!f) die("ncc: cannot open '%s'", in);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = xmalloc((size_t)sz + 1);
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) die("ncc: read error on '%s'", in);
    fclose(f);

    FILENAME = in;
    SRC = buf;
    LEN = (int)sz;
    CUR = next_token();
    parse_program();

    OUT = out ? fopen(out, "w") : stdout;
    if (!OUT) die("ncc: cannot open output '%s'", out);
    gen_program(in);
    if (out) fclose(OUT);

    fprintf(stderr, "ncc: OK — %d extern syscall(s), %d function(s)%s%s\n",
            NXFN, NFN, out ? " -> " : "", out ? out : "");
    return 0;
}
