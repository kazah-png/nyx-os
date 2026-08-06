#include "base32.h"

// Standard Base32 alphabet (RFC 4648 §6).
static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

// Alphabet value 0..31, or -1 for any non-alphabet byte ('=' included: callers
// handle padding explicitly, so it must not decode to a data value).
static int b32val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

uint32_t base32_encode(const uint8_t* in, uint32_t in_len, char* out) {
    uint32_t o = 0, i = 0;
    for (; i + 5 <= in_len; i += 5) {                 // full 40-bit groups -> 8 symbols
        uint64_t v = ((uint64_t)in[i] << 32) | ((uint64_t)in[i + 1] << 24) |
                     ((uint64_t)in[i + 2] << 16) | ((uint64_t)in[i + 3] << 8) | in[i + 4];
        for (int j = 0; j < 8; j++) out[o++] = B32[(v >> (5 * (7 - j))) & 31];
    }
    uint32_t rem = in_len - i;
    if (rem) {
        uint64_t v = 0;
        for (uint32_t j = 0; j < rem; j++) v |= (uint64_t)in[i + j] << (32 - 8 * j);
        int nsym = (rem == 1) ? 2 : (rem == 2) ? 4 : (rem == 3) ? 5 : 7;   // symbols carrying data
        for (int j = 0; j < nsym; j++) out[o++] = B32[(v >> (5 * (7 - j))) & 31];
        for (int j = nsym; j < 8; j++) out[o++] = '=';                     // pad the group to 8
    }
    out[o] = '\0';
    return o;
}

int base32_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len) {
    if (in_len % 8 != 0) return -1;                  // padded Base32 is always a multiple of 8
    uint32_t o = 0;
    for (uint32_t i = 0; i < in_len; i += 8) {
        int pad = 0;
        for (int j = 7; j >= 0 && in[i + j] == '='; j--) pad++;   // trailing '=' count
        if (pad > 0 && i + 8 != in_len) return -1;   // padding only in the final group
        int nbytes;                                  // the only pad counts a real byte length yields
        switch (pad) {
            case 0: nbytes = 5; break;  case 1: nbytes = 4; break;  case 3: nbytes = 3; break;
            case 4: nbytes = 2; break;  case 6: nbytes = 1; break;  default: return -1;
        }
        int nsym = 8 - pad;
        uint64_t v = 0;
        for (int j = 0; j < nsym; j++) {
            int s = b32val(in[i + j]);
            if (s < 0) return -1;                    // non-alphabet (or a '=' among the data symbols)
            v |= (uint64_t)s << (5 * (7 - j));
        }
        for (int j = nsym; j < 8; j++) if (in[i + j] != '=') return -1;    // the rest must be padding
        int unused = 40 - 8 * nbytes;                // bits below the last data byte must be zero
        if (unused > 0 && (v & (((uint64_t)1 << unused) - 1)) != 0) return -1;
        for (int j = 0; j < nbytes; j++) out[o++] = (uint8_t)(v >> (32 - 8 * j));
    }
    *out_len = o;
    return 0;
}

// ---- Known-answer self-test (CI battery) ----------------------------------
// RFC 4648 §10 Base32 test vectors both ways, plus malformed inputs the strict
// decoder must reject. Returns 0 on full pass, else the failing step.
static int b32_str_eq(const char* a, const char* b) {
    uint32_t i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

int base32_selftest(void) {
    struct { const char* raw; const char* enc; } v[] = {
        {"",       ""},
        {"f",      "MY======"},
        {"fo",     "MZXQ===="},
        {"foo",    "MZXW6==="},
        {"foob",   "MZXW6YQ="},
        {"fooba",  "MZXW6YTB"},
        {"foobar", "MZXW6YTBOI======"},
    };
    char enc[64];
    uint8_t dec[64];
    uint32_t dlen;
    for (int i = 0; i < 7; i++) {
        uint32_t rl = 0; while (v[i].raw[rl]) rl++;
        base32_encode((const uint8_t*)v[i].raw, rl, enc);          // encode matches the vector
        if (!b32_str_eq(enc, v[i].enc)) return 1;
        uint32_t el = 0; while (v[i].enc[el]) el++;
        if (base32_decode(v[i].enc, el, dec, &dlen) != 0) return 2;   // decode round-trips
        if (dlen != rl) return 3;
        for (uint32_t k = 0; k < rl; k++) if (dec[k] != (uint8_t)v[i].raw[k]) return 4;
    }
    const char* bad[] = {
        "MY=====",           // length not a multiple of 8
        "MZXW6YT1",          // '1' is not in the Base32 alphabet
        "MZ======",          // non-canonical: the last data symbol's unused bits are set
        "MZXW6YQ=MZXW6YQ=",  // padding in a non-final group
        "MY==MY==",          // padding not trailing / an invalid pad count
        "========",          // all padding
        "MYA=====",          // pad count 5 — no byte length produces it
    };
    for (int i = 0; i < 7; i++) {
        uint32_t bl = 0; while (bad[i][bl]) bl++;
        if (base32_decode(bad[i], bl, dec, &dlen) != -1) return 5;
    }
    if (base32_encoded_len(1) != 8 || base32_encoded_len(5) != 8 ||
        base32_encoded_len(6) != 16) return 6;
    return 0;
}
