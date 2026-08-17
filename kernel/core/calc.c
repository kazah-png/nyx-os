// ============================================================
// calc.c - 64-bit signed integer expression evaluator (+ KAT)
// ============================================================
// Recursive-descent, C operator precedence, two's-complement wraparound, truncating division /
// C modulo / arithmetic shift. Host-verified against an independent C-int64 model across 6000+
// random expressions before landing (see scratchpad/calc_proto.c). No heap, no libc.
#include "calc.h"

#define CALC_I64_MIN (-9223372036854775807LL - 1)

enum { CE_OK = 0, CE_BADTOK = 1, CE_PAREN = 2, CE_DIVZERO = 3, CE_TRAILING = 4, CE_SHIFT = 5, CE_EMPTY = 6 };

typedef struct { const char* s; int pos; int err; } cctx;

static void cskip(cctx* c) { while (c->s[c->pos] == ' ' || c->s[c->pos] == '\t') c->pos++; }
static int64_t p_bitor(cctx*);

static int hexv(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int64_t p_primary(cctx* c) {
    cskip(c);
    char ch = c->s[c->pos];
    if (ch == '(') {
        c->pos++;
        int64_t v = p_bitor(c);
        cskip(c);
        if (c->s[c->pos] == ')') c->pos++; else if (!c->err) c->err = CE_PAREN;
        return v;
    }
    if (ch >= '0' && ch <= '9') {
        uint64_t v = 0;
        if (ch == '0' && (c->s[c->pos + 1] == 'x' || c->s[c->pos + 1] == 'X') && hexv(c->s[c->pos + 2]) >= 0) {
            c->pos += 2;
            while (hexv(c->s[c->pos]) >= 0) { v = v * 16u + (uint64_t)hexv(c->s[c->pos]); c->pos++; }
        } else if (ch == '0' && (c->s[c->pos + 1] == 'b' || c->s[c->pos + 1] == 'B') &&
                   (c->s[c->pos + 2] == '0' || c->s[c->pos + 2] == '1')) {
            c->pos += 2;
            while (c->s[c->pos] == '0' || c->s[c->pos] == '1') { v = v * 2u + (uint64_t)(c->s[c->pos] - '0'); c->pos++; }
        } else {
            while (c->s[c->pos] >= '0' && c->s[c->pos] <= '9') { v = v * 10u + (uint64_t)(c->s[c->pos] - '0'); c->pos++; }
        }
        return (int64_t)v;
    }
    if (!c->err) c->err = CE_BADTOK;
    return 0;
}

static int64_t p_unary(cctx* c) {
    cskip(c);
    char ch = c->s[c->pos];
    if (ch == '-') { c->pos++; return (int64_t)(0 - (uint64_t)p_unary(c)); }
    if (ch == '+') { c->pos++; return p_unary(c); }
    if (ch == '~') { c->pos++; return ~p_unary(c); }
    return p_primary(c);
}

static int64_t p_mul(cctx* c) {
    int64_t a = p_unary(c);
    for (;;) {
        cskip(c);
        char op = c->s[c->pos];
        if (op != '*' && op != '/' && op != '%') break;
        c->pos++;
        int64_t b = p_unary(c);
        if (op == '*') { a = (int64_t)((uint64_t)a * (uint64_t)b); }
        else {
            if (b == 0) { if (!c->err) c->err = CE_DIVZERO; return 0; }
            if (a == CALC_I64_MIN && b == -1) { a = (op == '/') ? CALC_I64_MIN : 0; }   // avoid #DE
            else a = (op == '/') ? (a / b) : (a % b);
        }
    }
    return a;
}

static int64_t p_add(cctx* c) {
    int64_t a = p_mul(c);
    for (;;) {
        cskip(c);
        char op = c->s[c->pos];
        if (op != '+' && op != '-') break;
        c->pos++;
        int64_t b = p_mul(c);
        a = (op == '+') ? (int64_t)((uint64_t)a + (uint64_t)b) : (int64_t)((uint64_t)a - (uint64_t)b);
    }
    return a;
}

static int64_t p_shift(cctx* c) {
    int64_t a = p_add(c);
    for (;;) {
        cskip(c);
        char op = c->s[c->pos];
        if (!((op == '<' && c->s[c->pos + 1] == '<') || (op == '>' && c->s[c->pos + 1] == '>'))) break;
        c->pos += 2;
        int64_t b = p_add(c);
        if (b < 0 || b > 63) { if (!c->err) c->err = CE_SHIFT; return 0; }
        a = (op == '<') ? (int64_t)((uint64_t)a << b) : (a >> b);   // arithmetic >> for signed
    }
    return a;
}

static int64_t p_bitand(cctx* c) {
    int64_t a = p_shift(c);
    for (;;) { cskip(c); if (c->s[c->pos] != '&') break; c->pos++; a &= p_shift(c); }
    return a;
}
static int64_t p_bitxor(cctx* c) {
    int64_t a = p_bitand(c);
    for (;;) { cskip(c); if (c->s[c->pos] != '^') break; c->pos++; a ^= p_bitand(c); }
    return a;
}
static int64_t p_bitor(cctx* c) {
    int64_t a = p_bitxor(c);
    for (;;) { cskip(c); if (c->s[c->pos] != '|') break; c->pos++; a |= p_bitxor(c); }
    return a;
}

int calc_eval(const char* expr, int64_t* out) {
    cctx c = { expr, 0, CE_OK };
    cskip(&c);
    if (c.s[c.pos] == '\0') return -CE_EMPTY;
    int64_t v = p_bitor(&c);
    cskip(&c);
    if (!c.err && c.s[c.pos] != '\0') c.err = CE_TRAILING;
    if (c.err) return -c.err;
    *out = v;
    return 0;
}

int calc_selftest(void) {
    struct { const char* e; int rc; int64_t v; } t[] = {
        {"2+3*4", 0, 14}, {"(2+3)*4", 0, 20}, {"10-2-3", 0, 5}, {"20/4/5", 0, 1}, {"2*3%4", 0, 2},
        {"-17%5", 0, -2}, {"17%-5", 0, 2}, {"-17/5", 0, -3}, {"1|2&3", 0, 3}, {"8>>1+1", 0, 2},
        {"1<<4", 0, 16}, {"~0", 0, -1}, {"0xff+1", 0, 256}, {"0b1010", 0, 10}, {"2*(3+4)-1", 0, 13},
        {"1++2", 0, 3}, {"2<<3>>1", 0, 8},
        {"1/0", -3, 0}, {"1<<64", -5, 0}, {"(1+2", -2, 0}, {"1 2", -4, 0}, {"", -6, 0}, {"abc", -1, 0},
    };
    for (int i = 0; i < (int)(sizeof(t) / sizeof(t[0])); i++) {
        int64_t out = 0;
        int rc = calc_eval(t[i].e, &out);
        if (rc != t[i].rc) return 10 + i;
        if (rc == 0 && out != t[i].v) return 40 + i;
    }
    return 0;
}
