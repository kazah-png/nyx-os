#include "od.h"

// Append v in `base` (8/16/10), zero-padded to `width` digits, into out. `upper` selects the
// hex-digit case (GNU od prints the ADDRESS in upper-case hex but -t x1/x2 DATA in lower-case).
static void od_num(char* out, uint32_t* o, uint32_t cap, unsigned long v, int base, int width, int upper) {
    char tmp[24]; int n = 0;
    char a = upper ? 'A' : 'a';
    if (v == 0) tmp[n++] = '0';
    while (v) { int d = (int)(v % (unsigned long)base); tmp[n++] = (char)(d < 10 ? '0' + d : a + d - 10); v /= (unsigned long)base; }
    for (int p = width - n; p > 0; p--) if (*o < cap - 1) out[(*o)++] = '0';
    for (int i = n - 1; i >= 0; i--)    if (*o < cap - 1) out[(*o)++] = tmp[i];
}

static void od_ch(char* out, uint32_t* o, uint32_t cap, char c) { if (*o < cap - 1) out[(*o)++] = c; }

// One byte in -c form, right-justified in a 4-wide field (GNU od -c layout).
static void od_c_item(char* out, uint32_t* o, uint32_t cap, uint8_t b) {
    char tb[4]; const char* tok = tb;
    switch (b) {
        case 0:  tok = "\\0"; break; case 7:  tok = "\\a"; break; case 8:  tok = "\\b"; break;
        case 9:  tok = "\\t"; break; case 10: tok = "\\n"; break; case 11: tok = "\\v"; break;
        case 12: tok = "\\f"; break; case 13: tok = "\\r"; break;
        default:
            if (b >= 0x20 && b <= 0x7e) { tb[0] = (char)b; tb[1] = 0; }
            else { tb[0] = (char)('0' + ((b >> 6) & 7)); tb[1] = (char)('0' + ((b >> 3) & 7)); tb[2] = (char)('0' + (b & 7)); tb[3] = 0; }
    }
    int tl = 0; while (tok[tl]) tl++;
    for (int p = 4 - tl; p > 0; p--) od_ch(out, o, cap, ' ');
    for (int i = 0; i < tl; i++)     od_ch(out, o, cap, tok[i]);
}

// Append the leading-space + fixed-width numeric item for the octal/hex types.
static void od_num_item(char* out, uint32_t* o, uint32_t cap, unsigned long v, int type) {
    od_ch(out, o, cap, ' ');
    switch (type) {
        case OD_O1: od_num(out, o, cap, v, 8,  3, 0); break;
        case OD_O2: od_num(out, o, cap, v, 8,  6, 0); break;
        case OD_X1: od_num(out, o, cap, v, 16, 2, 0); break;   // data hex is lower-case
        case OD_X2: od_num(out, o, cap, v, 16, 4, 0); break;
    }
}

static void od_addr(char* out, uint32_t* o, uint32_t cap, uint32_t off, char radix) {
    if (radix == 'o') od_num(out, o, cap, off, 8, 7, 0);
    else if (radix == 'x') od_num(out, o, cap, off, 16, 6, 1);   // GNU prints the address in UPPER-case hex
    else if (radix == 'd') od_num(out, o, cap, off, 10, 7, 0);
}

static int od_eq16(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 16; i++) if (a[i] != b[i]) return 0;
    return 1;
}

uint32_t od_format(const uint8_t* data, uint32_t len, char addr_radix, int type,
                   char* out, uint32_t outcap) {
    uint32_t o = 0;
    int bpi = (type == OD_O2 || type == OD_X2) ? 2 : 1;   // bytes per item
    int star = 0;
    for (uint32_t off = 0; off < len; off += 16) {
        uint32_t ll = len - off; if (ll > 16) ll = 16;
        // squeeze: a full 16-byte line identical to the previous one collapses to a single '*'
        if (off >= 16 && ll == 16 && od_eq16(data + off, data + off - 16)) {
            if (!star) { od_ch(out, &o, outcap, '*'); od_ch(out, &o, outcap, '\n'); star = 1; }
            continue;
        }
        star = 0;
        if (addr_radix != 'n') od_addr(out, &o, outcap, off, addr_radix);
        for (uint32_t bo = off; bo < off + ll; bo += (uint32_t)bpi) {
            if (bpi == 1) {
                if (type == OD_C) od_c_item(out, &o, outcap, data[bo]);
                else              od_num_item(out, &o, outcap, data[bo], type);
            } else {
                unsigned lo = data[bo];
                unsigned hi = (bo + 1 < off + ll) ? data[bo + 1] : 0;   // last odd byte: high = 0
                od_num_item(out, &o, outcap, lo | (hi << 8), type);
            }
        }
        od_ch(out, &o, outcap, '\n');
    }
    if (addr_radix != 'n') { od_addr(out, &o, outcap, len, addr_radix); od_ch(out, &o, outcap, '\n'); }
    if (o < outcap) out[o] = '\0';
    return o;
}

// ---- known-answer self-test (`od`): fixed vectors cross-checked against GNU od ----
static int od_eq(const uint8_t* d, uint32_t len, char radix, int type, const char* want) {
    static char out[512];
    uint32_t n = od_format(d, len, radix, type, out, sizeof out);
    uint32_t wl = 0; while (want[wl]) wl++;
    if (n != wl) return 0;
    for (uint32_t i = 0; i < n; i++) if (out[i] != want[i]) return 0;
    return 1;
}

int od_selftest(void) {
    static const uint8_t v[19] = { 'h','e','l','l','o','\n','A','B',0,1,0xfe,0xff,' ','w','o','r','l','d','d' };
    // default (-A o -t o2)
    if (!od_eq(v, 19, 'o', OD_O2,
        "0000000 062550 066154 005157 041101 000400 177776 073440 071157\n"
        "0000020 062154 000144\n0000023\n")) return 1;
    // -A x -t x1
    if (!od_eq(v, 19, 'x', OD_X1,
        "000000 68 65 6c 6c 6f 0a 41 42 00 01 fe ff 20 77 6f 72\n"
        "000010 6c 64 64\n000013\n")) return 2;
    // -A o -t c  (named escapes / printable / octal, width-4 fields)
    if (!od_eq(v, 19, 'o', OD_C,
        "0000000   h   e   l   l   o  \\n   A   B  \\0 001 376 377       w   o   r\n"
        "0000020   l   d   d\n0000023\n")) return 3;
    // -A n -t x1 (no address column, no trailing offset line)
    if (!od_eq(v, 19, 'n', OD_X1,
        " 68 65 6c 6c 6f 0a 41 42 00 01 fe ff 20 77 6f 72\n 6c 64 64\n")) return 4;
    // '*' squeeze: 32 zero bytes -> first line, '*', trailing offset
    { static const uint8_t z[32] = {0};
      if (!od_eq(z, 32, 'o', OD_O2,
        "0000000 000000 000000 000000 000000 000000 000000 000000 000000\n*\n0000040\n")) return 5; }
    // empty input -> just the trailing offset line (radix o); nothing for radix n
    if (!od_eq(v, 0, 'o', OD_O2, "0000000\n")) return 6;
    if (!od_eq(v, 0, 'n', OD_X1, "")) return 7;
    // -A d -t x1 (decimal address, 7 wide)
    if (!od_eq(v, 19, 'd', OD_X1,
        "0000000 68 65 6c 6c 6f 0a 41 42 00 01 fe ff 20 77 6f 72\n"
        "0000016 6c 64 64\n0000019\n")) return 8;
    return 0;
}
