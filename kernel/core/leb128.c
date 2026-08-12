// ============================================================
// leb128.c - LEB128 variable-length integer coding + KAT (see leb128.h)
// ============================================================
#include "leb128.h"

int uleb128_encode(uint64_t v, uint8_t* out, int outcap) {
    int n = 0;
    do {
        if (n >= outcap) return -1;
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;
        if (v) b |= 0x80;               // more bytes follow
        out[n++] = b;
    } while (v);
    return n;
}

int uleb128_decode(const uint8_t* in, int inlen, uint64_t* out) {
    uint64_t r = 0;
    int shift = 0, n = 0;
    for (;;) {
        if (shift >= 64) return -1;               // an 11th continuation byte -> > 64 bits
        if (n >= inlen)   return -1;              // continuation set but input ended
        uint8_t b = in[n++];
        if (shift == 63 && (b & 0x7E)) return -1; // payload bits that would run past bit 63
        r |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { if (out) *out = r; return n; }
        shift += 7;
    }
}

int sleb128_encode(int64_t v, uint8_t* out, int outcap) {
    int n = 0, more = 1;
    while (more) {
        if (n >= outcap) return -1;
        uint8_t b = (uint8_t)(v & 0x7F);
        v >>= 7;                                  // arithmetic shift: sign-extends
        // terminate once the remaining value is all-sign-bits AND the byte's sign bit
        // (0x40) already carries that sign; otherwise emit another byte.
        if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40))) more = 0;
        else b |= 0x80;
        out[n++] = b;
    }
    return n;
}

int sleb128_decode(const uint8_t* in, int inlen, int64_t* out) {
    int64_t r = 0;
    int shift = 0, n = 0;
    uint8_t b = 0;
    for (;;) {
        if (shift >= 64) return -1;               // overlong -> would exceed 64 bits
        if (n >= inlen)   return -1;              // truncated
        b = in[n++];
        r |= (int64_t)((uint64_t)(b & 0x7F) << shift);
        shift += 7;
        if (!(b & 0x80)) break;
    }
    if (shift < 64 && (b & 0x40)) r |= -((int64_t)1 << shift);   // sign-extend
    if (out) *out = r;
    return n;
}

// ---- known-answer self-test (`leb128`) ----
static int bytes_eq(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int leb128_selftest(void) {
    uint8_t buf[16];

    // --- unsigned: canonical encodings + round-trip ---
    struct { uint64_t v; uint8_t enc[10]; int len; } uv[] = {
        {        0u, {0x00},                                     1 },
        {        1u, {0x01},                                     1 },
        {      127u, {0x7F},                                     1 },
        {      128u, {0x80, 0x01},                               2 },
        {      300u, {0xAC, 0x02},                               2 },
        {   624485u, {0xE5, 0x8E, 0x26},                         3 },
        { 0xFFFFFFFFFFFFFFFFull,
          {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01}, 10 },
    };
    for (int i = 0; i < (int)(sizeof(uv)/sizeof(uv[0])); i++) {
        int n = uleb128_encode(uv[i].v, buf, sizeof(buf));
        if (n != uv[i].len || !bytes_eq(buf, uv[i].enc, n)) return 1;
        uint64_t d = 0;
        int m = uleb128_decode(uv[i].enc, uv[i].len, &d);
        if (m != uv[i].len || d != uv[i].v) return 2;
    }

    // --- signed: canonical encodings + round-trip ---
    struct { int64_t v; uint8_t enc[10]; int len; } sv[] = {
        {        0, {0x00},             1 },
        {       -1, {0x7F},             1 },
        {       63, {0x3F},             1 },
        {       64, {0xC0, 0x00},       2 },
        {      -64, {0x40},             1 },
        {      127, {0xFF, 0x00},       2 },
        {     -128, {0x80, 0x7F},       2 },
        {  -123456, {0xC0, 0xBB, 0x78}, 3 },
    };
    for (int i = 0; i < (int)(sizeof(sv)/sizeof(sv[0])); i++) {
        int n = sleb128_encode(sv[i].v, buf, sizeof(buf));
        if (n != sv[i].len || !bytes_eq(buf, sv[i].enc, n)) return 3;
        int64_t d = 0;
        int m = sleb128_decode(sv[i].enc, sv[i].len, &d);
        if (m != sv[i].len || d != sv[i].v) return 4;
    }

    // --- malformed rejections (bounds + overflow safety) ---
    uint64_t du; int64_t ds;
    const uint8_t trunc[] = {0x80, 0x80};                 // continuation set, then EOF
    if (uleb128_decode(trunc, 2, &du) != -1) return 5;
    if (sleb128_decode(trunc, 2, &ds) != -1) return 6;
    const uint8_t overlong[] = {0x80,0x80,0x80,0x80,0x80, // 11 bytes -> > 64 bits
                                0x80,0x80,0x80,0x80,0x80,0x00};
    if (uleb128_decode(overlong, 11, &du) != -1) return 7;
    const uint8_t past63[] = {0xFF,0xFF,0xFF,0xFF,0xFF,   // 10th byte carries payload past bit 63
                              0xFF,0xFF,0xFF,0xFF,0x02};
    if (uleb128_decode(past63, 10, &du) != -1) return 8;
    // a decode into a too-short buffer must fail, not read past the end
    if (uleb128_decode(uv[3].enc, 1, &du) != -1) return 9;   // 128 needs 2 bytes, only 1 given

    return 0;
}
