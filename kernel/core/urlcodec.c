#include "urlcodec.h"

// RFC 3986 unreserved: ALPHA / DIGIT / "-" / "." / "_" / "~". Everything else is encoded.
static int uc_unreserved(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}

static int uc_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;   // decode accepts either case
    return -1;
}

int url_pct_encode(const uint8_t* src, uint32_t srclen, char* dst, uint32_t dstcap) {
    static const char H[] = "0123456789ABCDEF";      // encode always emits uppercase hex
    uint32_t o = 0;
    for (uint32_t i = 0; i < srclen; i++) {
        uint8_t c = src[i];
        if (uc_unreserved(c)) {
            if (o + 1 >= dstcap) return -1;
            dst[o++] = (char)c;
        } else {
            if (o + 3 >= dstcap) return -1;          // "%XX"
            dst[o++] = '%';
            dst[o++] = H[c >> 4];
            dst[o++] = H[c & 0x0F];
        }
    }
    if (o >= dstcap) return -1;                       // room for the NUL
    dst[o] = '\0';
    return (int)o;
}

int url_form_encode(const uint8_t* src, uint32_t srclen, char* dst, uint32_t dstcap) {
    static const char H[] = "0123456789ABCDEF";
    uint32_t o = 0;
    for (uint32_t i = 0; i < srclen; i++) {
        uint8_t c = src[i];
        if (uc_unreserved(c))   { if (o + 1 >= dstcap) return -1; dst[o++] = (char)c; }
        else if (c == ' ')      { if (o + 1 >= dstcap) return -1; dst[o++] = '+'; }   // form convention
        else { if (o + 3 >= dstcap) return -1; dst[o++] = '%'; dst[o++] = H[c >> 4]; dst[o++] = H[c & 0x0F]; }
    }
    if (o >= dstcap) return -1;
    dst[o] = '\0';
    return (int)o;
}

int url_pct_decode(const char* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap) {
    uint32_t o = 0;
    for (uint32_t i = 0; i < srclen; i++) {
        char c = src[i];
        if (c == '%') {
            if (i + 2 >= srclen) return -1;           // need two more chars after '%'
            int hi = uc_hexval(src[i + 1]), lo = uc_hexval(src[i + 2]);
            if (hi < 0 || lo < 0) return -1;          // not two hex digits -> reject
            if (o >= dstcap) return -1;
            dst[o++] = (uint8_t)((hi << 4) | lo);
            i += 2;
        } else {
            if (o >= dstcap) return -1;
            dst[o++] = (uint8_t)c;
        }
    }
    return (int)o;
}

// ---- Known-answer self-test (CI battery) -------------------------------------
// Encode vectors match Python urllib.parse.quote(s, safe=''); every vector also
// round-trips through the strict decoder, and a battery of malformed escapes must be
// rejected. Cross-checked against an independent Python computation on the host.
static int uc_streq(const char* a, const char* b) {
    uint32_t i = 0;
    for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}

int url_codec_selftest(void) {
    char enc[128];
    uint8_t dec[128];
    struct { const char* raw; const char* enc; } v[] = {
        { "",              ""                       },
        { "abc",           "abc"                    },
        { "a b",           "a%20b"                  },
        { "hello world!",  "hello%20world%21"       },
        { "/?#[]@",        "%2F%3F%23%5B%5D%40"     },
        { "Nyx-OS_1.0~",   "Nyx-OS_1.0~"            },   // all unreserved: unchanged
        { "100%",          "100%25"                 },
        { "a+b=c&d",       "a%2Bb%3Dc%26d"          },   // '+' is a literal here, encoded
    };
    for (int i = 0; i < 8; i++) {
        uint32_t rl = 0; while (v[i].raw[rl]) rl++;
        int el = url_pct_encode((const uint8_t*)v[i].raw, rl, enc, sizeof(enc));
        if (el < 0 || !uc_streq(enc, v[i].enc)) return 1;
        uint32_t cl = 0; while (v[i].enc[cl]) cl++;
        int dl = url_pct_decode(v[i].enc, cl, dec, sizeof(dec));
        if (dl < 0 || (uint32_t)dl != rl) return 2;
        for (uint32_t k = 0; k < rl; k++) if (dec[k] != (uint8_t)v[i].raw[k]) return 3;
    }
    // A UTF-8 byte sequence (u+00E9 e-acute = C3 A9) encodes per-byte and round-trips.
    if (url_pct_encode((const uint8_t*)"caf\xC3\xA9", 5, enc, sizeof(enc)) < 0) return 4;
    if (!uc_streq(enc, "caf%C3%A9")) return 5;
    // Lowercase hex in the input must decode too (%c3%a9 -> the same two bytes).
    if (url_pct_decode("%c3%a9", 6, dec, sizeof(dec)) != 2 ||
        dec[0] != 0xC3 || dec[1] != 0xA9) return 6;
    // Strict decoder rejects every malformed escape.
    const char* bad[] = { "%", "%2", "%2Z", "%ZZ", "a%", "%%41" };
    for (int i = 0; i < 6; i++) {
        uint32_t bl = 0; while (bad[i][bl]) bl++;
        if (url_pct_decode(bad[i], bl, dec, sizeof(dec)) != -1) return 10 + i;
    }
    // A tight output buffer must be reported, not overrun.
    if (url_pct_encode((const uint8_t*)" ", 1, enc, 3) != -1) return 20;  // "%20"+NUL needs 4

    // x-www-form-urlencoded variant: space -> '+', literal '+' -> %2B (matches quote_plus).
    struct { const char* raw; const char* enc; } fv[] = {
        { "a b",          "a+b"            },
        { "hello world!", "hello+world%21" },
        { "a+b=c&d",      "a%2Bb%3Dc%26d"  },
        { "Nyx-OS_1.0~",  "Nyx-OS_1.0~"    },   // all unreserved: unchanged
    };
    for (int i = 0; i < 4; i++) {
        uint32_t rl = 0; while (fv[i].raw[rl]) rl++;
        if (url_form_encode((const uint8_t*)fv[i].raw, rl, enc, sizeof(enc)) < 0) return 30 + i;
        if (!uc_streq(enc, fv[i].enc)) return 40 + i;
    }
    return 0;
}
