#include "../core/kernel.h"
#include "utf8.h"

// Strict UTF-8 decoder following the Unicode Standard's Table 3-7 (well-formed byte
// sequences). Decodes ONE code point from s[0..len): on success returns the number of
// bytes consumed (1..4) and writes the code point to *cp; on any malformed sequence
// returns 0. It rejects overlong encodings, UTF-16 surrogates (U+D800..U+DFFF), code
// points above U+10FFFF, truncated sequences, stray continuation bytes, and the
// invalid lead bytes C0/C1 and F5..FF. This is the safe way to consume UNTRUSTED text
// (filenames, HTML, headers): a NUL smuggled as C0 80 or a surrogate injected as
// ED A0 80 does not slip through.
int utf8_decode(const uint8_t* s, size_t len, uint32_t* cp) {
    if (len == 0) return 0;
    uint8_t b0 = s[0];

    if (b0 <= 0x7F) { *cp = b0; return 1; }                 // ASCII

    if (b0 >= 0xC2 && b0 <= 0xDF) {                          // 2-byte (C0/C1 = overlong)
        if (len < 2) return 0;
        uint8_t b1 = s[1];
        if (b1 < 0x80 || b1 > 0xBF) return 0;
        *cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        return 2;
    }

    if (b0 >= 0xE0 && b0 <= 0xEF) {                          // 3-byte
        if (len < 3) return 0;
        uint8_t b1 = s[1], b2 = s[2];
        uint8_t lo = 0x80, hi = 0xBF;
        if (b0 == 0xE0) lo = 0xA0;                           // reject E0 80..9F (overlong)
        if (b0 == 0xED) hi = 0x9F;                           // reject ED A0..BF (surrogates)
        if (b1 < lo || b1 > hi) return 0;
        if (b2 < 0x80 || b2 > 0xBF) return 0;
        *cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
        return 3;
    }

    if (b0 >= 0xF0 && b0 <= 0xF4) {                          // 4-byte
        if (len < 4) return 0;
        uint8_t b1 = s[1], b2 = s[2], b3 = s[3];
        uint8_t lo = 0x80, hi = 0xBF;
        if (b0 == 0xF0) lo = 0x90;                           // reject F0 80..8F (overlong)
        if (b0 == 0xF4) hi = 0x8F;                           // reject F4 90..BF (> U+10FFFF)
        if (b1 < lo || b1 > hi) return 0;
        if (b2 < 0x80 || b2 > 0xBF) return 0;
        if (b3 < 0x80 || b3 > 0xBF) return 0;
        *cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) |
              ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
        return 4;
    }

    return 0;   // 80..BF stray continuation, C0/C1, F5..FF: invalid lead bytes
}

// 1 if the whole buffer is valid UTF-8, else 0. An embedded NUL is a valid U+0000;
// callers that forbid NUL should check for it separately.
int utf8_validate(const uint8_t* s, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        int n = utf8_decode(s + i, len - i, &cp);
        if (n <= 0) return 0;
        i += (size_t)n;
    }
    return 1;
}

