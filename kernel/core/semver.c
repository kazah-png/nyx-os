// ============================================================
// semver.c - strict Semantic Versioning 2.0.0 parser + precedence comparator (see semver.h).
// Pure logic. The `semver` command + KAT use it; the KAT is the semver.org §11 ordering.
// ============================================================
#include "semver.h"

static int is_dig(char c) { return c >= '0' && c <= '9'; }
static int is_id(char c)  { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                   (c >= 'a' && c <= 'z') || c == '-'; }
static int all_dig(const char* s, int n) {
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) if (!is_dig(s[i])) return 0;
    return 1;
}

// Validate a dot-separated identifier list in [s, end): each identifier non-empty and made
// of [0-9A-Za-z-]; if `strict_num` a numeric identifier must not have a leading zero.
// Returns 0 if valid, -1 otherwise.
static int check_id_list(const char* s, const char* end, int strict_num) {
    const char* cur = s;
    int len = 0;
    for (const char* q = s;; q++) {
        if (q == end || *q == '.') {
            if (len == 0) return -1;                                  // empty identifier
            if (strict_num && all_dig(cur, len) && len > 1 && cur[0] == '0') return -1;
            if (q == end) return 0;
            cur = q + 1; len = 0;                                     // next identifier
        } else {
            if (!is_id(*q)) return -1;
            len++;
        }
    }
}

int semver_parse(const char* s, semver_t* v) {
    v->valid = 0; v->pre = 0; v->pre_len = 0;
    if (!s || !*s) return -1;
    const char* p = s;

    uint64_t core[3];
    for (int i = 0; i < 3; i++) {
        if (!is_dig(*p)) return -1;
        const char* d = p;
        uint64_t val = 0; int nd = 0;
        while (is_dig(*p)) { val = val * 10 + (uint64_t)(*p - '0'); p++; if (++nd > 18) return -1; }
        if (nd > 1 && d[0] == '0') return -1;                        // no leading zero
        core[i] = val;
        if (i < 2) { if (*p != '.') return -1; p++; }
    }
    v->major = core[0]; v->minor = core[1]; v->patch = core[2];

    if (*p == '-') {                                                 // prerelease
        const char* ps = ++p;
        while (*p && *p != '+') p++;
        if (check_id_list(ps, p, 1) != 0) return -1;
        v->pre = ps; v->pre_len = (int)(p - ps);
    }
    if (*p == '+') {                                                 // build metadata (ignored)
        const char* bs = ++p;
        while (*p) p++;
        if (check_id_list(bs, p, 0) != 0) return -1;
    }
    if (*p != 0) return -1;                                          // trailing garbage
    v->valid = 1;
    return 0;
}

// Compare two numeric identifiers (no leading zeros): longer string = larger number.
static int cmp_num(const char* a, int an, const char* b, int bn) {
    if (an != bn) return an < bn ? -1 : 1;
    for (int i = 0; i < an; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

// Advance *p past the next dot-separated identifier in [*p, end); return it and set *len.
static const char* next_id(const char** p, const char* end, int* len) {
    const char* s = *p; const char* q = s;
    while (q < end && *q != '.') q++;
    *len = (int)(q - s);
    *p = (q < end) ? q + 1 : q;
    return s;
}

static int cmp_pre(const char* a, int alen, const char* b, int blen) {
    int ah = (alen > 0), bh = (blen > 0);
    if (!ah && !bh) return 0;
    if (!ah) return 1;                                               // no prerelease > has prerelease
    if (!bh) return -1;
    const char* ap = a; const char* ae = a + alen;
    const char* bp = b; const char* be = b + blen;
    for (;;) {
        int am = (ap < ae), bm = (bp < be);
        if (!am && !bm) return 0;
        if (!am) return -1;                                         // fewer identifiers = lower
        if (!bm) return 1;
        int al, bl;
        const char* ai = next_id(&ap, ae, &al);
        const char* bi = next_id(&bp, be, &bl);
        int an = all_dig(ai, al), bn = all_dig(bi, bl);
        int c;
        if (an && bn)       c = cmp_num(ai, al, bi, bl);
        else if (an && !bn) c = -1;                                 // numeric < alphanumeric
        else if (!an && bn) c = 1;
        else {                                                      // both alphanumeric: ASCII
            int m = al < bl ? al : bl; c = 0;
            for (int i = 0; i < m; i++)
                if (ai[i] != bi[i]) { c = (unsigned char)ai[i] < (unsigned char)bi[i] ? -1 : 1; break; }
            if (c == 0 && al != bl) c = al < bl ? -1 : 1;
        }
        if (c) return c;
    }
}

int semver_cmp(const semver_t* a, const semver_t* b) {
    if (a->major != b->major) return a->major < b->major ? -1 : 1;
    if (a->minor != b->minor) return a->minor < b->minor ? -1 : 1;
    if (a->patch != b->patch) return a->patch < b->patch ? -1 : 1;
    return cmp_pre(a->pre, a->pre_len, b->pre, b->pre_len);
}

// ---- known-answer self-test (`semver`) ----
int semver_selftest(void) {
    // 1) the canonical strictly-ascending precedence chain from semver.org §11
    static const char* const chain[] = {
        "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
        "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0",
        "1.0.1", "1.1.0", "1.9.0", "1.10.0", "2.0.0", "2.1.0", "2.1.1",
    };
    int n = (int)(sizeof(chain) / sizeof(chain[0]));
    for (int i = 0; i + 1 < n; i++) {
        semver_t a, b;
        if (semver_parse(chain[i], &a) != 0)     return 1000 + i;
        if (semver_parse(chain[i + 1], &b) != 0) return 2000 + i;
        if (semver_cmp(&a, &b) >= 0) return i + 1;                  // a < b
        if (semver_cmp(&b, &a) <= 0) return 100 + i;                // b > a
        if (semver_cmp(&a, &a) != 0) return 300 + i;                // reflexive
    }
    // 2) build metadata is ignored: 1.0.0+x == 1.0.0 == 1.0.0+y
    semver_t c1, c2, c3;
    if (semver_parse("1.0.0+build.1", &c1) || semver_parse("1.0.0", &c2) ||
        semver_parse("1.0.0+other.7", &c3)) return 400;
    if (semver_cmp(&c1, &c2) != 0 || semver_cmp(&c1, &c3) != 0 || semver_cmp(&c2, &c3) != 0) return 500;
    // 3) invalid strings must be rejected
    static const char* const bad[] = {
        "", "1", "1.0", "1.0.0.0", "01.0.0", "1.00.0", "1.0.0-", "1.0.0-01",
        "1.0.0-a..b", "v1.0.0", "1.0.x", "1.0.0- ", "1.0.0+", "1.0.0+ ",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        semver_t t; if (semver_parse(bad[i], &t) == 0) return 600 + (int)i;
    }
    // 4) valid edge forms must be accepted
    static const char* const good[] = {
        "0.0.0", "1.2.3", "10.20.30", "1.0.0-0", "1.0.0-x.7.z.92",
        "1.0.0-alpha-1", "1.0.0+20130313144700", "1.2.3-beta+exp.sha.5114f85",
    };
    for (unsigned i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        semver_t t; if (semver_parse(good[i], &t) != 0) return 700 + (int)i;
    }
    return 0;
}
