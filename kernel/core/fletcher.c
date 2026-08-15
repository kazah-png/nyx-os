#include "fletcher.h"

// Fletcher-16: fold each byte into c0, and each running c0 into c1, both mod 255. The checksum
// is c1 in the high byte, c0 in the low byte.
uint16_t fletcher16(const uint8_t* data, uint32_t len) {
    uint32_t c0 = 0, c1 = 0;
    for (uint32_t i = 0; i < len; i++) {
        c0 = (c0 + data[i]) % 255;
        c1 = (c1 + c0) % 255;
    }
    return (uint16_t)((c1 << 8) | c0);
}

// Fletcher-32: the same recurrence over 16-bit little-endian words, mod 65535. A trailing odd
// byte is treated as a word with a zero high byte (standard zero padding).
uint32_t fletcher32(const uint8_t* data, uint32_t len) {
    uint32_t c0 = 0, c1 = 0, i = 0;
    for (; i + 1 < len; i += 2) {
        uint32_t w = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8);
        c0 = (c0 + w) % 65535;
        c1 = (c1 + c0) % 65535;
    }
    if (i < len) {                                   // final odd byte, high byte zero-padded
        c0 = (c0 + (uint32_t)data[i]) % 65535;
        c1 = (c1 + c0) % 65535;
    }
    return (c1 << 16) | c0;
}

// Known-answer test: the canonical Fletcher vectors ("abcde"/"abcdef"/"abcdefgh") for both
// widths, plus the empty input (both sums stay zero).
int fletcher_selftest(void) {
    static const struct { const char* s; uint16_t e; } v16[] = {
        {"abcde", 0xC8F0}, {"abcdef", 0x2057}, {"abcdefgh", 0x0627},
    };
    for (int i = 0; i < 3; i++) {
        uint32_t n = 0; while (v16[i].s[n]) n++;
        if (fletcher16((const uint8_t*)v16[i].s, n) != v16[i].e) return i + 1;
    }
    static const struct { const char* s; uint32_t e; } v32[] = {
        {"abcde", 0xF04FC729u}, {"abcdef", 0x56502D2Au}, {"abcdefgh", 0xEBE19591u},
    };
    for (int i = 0; i < 3; i++) {
        uint32_t n = 0; while (v32[i].s[n]) n++;
        if (fletcher32((const uint8_t*)v32[i].s, n) != v32[i].e) return 10 + i + 1;
    }
    if (fletcher16((const uint8_t*)"", 0) != 0) return 20;
    if (fletcher32((const uint8_t*)"", 0) != 0) return 21;
    return 0;
}
