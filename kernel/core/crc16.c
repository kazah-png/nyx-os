// ============================================================
// crc16.c - CRC-16/CCITT-FALSE + KAT (see crc16.h).
// ============================================================
#include "crc16.h"

// Bitwise, MSB-first, poly 0x1021. No lookup table: the frames this checks are
// short control/serial messages, so the per-byte inner loop costs nothing that
// matters and the code stays tiny.
uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t crc16_ccitt(const uint8_t* data, uint32_t len) {
    return crc16_ccitt_update(0xFFFF, data, len);
}

// ---- known-answer self-test (`crc16`) ----
int crc16_selftest(void) {
    // The canonical CCITT-FALSE check value.
    if (crc16_ccitt((const uint8_t*)"123456789", 9) != 0x29B1) return 1;
    // Empty input leaves the 0xFFFF init untouched.
    if (crc16_ccitt((const uint8_t*)"", 0) != 0xFFFF) return 2;
    // Extra fixed vectors (host-computed).
    if (crc16_ccitt((const uint8_t*)"A", 1)   != 0xB915) return 3;
    if (crc16_ccitt((const uint8_t*)"abc", 3) != 0x514A) return 4;
    // Streaming across two updates must equal the one-shot over the join.
    uint16_t c = 0xFFFF;
    c = crc16_ccitt_update(c, (const uint8_t*)"1234", 4);
    c = crc16_ccitt_update(c, (const uint8_t*)"56789", 5);
    if (c != 0x29B1) return 5;
    return 0;
}
