#include "base85.h"

// Ascii85 digit d (0..84) maps to the printable char '!'+d, so the alphabet is
// '!'(0x21) .. 'u'(0x75); a full-zero 4-byte group is written as the single char 'z'.

uint32_t base85_encode(const uint8_t* in, uint32_t in_len, char* out) {
    uint32_t o = 0, i = 0;
    for (; i + 4 <= in_len; i += 4) {
        uint32_t v = ((uint32_t)in[i] << 24) | ((uint32_t)in[i + 1] << 16) |
                     ((uint32_t)in[i + 2] << 8) | in[i + 3];
        if (v == 0) { out[o++] = 'z'; continue; }        // all-zero group shorthand
        char t[5];
        for (int k = 4; k >= 0; k--) { t[k] = (char)('!' + v % 85); v /= 85; }
        for (int k = 0; k < 5; k++) out[o++] = t[k];
    }
    uint32_t rem = in_len - i;                           // 0..3 trailing bytes
    if (rem) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 4; k++) v = (v << 8) | (k < rem ? in[i + k] : 0);
        char t[5];
        for (int k = 4; k >= 0; k--) { t[k] = (char)('!' + v % 85); v /= 85; }
        for (uint32_t k = 0; k < rem + 1; k++) out[o++] = t[k];   // trim to rem+1 chars
    }
    out[o] = '\0';
    return o;
}

int base85_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len) {
    uint32_t o = 0;
    uint64_t v = 0;                                      // 5 base-85 digits can exceed 2^32
    int cnt = 0;
    for (uint32_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == 'z') {
            if (cnt != 0) return -1;                     // 'z' only on a group boundary
            out[o++] = 0; out[o++] = 0; out[o++] = 0; out[o++] = 0;
            continue;
        }
        if (c < '!' || c > 'u') return -1;               // outside the Ascii85 alphabet
        v = v * 85 + (uint32_t)(c - '!');
        if (++cnt == 5) {
            if (v > 0xFFFFFFFFULL) return -1;            // group out of 32-bit range
            out[o++] = (uint8_t)(v >> 24); out[o++] = (uint8_t)(v >> 16);
            out[o++] = (uint8_t)(v >> 8);  out[o++] = (uint8_t)v;
            v = 0; cnt = 0;
        }
    }
    if (cnt == 1) return -1;                             // a lone trailing char is invalid
    if (cnt > 0) {                                       // partial group: pad with 'u' (84)
        for (int k = cnt; k < 5; k++) v = v * 85 + 84;
        if (v > 0xFFFFFFFFULL) return -1;
        uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
        for (int k = 0; k < cnt - 1; k++) out[o++] = b[k];
    }
    *out_len = o;
    return 0;
}

// ---- Known-answer self-test (CI battery) ----------------------------------
// Vectors from Python base64.a85encode; encode must match exactly and decode must
// round-trip, plus a battery of malformed inputs the strict decoder must reject.
static int b85_str_eq(const char* a, const char* b) {
    uint32_t i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

int base85_selftest(void) {
    char enc[64];
    uint8_t dec[64];
    uint32_t dlen;

    // printable raw -> Ascii85
    struct { const char* raw; const char* enc; } v[] = {
        {"",            ""},
        {"M",           "9`"},
        {"Ma",          "9jn"},
        {"Man",         "9jqo"},
        {"Man ",        "9jqo^"},
        {"sure.",       "F*2M7/c"},
        {"hello world", "BOu!rD]j7BEbo7"},
    };
    for (int i = 0; i < 7; i++) {
        uint32_t rl = 0; while (v[i].raw[rl]) rl++;
        base85_encode((const uint8_t*)v[i].raw, rl, enc);
        if (!b85_str_eq(enc, v[i].enc)) return 1;
        uint32_t el = 0; while (v[i].enc[el]) el++;
        if (base85_decode(v[i].enc, el, dec, &dlen) != 0) return 2;
        if (dlen != rl) return 3;
        for (uint32_t k = 0; k < rl; k++) if (dec[k] != (uint8_t)v[i].raw[k]) return 4;
    }

    // binary vectors (zero-group 'z', boundary values, mixed)
    struct { const uint8_t* raw; uint32_t rl; const char* enc; } b[] = {
        {(const uint8_t*)"\x00\x00\x00\x00",         4, "z"},
        {(const uint8_t*)"\x01\x02\x03\x04",         4, "!<N?+"},
        {(const uint8_t*)"\xff\xff\xff\xff",         4, "s8W-!"},
        {(const uint8_t*)"\x00\x00\x00\x00Man",      7, "z9jqo"},
    };
    for (int i = 0; i < 4; i++) {
        base85_encode(b[i].raw, b[i].rl, enc);
        if (!b85_str_eq(enc, b[i].enc)) return 10 + i;
        uint32_t el = 0; while (b[i].enc[el]) el++;
        if (base85_decode(b[i].enc, el, dec, &dlen) != 0) return 20 + i;
        if (dlen != b[i].rl) return 30 + i;
        for (uint32_t k = 0; k < b[i].rl; k++) if (dec[k] != b[i].raw[k]) return 40 + i;
    }

    // malformed inputs — every one must be rejected
    const char* bad[] = {
        "9",       // a lone trailing char (a group needs >= 2)
        "9j|",     // '|' (0x7C) is outside '!'..'u'
        "uuuuu",   // 85^5-1 = 4437053124 > 2^32-1: group overflow
        "!z",      // 'z' not on a group boundary
        "s8W-\"",  // 0xffffffff + 1 group: "s8W-\"" = 2^32 -> overflow
    };
    for (int i = 0; i < 5; i++) {
        uint32_t bl = 0; while (bad[i][bl]) bl++;
        if (base85_decode(bad[i], bl, dec, &dlen) != -1) return 50 + i;
    }

    // encoded-length upper bound agrees for a few sizes
    if (base85_encoded_len(1) != 2 || base85_encoded_len(3) != 4 ||
        base85_encoded_len(4) != 5 || base85_encoded_len(5) != 7) return 60;
    return 0;
}