// KAT: valid sequences (incl. the surrogate boundary and U+10FFFF) accept and decode
// to the right code point; a battery of malformed sequences — overlong, surrogate,
// out-of-range, truncated, stray continuation, invalid lead — all reject.
int utf8_selftest(void) {
    static const uint8_t v_ascii[] = { 'A','a','9' };
    static const uint8_t v_e9[]    = { 0xC3,0xA9 };                 // U+00E9  é
    static const uint8_t v_euro[]  = { 0xE2,0x82,0xAC };           // U+20AC  €
    static const uint8_t v_emo[]   = { 0xF0,0x9F,0x98,0x80 };      // U+1F600 😀
    static const uint8_t v_d7ff[]  = { 0xED,0x9F,0xBF };           // U+D7FF (just below surrogates)
    static const uint8_t v_e000[]  = { 0xEE,0x80,0x80 };          // U+E000 (just above surrogates)
    static const uint8_t v_max[]   = { 0xF4,0x8F,0xBF,0xBF };     // U+10FFFF (max)
    static const uint8_t v_nul[]   = { 0x00,0x41 };               // embedded NUL + 'A'
    static const uint8_t v_mix[]   = { 'a',0xC3,0xA9,0xE2,0x82,0xAC,0xF0,0x9F,0x98,0x80 };

    if (!utf8_validate((const uint8_t*)"", 0))       return 1;
    if (!utf8_validate(v_ascii, sizeof v_ascii))     return 2;
    if (!utf8_validate(v_e9,    sizeof v_e9))         return 3;
    if (!utf8_validate(v_euro,  sizeof v_euro))       return 4;
    if (!utf8_validate(v_emo,   sizeof v_emo))        return 5;
    if (!utf8_validate(v_d7ff,  sizeof v_d7ff))       return 6;
    if (!utf8_validate(v_e000,  sizeof v_e000))       return 7;
    if (!utf8_validate(v_max,   sizeof v_max))        return 8;
    if (!utf8_validate(v_nul,   sizeof v_nul))        return 9;
    if (!utf8_validate(v_mix,   sizeof v_mix))        return 10;

    struct { const uint8_t* p; size_t n; } bad[] = {
        { (const uint8_t[]){ 0xC0,0x80 }, 2 },                  // overlong NUL
        { (const uint8_t[]){ 0xC1,0xBF }, 2 },                  // overlong
        { (const uint8_t[]){ 0xE0,0x80,0xAF }, 3 },             // overlong '/'
        { (const uint8_t[]){ 0xED,0xA0,0x80 }, 3 },             // surrogate U+D800
        { (const uint8_t[]){ 0xED,0xBF,0xBF }, 3 },             // surrogate U+DFFF
        { (const uint8_t[]){ 0xF0,0x80,0x80,0x80 }, 4 },        // overlong
        { (const uint8_t[]){ 0xF4,0x90,0x80,0x80 }, 4 },        // > U+10FFFF
        { (const uint8_t[]){ 0xF5,0x80,0x80,0x80 }, 4 },        // invalid lead
        { (const uint8_t[]){ 0xC3 }, 1 },                       // truncated 2-byte
        { (const uint8_t[]){ 0xE2,0x82 }, 2 },                  // truncated 3-byte
        { (const uint8_t[]){ 0xF0,0x9F,0x98 }, 3 },             // truncated 4-byte
        { (const uint8_t[]){ 0x80 }, 1 },                       // stray continuation
        { (const uint8_t[]){ 0xBF }, 1 },                       // stray continuation
        { (const uint8_t[]){ 0xC3,0x20 }, 2 },                  // bad continuation
        { (const uint8_t[]){ 0xE2,0x82,0x20 }, 3 },             // bad continuation
        { (const uint8_t[]){ 0xFF }, 1 },                       // invalid byte
        { (const uint8_t[]){ 0xFE }, 1 },                       // invalid byte
        { (const uint8_t[]){ 'A',0xC0,0x80 }, 3 },              // valid then overlong
    };
    for (int i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++)
        if (utf8_validate(bad[i].p, bad[i].n)) return 100 + i;

    uint32_t cp;
    if (utf8_decode((const uint8_t[]){ 0x41 }, 1, &cp) != 1 || cp != 0x41)     return 40;
    if (utf8_decode(v_e9,   2, &cp) != 2 || cp != 0x00E9)                      return 41;
    if (utf8_decode(v_euro, 3, &cp) != 3 || cp != 0x20AC)                      return 42;
    if (utf8_decode(v_emo,  4, &cp) != 4 || cp != 0x1F600)                     return 43;
    if (utf8_decode(v_max,  4, &cp) != 4 || cp != 0x10FFFF)                    return 44;
    return 0;
}
