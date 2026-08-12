#ifndef CRC16_H
#define CRC16_H

#include "kernel.h"

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no input/output reflection, xorout
// 0x0000. This is the de-facto "CRC-16" of the XMODEM/YMODEM family, Bluetooth HCI,
// many smart-card (ISO 7816) and serial/framing protocols. It is an error-detection
// code, NOT a cryptographic checksum. The canonical check value CRC16("123456789")
// == 0x29B1. NyxOS already has CRC-32 (PNG) and Adler-32 (zlib); this fills the
// CRC-16 gap. Pinned by crc16_selftest().

// Feed len bytes into a running CRC (start a fresh CRC at 0xFFFF); returns the new CRC.
uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t* data, uint32_t len);

// One-shot CRC-16/CCITT-FALSE over data[0..len).
uint16_t crc16_ccitt(const uint8_t* data, uint32_t len);

int crc16_selftest(void);   // KAT: canonical + streaming vectors

#endif
