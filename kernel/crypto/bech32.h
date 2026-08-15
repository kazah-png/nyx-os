#ifndef NYX_BECH32_H
#define NYX_BECH32_H
// Bech32 (BIP-173) — a base-32 string encoding carrying a BCH error-detecting checksum over
// GF(32), used for Bitcoin SegWit / Lightning identifiers. Unlike the plain base16/32/58/64/85
// codecs, bech32 is CHECKSUMMED: a human-readable prefix (HRP), a '1' separator, the base-32
// data symbols, and a 6-symbol checksum computed by a length-aware polynomial so that a small
// number of altered or transposed characters is provably detected. This is the pure codec of
// the 5-bit-symbol layer (the same layer BIP-173 specifies and test-vectors); converting bytes
// to/from 5-bit groups (the "convertbits" step used for addresses) is intentionally left to the
// caller. Allocation-free and bounds-checked for untrusted input.
#include "../core/kernel.h"

#define BECH32_MAX_LEN  90    // BIP-173: the whole string is at most 90 characters
#define BECH32_MAX_HRP  83    // 90 - 1 separator - 6 checksum
#define BECH32_MAX_DATA 82    // data symbols excluding the 6-symbol checksum

// Encode: a NUL-terminated lowercase `hrp` plus `data` (each symbol 0..31) into `out`
// (NUL-terminated). Returns the string length, or -1 on error (empty/oversize/uppercase HRP,
// out-of-range symbol, total length > 90, or `out` too small).
int bech32_encode(const char* hrp, const uint8_t* data, uint32_t data_len,
                  char* out, uint32_t outcap);

// Decode: verify the checksum and split the string into HRP + data symbols (checksum stripped).
// `hrp_out` receives the NUL-terminated HRP; `data_out`/`*data_len` the 0..31 symbols. Returns
// 0 on success, -1 on any failure — matching BIP-173's decode() exactly: rejects mixed case,
// any byte outside 33..126, a missing/misplaced '1' separator, an empty HRP, out-of-charset
// data, an overall length above 90, or a bad checksum.
int bech32_decode(const char* s, char* hrp_out, uint32_t hrp_cap,
                  uint8_t* data_out, uint32_t data_cap, uint32_t* data_len);

int bech32_selftest(void);   // 0 = pass; else the failing vector's 1-based index

#endif
