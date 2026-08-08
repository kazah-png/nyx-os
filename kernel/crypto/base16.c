// ============================================================
// Base16 / hex codec (RFC 4648 §8) — strict decode.
// ============================================================
#include "base16.h"

static const char UP[] = "0123456789ABCDEF";

uint32_t base16_encode(const uint8_t* in, uint32_t in_len, char* out) {
    uint32_t o = 0;
    for (uint32_t i = 0; i < in_len; i++) {
        out[o++] = UP[in[i] >> 4];
        out[o++] = UP[in[i] & 0x0f];
    }
    out[o] = 0;
    return o;
}

// Hex value of one symbol, or -1 if it is not a hex digit (either case).
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int base16_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len) {
    if (in_len & 1u) return -1;                       // odd length
    uint32_t o = 0;
    for (uint32_t i = 0; i < in_len; i += 2) {
        int hi = hexval(in[i]), lo = hexval(in[i + 1]);
        if (hi < 0 || lo < 0) return -1;              // non-hex symbol (incl. whitespace)
        out[o++] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = o;
    return 0;
}

// ---------------- Known-answer test ----------------
static uint32_t slen(const char* s) { uint32_t n = 0; while (s[n]) n++; return n; }
static int streq(const char* a, const char* b) { for (uint32_t i = 0; ; i++) { if (a[i] != b[i]) return 0; if (!a[i]) return 1; } }

int base16_selftest(void) {
    char enc[64]; uint8_t dec[32]; uint32_t dl;

    // RFC 4648 §10 Base16 test vectors (uppercase).
    static const struct { const char* s; const char* hex; } v[] = {
        { "",       ""            },
        { "f",      "66"          },
        { "fo",     "666F"        },
        { "foo",    "666F6F"      },
        { "foob",   "666F6F62"    },
        { "fooba",  "666F6F6261"  },
        { "foobar", "666F6F626172"},
    };
    for (int i = 0; i < 7; i++) {
        uint32_t n = slen(v[i].s);
        base16_encode((const uint8_t*)v[i].s, n, enc);
        if (!streq(enc, v[i].hex)) return i + 1;                       // encode
        if (base16_decode(v[i].hex, slen(v[i].hex), dec, &dl) != 0) return 10 + i;  // decode ok
        if (dl != n) return 20 + i;
        for (uint32_t k = 0; k < n; k++) if (dec[k] != (uint8_t)v[i].s[k]) return 30 + i;
    }

    // Decoding accepts lowercase and mixed case.
    if (base16_decode("deadBEEF", 8, dec, &dl) != 0 || dl != 4) return 40;
    if (dec[0] != 0xde || dec[1] != 0xad || dec[2] != 0xbe || dec[3] != 0xef) return 41;

    // Strict rejects: odd length, non-hex symbols, embedded whitespace.
    if (base16_decode("abc", 3, dec, &dl) == 0) return 42;   // odd length
    if (base16_decode("6G", 2, dec, &dl) == 0) return 43;    // non-hex
    if (base16_decode("zz", 2, dec, &dl) == 0) return 44;    // non-hex
    if (base16_decode("66 ", 3, dec, &dl) == 0) return 45;   // odd (trailing space)
    if (base16_decode("6 6", 3, dec, &dl) == 0) return 46;   // odd (embedded space)
    return 0;
}
