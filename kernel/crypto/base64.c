#include "base64.h"

// Standard Base64 alphabet (RFC 4648 §4).
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Alphabet value 0..63, or -1 for any non-alphabet byte ('=' included: callers
// handle padding explicitly, so it must not decode to a data value).
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

uint32_t base64_encode(const uint8_t* in, uint32_t in_len, char* out) {
    uint32_t o = 0, i = 0;
    for (; i + 3 <= in_len; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = B64[v & 63];
    }
    uint32_t rem = in_len - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

int base64_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len) {
    if (in_len % 4 != 0) return -1;               // padded Base64 is always a multiple of 4
    uint32_t o = 0;
    for (uint32_t i = 0; i < in_len; i += 4) {
        char c0 = in[i], c1 = in[i + 1], c2 = in[i + 2], c3 = in[i + 3];
        int v0 = b64val(c0), v1 = b64val(c1);
        if (v0 < 0 || v1 < 0) return -1;          // the first two symbols are always data

        int pad2 = (c2 == '='), pad3 = (c3 == '=');
        if ((pad2 || pad3) && i + 4 != in_len) return -1;   // padding only in the last quartet
        if (pad2 && !pad3) return -1;                        // "=" cannot precede a data symbol

        int v2 = pad2 ? 0 : b64val(c2);
        int v3 = pad3 ? 0 : b64val(c3);
        if (v2 < 0 || v3 < 0) return -1;          // a non-'=' symbol outside the alphabet

        uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12) |
                          ((uint32_t)v2 << 6) | (uint32_t)v3;
        if (pad2) {                                // "xx==" -> 1 byte; v1's low 4 bits are unused
            if (v1 & 0x0F) return -1;              // must be zero, or the encoding is non-canonical
            out[o++] = (uint8_t)(triple >> 16);
        } else if (pad3) {                         // "xxx=" -> 2 bytes; v2's low 2 bits are unused
            if (v2 & 0x03) return -1;
            out[o++] = (uint8_t)(triple >> 16);
            out[o++] = (uint8_t)(triple >> 8);
        } else {                                   // full quartet -> 3 bytes
            out[o++] = (uint8_t)(triple >> 16);
            out[o++] = (uint8_t)(triple >> 8);
            out[o++] = (uint8_t)triple;
        }
    }
    *out_len = o;
    return 0;
}

// ---- Known-answer self-test (CI battery) ----------------------------------
// RFC 4648 §10 test vectors both ways, plus a battery of malformed inputs the
// strict decoder must reject. Returns 0 on full pass, else the failing step.
static int b64_str_eq(const char* a, const char* b) {
    uint32_t i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

int base64_selftest(void) {
    struct { const char* raw; const char* enc; } v[] = {
        {"",       ""},
        {"f",      "Zg=="},
        {"fo",     "Zm8="},
        {"foo",    "Zm9v"},
        {"foob",   "Zm9vYg=="},
        {"fooba",  "Zm9vYmE="},
        {"foobar", "Zm9vYmFy"},
    };
    char enc[32];
    uint8_t dec[32];
    uint32_t dlen;
    for (int i = 0; i < 7; i++) {
        uint32_t rl = 0; while (v[i].raw[rl]) rl++;
        // encode must match the vector exactly
        base64_encode((const uint8_t*)v[i].raw, rl, enc);
        if (!b64_str_eq(enc, v[i].enc)) return 1;
        // decode must round-trip back to the original bytes
        uint32_t el = 0; while (v[i].enc[el]) el++;
        if (base64_decode(v[i].enc, el, dec, &dlen) != 0) return 2;
        if (dlen != rl) return 3;
        for (uint32_t k = 0; k < rl; k++) if (dec[k] != (uint8_t)v[i].raw[k]) return 4;
    }
    // Malformed inputs — every one must be rejected.
    const char* bad[] = {
        "Zg=",        // length not a multiple of 4
        "Zm9@",       // symbol outside the alphabet
        "Zh==",       // non-canonical: unused trailing bits set ('h' low nibble != 0)
        "Zg==Zg==",   // padding in a non-final quartet
        "Z===",       // '=' where a data symbol is required
        "====",       // all padding
    };
    for (int i = 0; i < 6; i++) {
        uint32_t bl = 0; while (bad[i][bl]) bl++;
        if (base64_decode(bad[i], bl, dec, &dlen) != -1) return 5;
    }
    // Encoded length helper agrees with reality.
    if (base64_encoded_len(1) != 4 || base64_encoded_len(3) != 4 ||
        base64_encoded_len(4) != 8) return 6;
    return 0;
}
