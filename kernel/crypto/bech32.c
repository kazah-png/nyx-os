#include "bech32.h"

// The 32 data symbols, in value order (value 0 = 'q', ... value 31 = 'l').
static const char BECH32_CHARSET[33] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

// The BCH generator over GF(32) — one step of the length-aware polynomial checksum.
static uint32_t bech32_polymod(const uint8_t* values, uint32_t len) {
    static const uint32_t GEN[5] = {
        0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3
    };
    uint32_t chk = 1;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t b = chk >> 25;
        chk = ((chk & 0x1ffffff) << 5) ^ values[i];
        for (int j = 0; j < 5; j++)
            if ((b >> j) & 1) chk ^= GEN[j];
    }
    return chk;
}

// Expand the HRP into checksum input: [c>>5 for c] + [0] + [c&31 for c]. Writes 2*hrplen+1
// values into buf and returns that count.
static uint32_t bech32_hrp_expand(const char* hrp, uint32_t hrplen, uint8_t* buf) {
    for (uint32_t i = 0; i < hrplen; i++)
        buf[i] = (uint8_t)((unsigned char)hrp[i] >> 5);
    buf[hrplen] = 0;
    for (uint32_t i = 0; i < hrplen; i++)
        buf[hrplen + 1 + i] = (uint8_t)((unsigned char)hrp[i] & 31);
    return 2 * hrplen + 1;
}

// Map a charset character to its 0..31 value, or -1 if it is not a bech32 symbol.
static int bech32_charval(char c) {
    for (int k = 0; k < 32; k++)
        if (BECH32_CHARSET[k] == c) return k;
    return -1;
}

int bech32_encode(const char* hrp, const uint8_t* data, uint32_t data_len,
                  char* out, uint32_t outcap) {
    uint32_t hrplen = 0;
    while (hrp[hrplen]) hrplen++;
    if (hrplen < 1 || hrplen > BECH32_MAX_HRP) return -1;
    for (uint32_t i = 0; i < hrplen; i++) {              // canonical output: lowercase HRP only
        unsigned char c = (unsigned char)hrp[i];
        if (c < 33 || c > 126) return -1;
        if (c >= 'A' && c <= 'Z') return -1;
    }
    if (data_len > BECH32_MAX_DATA) return -1;
    for (uint32_t i = 0; i < data_len; i++) if (data[i] > 31) return -1;

    uint32_t total = hrplen + 1 + data_len + 6;          // hrp + '1' + data + checksum
    if (total > BECH32_MAX_LEN) return -1;
    if (total + 1 > outcap) return -1;                   // + the NUL

    uint8_t vals[300];
    uint32_t n = bech32_hrp_expand(hrp, hrplen, vals);
    for (uint32_t i = 0; i < data_len; i++) vals[n++] = data[i];
    for (int i = 0; i < 6; i++) vals[n++] = 0;           // six zero symbols for the checksum
    uint32_t pm = bech32_polymod(vals, n) ^ 1;

    uint32_t o = 0;
    for (uint32_t i = 0; i < hrplen; i++) out[o++] = hrp[i];
    out[o++] = '1';
    for (uint32_t i = 0; i < data_len; i++) out[o++] = BECH32_CHARSET[data[i]];
    for (int i = 0; i < 6; i++) out[o++] = BECH32_CHARSET[(pm >> (5 * (5 - i))) & 31];
    out[o] = 0;
    return (int)o;
}

