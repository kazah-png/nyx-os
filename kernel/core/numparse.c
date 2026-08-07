// ============================================================
// numparse.c - strict base-10 integer parsing with overflow detection + KAT (v6.4.126)
// ============================================================
// kernel.h's atoi()/strtol() are the lenient, allocation-free helpers used all over the
// command layer, but they are unsafe for validating untrusted numeric input: atoi("abc")
// and atoi("0") both return 0 (no way to distinguish garbage from a real zero), and both
// silently overflow — atoi("99999999999999") wraps to some unrelated int, and the common
// `(uint16_t)atoi("99999")` quietly targets port 34463 instead of erroring. parse_i64 /
// parse_u64 / parse_u32_max are the strict counterparts: they validate the entire string
// and detect overflow BEFORE it happens, returning -1 rather than a silently-wrong value.
// This is the same "strict parser for config, lenient parser for heuristics" split that
// ipv4_parse() introduced for addresses. Pinned by numparse_selftest().
#include "kernel.h"
#include "numparse.h"

static int np_is_blank(char c) { return c == ' ' || c == '\t'; }

// Read the unsigned decimal magnitude at *sp, rejecting any value that would exceed
// `limit`. Advances *sp past the digits consumed. Returns 0 on success (at least one
// digit and no overflow), -1 otherwise. The overflow test is done in the accumulator's
// own width so the accumulator itself never wraps.
static int np_mag(const char** sp, uint64_t limit, uint64_t* out) {
    const char* s = *sp;
    if (*s < '0' || *s > '9') return -1;                 // need at least one digit
    uint64_t acc = 0;
    while (*s >= '0' && *s <= '9') {
        uint64_t d = (uint64_t)(*s - '0');
        // Reject when acc*10 + d would exceed limit, computed so neither side overflows:
        //   acc*10 + d > limit   <=>   acc > (limit - d) / 10     (limit >= 9 >= d always)
        if (acc > (limit - d) / 10) return -1;
        acc = acc * 10 + d;
        s++;
    }
    *sp = s;
    *out = acc;
    return 0;
}

int parse_u64(const char* s, uint64_t* out) {
    if (!s || !out) return -1;
    while (np_is_blank(*s)) s++;
    if (*s == '+') s++;                                  // a leading '+' is allowed; '-' is not
    uint64_t v;
    if (np_mag(&s, 0xFFFFFFFFFFFFFFFFULL, &v) != 0) return -1;
    while (np_is_blank(*s)) s++;
    if (*s != '\0') return -1;                           // trailing junk
    *out = v;
    return 0;
}

int parse_i64(const char* s, int64_t* out) {
    if (!s || !out) return -1;
    while (np_is_blank(*s)) s++;
    int neg = 0;
    if (*s == '+') s++;
    else if (*s == '-') { neg = 1; s++; }
    // A positive magnitude caps at INT64_MAX (9223372036854775807); a negative one at
    // INT64_MAX + 1 (9223372036854775808 == -INT64_MIN), so INT64_MIN stays representable.
    uint64_t limit = neg ? 0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFULL;
    uint64_t v;
    if (np_mag(&s, limit, &v) != 0) return -1;
    while (np_is_blank(*s)) s++;
    if (*s != '\0') return -1;                           // trailing junk
    // Negate in unsigned space so v == 2^63 becomes INT64_MIN with no signed overflow (UB).
    *out = neg ? (int64_t)(~v + 1ULL) : (int64_t)v;
    return 0;
}

int parse_u32_max(const char* s, uint32_t max, uint32_t* out) {
    if (!out) return -1;
    uint64_t v;
    if (parse_u64(s, &v) != 0) return -1;
    if (v > (uint64_t)max) return -1;
    *out = (uint32_t)v;
    return 0;
}

// KAT: boundary values that MUST round-trip exactly, plus a wide set of malformed and
// out-of-range strings that MUST be rejected. Like ipv4_parse_selftest this is adversarial
// — the whole point is that hostile / mistyped input never yields a silently-wrong number.
int numparse_selftest(void) {
    // --- signed: boundary + representative values ---
    struct { const char* s; int64_t v; } iok[] = {
        { "0", 0 }, { "123", 123 }, { "-123", -123 }, { "+42", 42 },
        { "007", 7 }, { "  -1  ", -1 },
        { "9223372036854775807",  (int64_t)0x7FFFFFFFFFFFFFFFLL },   // INT64_MAX
        { "-9223372036854775808", (int64_t)0x8000000000000000ULL }, // INT64_MIN
    };
    for (int i = 0; i < (int)(sizeof(iok) / sizeof(iok[0])); i++) {
        int64_t v = (int64_t)0xABCD;
        if (parse_i64(iok[i].s, &v) != 0) return 1;
        if (v != iok[i].v) return 2;
    }
    // --- unsigned: boundary + representative values ---
    struct { const char* s; uint64_t v; } uok[] = {
        { "0", 0 }, { "255", 255 }, { "+1000000000000", 1000000000000ULL },
        { "  4096 ", 4096 },
        { "18446744073709551615", 0xFFFFFFFFFFFFFFFFULL },           // UINT64_MAX
    };
    for (int i = 0; i < (int)(sizeof(uok) / sizeof(uok[0])); i++) {
        uint64_t v = 0xABCD;
        if (parse_u64(uok[i].s, &v) != 0) return 3;
        if (v != uok[i].v) return 4;
    }
    // --- adversarial: rejected by BOTH parsers ---
    const char* bad[] = {
        "", "   ", "+", "-", "abc", "12abc", "1 2", "0x10", " 0x1", "1.5",
        "99999999999999999999999999",   // far past u64
        "\t", "- 1", "+ 1",
    };
    for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
        int64_t a; uint64_t b;
        if (parse_i64(bad[i], &a) != -1) return 5;
        if (parse_u64(bad[i], &b) != -1) return 6;
    }
    // --- range-specific overflow (accepted by the wider type, rejected by the narrower) ---
    int64_t a; uint64_t b;
    if (parse_i64("9223372036854775808", &a)  != -1) return 7;   // INT64_MAX + 1
    if (parse_i64("-9223372036854775809", &a) != -1) return 8;   // INT64_MIN - 1
    if (parse_u64("18446744073709551616", &b) != -1) return 9;   // UINT64_MAX + 1
    if (parse_u64("-1", &b) != -1) return 10;                    // unsigned rejects a sign
    // the INT64_MAX+1 magnitude is still a valid *unsigned* value:
    if (parse_u64("9223372036854775808", &b) != 0 || b != 0x8000000000000000ULL) return 11;
    // --- bounded helper (the safe port parser) ---
    uint32_t p;
    if (parse_u32_max("65535", 65535, &p) != 0 || p != 65535) return 12;
    if (parse_u32_max("80", 65535, &p)    != 0 || p != 80)    return 13;
    if (parse_u32_max("0", 65535, &p)     != 0 || p != 0)     return 14;
    if (parse_u32_max("65536", 65535, &p) != -1) return 15;      // the (uint16_t)atoi footgun
    if (parse_u32_max("70000", 65535, &p) != -1) return 16;
    if (parse_u32_max("abc", 65535, &p)   != -1) return 17;
    if (parse_u32_max("-1", 65535, &p)    != -1) return 18;
    // --- NULL / NULL-out guards must fail, not crash ---
    if (parse_i64((const char*)0, &a)          != -1) return 19;
    if (parse_u64("1", (uint64_t*)0)           != -1) return 20;
    if (parse_u32_max("1", 100, (uint32_t*)0)  != -1) return 21;
    return 0;
}
