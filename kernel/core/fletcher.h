#ifndef NYX_FLETCHER_H
#define NYX_FLETCHER_H
// Fletcher's checksum (John G. Fletcher, 1982) — a position-sensitive checksum from two running
// modular sums, so unlike a plain additive checksum it detects most byte transpositions and
// reorderings. Two standard widths: Fletcher-16 folds bytes mod 255 into two 8-bit sums;
// Fletcher-32 folds 16-bit little-endian words mod 65535 into two 16-bit sums (a trailing odd
// byte is zero-padded). Cheaper than a CRC — used by ZFS, some transport protocols, and OSPF/
// SNMP-adjacent code. Distinct algorithm from the crc16/32/32c polynomial checksums already
// here. Correctness is pinned by fletcher_selftest() (the CI `fletcher` KAT).
#include "kernel.h"

uint16_t fletcher16(const uint8_t* data, uint32_t len);   // two 8-bit sums, mod 255
uint32_t fletcher32(const uint8_t* data, uint32_t len);   // two 16-bit sums over LE words, mod 65535

int fletcher_selftest(void);   // 0 = pass; else a non-zero failing-vector marker

#endif
