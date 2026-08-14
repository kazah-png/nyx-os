#include "base58.h"

static const char B58_ALPHA[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";   // 58 chars, no 0 O I l

static int b58_val(char c) {
    for (int i = 0; i < 58; i++) if (B58_ALPHA[i] == c) return i;
    return -1;
}

int base58_encode(const uint8_t* in, uint32_t inlen, char* out, uint32_t outcap) {
    if (inlen > BASE58_MAX) return -1;
    uint32_t zeros = 0;
    while (zeros < inlen && in[zeros] == 0) zeros++;      // leading 0x00 -> leading '1'

    uint8_t buf[BASE58_MAX];                              // in-place long-division work copy
    for (uint32_t i = 0; i < inlen; i++) buf[i] = in[i];

    char tmp[BASE58_MAX * 138 / 100 + 2];                 // digits, least-significant first
    uint32_t tn = 0, start = zeros;
    while (start < inlen) {                               // repeatedly divide the number by 58
        uint32_t rem = 0;
        for (uint32_t i = start; i < inlen; i++) {
            uint32_t cur = (rem << 8) | buf[i];
            buf[i] = (uint8_t)(cur / 58);
            rem = cur % 58;
        }
        tmp[tn++] = B58_ALPHA[rem];
        while (start < inlen && buf[start] == 0) start++; // skip quotient's new leading zeros
    }

    uint32_t total = zeros + tn;
    if (total + 1 > outcap) return -1;
    uint32_t o = 0;
    for (uint32_t i = 0; i < zeros; i++) out[o++] = '1';
    for (uint32_t i = 0; i < tn; i++) out[o++] = tmp[tn - 1 - i];   // most-significant first
    out[o] = '\0';
    return (int)o;
}

int base58_decode(const char* in, uint32_t inlen, uint8_t* out, uint32_t outcap) {
    uint32_t zeros = 0;
    while (zeros < inlen && in[zeros] == '1') zeros++;    // leading '1' -> leading 0x00

    uint8_t buf[BASE58_MAX];                              // accumulator, least-significant first
    uint32_t bn = 0;
    for (uint32_t i = 0; i < inlen; i++) {
        int v = b58_val(in[i]);
        if (v < 0) return -1;                             // reject anything outside the alphabet
        uint32_t carry = (uint32_t)v;                     // buf = buf * 58 + v
        for (uint32_t j = 0; j < bn; j++) {
            uint32_t cur = (uint32_t)buf[j] * 58 + carry;
            buf[j] = (uint8_t)(cur & 0xFF);
            carry = cur >> 8;
        }
        while (carry) {
            if (bn >= BASE58_MAX) return -1;              // overflow guard
            buf[bn++] = (uint8_t)(carry & 0xFF);
            carry >>= 8;
        }
    }

    uint32_t total = zeros + bn;
    if (total > outcap || total > BASE58_MAX) return -1;
    uint32_t o = 0;
    for (uint32_t i = 0; i < zeros; i++) out[o++] = 0;
    for (uint32_t i = 0; i < bn; i++) out[o++] = buf[bn - 1 - i];   // big-endian
    return (int)o;
}

// ---- KAT ---------------------------------------------------------------------------
// The canonical Bitcoin base58 test vectors (hex input <-> base58), covering leading
// zeros, short and long inputs. Each vector is checked BOTH ways (encode + decode).
static int b58_hexnib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static uint32_t b58_unhex(const char* hex, uint8_t* out) {   // hex string -> bytes; returns len
    uint32_t n = 0;
    while (hex[0] && hex[1]) {
        int hi = b58_hexnib(hex[0]), lo = b58_hexnib(hex[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}

int base58_selftest(void) {
    static const struct { const char* hex; const char* b58; } v[] = {
        { "",                                           ""                              },
        { "61",                                         "2g"                            },
        { "626262",                                     "a3gV"                          },
        { "636363",                                     "aPEr"                          },
        { "73696d706c792061206c6f6e6720737472696e67",   "2cFupjhnEsSn59qHXstmK2ffpLv2"  },
        { "516b6fcd0f",                                 "ABnLTmg"                       },
        { "bf4f89001e670274dd",                         "3SEo3LWLoPntC"                 },
        { "572e4794",                                   "3EFU7m"                        },
        { "ecac89cad93923c02321",                       "EJDM8drfXA6uyA"                },
        { "10c8511e",                                   "Rt5zm"                         },
        { "00000000000000000000",                       "1111111111"                    },
        { "0000287fb4cd",                               "11233QC4"                      },
    };
    int nv = (int)(sizeof(v) / sizeof(v[0]));
    for (int i = 0; i < nv; i++) {
        uint8_t raw[BASE58_MAX];
        uint32_t rawlen = b58_unhex(v[i].hex, raw);
        char enc[BASE58_MAX * 2];
        int el = base58_encode(raw, rawlen, enc, sizeof(enc));
        if (el < 0) return i + 1;
        if (strcmp(enc, v[i].b58) != 0) return i + 1;      // encode mismatch

        uint8_t dec[BASE58_MAX];
        uint32_t b58len = 0; while (v[i].b58[b58len]) b58len++;
        int dl = base58_decode(v[i].b58, b58len, dec, sizeof(dec));
        if (dl < 0 || (uint32_t)dl != rawlen) return i + 1; // decode length mismatch
        for (uint32_t k = 0; k < rawlen; k++)
            if (dec[k] != raw[k]) return i + 1;             // decode content mismatch
    }
    // reject an out-of-alphabet character (0, O, I, l are not in base58)
    uint8_t junk[8];
    if (base58_decode("0OIl", 4, junk, sizeof(junk)) != -1) return 100;
    return 0;
}