int bech32_decode(const char* s, char* hrp_out, uint32_t hrp_cap,
                  uint8_t* data_out, uint32_t data_cap, uint32_t* data_len) {
    uint32_t len = 0;
    while (s[len]) len++;
    if (len > BECH32_MAX_LEN) return -1;

    int has_lower = 0, has_upper = 0;
    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 33 || c > 126) return -1;                // printable ASCII only
        if (c >= 'a' && c <= 'z') has_lower = 1;
        if (c >= 'A' && c <= 'Z') has_upper = 1;
    }
    if (has_lower && has_upper) return -1;                // mixed case is rejected

    char buf[BECH32_MAX_LEN + 1];
    for (uint32_t i = 0; i < len; i++) {                  // fold to lowercase to decode
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[len] = 0;

    int pos = -1;                                         // index of the last '1' separator
    for (int i = (int)len - 1; i >= 0; i--)
        if (buf[i] == '1') { pos = i; break; }
    if (pos < 1 || (uint32_t)(pos + 7) > len) return -1;  // non-empty HRP + >=6 data symbols

    uint32_t dcount = len - (uint32_t)pos - 1;
    uint8_t data[BECH32_MAX_LEN];
    for (uint32_t i = 0; i < dcount; i++) {
        int v = bech32_charval(buf[pos + 1 + i]);
        if (v < 0) return -1;
        data[i] = (uint8_t)v;
    }

    uint32_t hrplen = (uint32_t)pos;
    uint8_t vals[300];
    uint32_t n = bech32_hrp_expand(buf, hrplen, vals);
    for (uint32_t i = 0; i < dcount; i++) vals[n++] = data[i];
    if (bech32_polymod(vals, n) != 1) return -1;          // checksum must be valid

    uint32_t out_dlen = dcount - 6;
    if (hrplen + 1 > hrp_cap || out_dlen > data_cap) return -1;
    for (uint32_t i = 0; i < hrplen; i++) hrp_out[i] = buf[i];
    hrp_out[hrplen] = 0;
    for (uint32_t i = 0; i < out_dlen; i++) data_out[i] = data[i];
    *data_len = out_dlen;
    return 0;
}

// Known-answer self-test: the BIP-173 canonical valid + invalid vectors. Each valid string must
// decode AND round-trip (re-encode to its lowercased form); each invalid string must be rejected.
int bech32_selftest(void) {
    static const char* const valid[] = {
        "A12UEL5L",
        "a12uel5l",
        "an83characterlonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1tt5tgs",
        "abcdef1qpzry9x8gf2tvdw0s3jn54khce6mua7lmqqqxw",
        "11qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqc8247j",
        "split1checkupstagehandshakeupstreamerranterredcaperred2y9e3w",
        "?1ezyfcl",
    };
    static const char* const invalid[] = {
        " 1nwldj5",          // HRP char out of range (space)
        "\x7f""1axkwrx",     // HRP char out of range (DEL)
        "\x80""1eym55h",     // HRP char out of range (>127)
        "an84characterslonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1569pvx",
        "pzry9x0s0muk",      // no separator
        "1pzry9x0s0muk",     // empty HRP
        "x1b4n0q5v",         // invalid data character
        "li1dgmt3",          // too short a checksum
        "A1G7SGD8",          // checksum computed over the uppercased HRP
        "10a06t8",           // empty HRP
        "1qzzfhee",          // empty HRP
        "a12Uel5l",          // mixed case
        "",                  // empty string
        "1",                 // separator only
    };
    char hrp[BECH32_MAX_HRP + 1];
    uint8_t data[BECH32_MAX_DATA];
    uint32_t dlen;
    for (uint32_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        if (bech32_decode(valid[i], hrp, sizeof(hrp), data, sizeof(data), &dlen) != 0)
            return (int)(i + 1);
        char re[BECH32_MAX_LEN + 1];
        if (bech32_encode(hrp, data, dlen, re, sizeof(re)) < 0)
            return (int)(i + 1);
        // re-encode must reproduce the lowercased original
        for (uint32_t k = 0;; k++) {
            char a = re[k];
            char b = valid[i][k];
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return (int)(i + 1);
            if (a == 0) break;
        }
    }
    for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        if (bech32_decode(invalid[i], hrp, sizeof(hrp), data, sizeof(data), &dlen) == 0)
            return (int)(100 + i + 1);
    }
    return 0;
}
