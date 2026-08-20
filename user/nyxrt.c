/* nyxrt.c — Nyx C runtime implementation. Freestanding, no libc calls. */
#include "nyxrt.h"

/* crt0 publishes the process environment here (it references `environ` on every
 * launch). Cross-compiled N programs link nyxrt INSTEAD of libc, so the symbol
 * must live here too — even though nothing in the N runtime reads it yet.
 * In-OS builds (the `cc` builtin, milestone M3) always link /libc.o, which
 * already defines environ — defining it here too would be a duplicate symbol,
 * so the in-OS (tcc) build leaves it to libc. */
#ifndef __TINYC__
char** environ = 0;
#endif

nyx_str __nyx_fmt_begin(char* buf, nyx_u64 cap) {
    (void)cap;
    if (cap) buf[0] = '\0';
    return (nyx_str){ buf, 0 };
}

/* Append n bytes, always NUL-terminating, never overflowing cap. */
static void nyx_append(nyx_str* d, char* buf, nyx_u64 cap, const char* s, nyx_u64 n) {
    nyx_u64 i = 0;
    while (i < n && d->len + 1 < cap) buf[d->len++] = s[i++];
    if (cap) buf[d->len] = '\0';
    d->ptr = buf;
}

void __nyx_fmt_str(nyx_str* d, char* buf, nyx_u64 cap, nyx_str s) {
    nyx_append(d, buf, cap, s.ptr, s.len);
}

void __nyx_fmt_i64(nyx_str* d, char* buf, nyx_u64 cap, nyx_i64 v) {
    char tmp[24];
    int  ti = 0;
    int  neg = 0;
    nyx_u64 uv;
    if (v < 0) { neg = 1; uv = (nyx_u64)(-(v + 1)) + 1; }  /* handles INT64_MIN */
    else         uv = (nyx_u64)v;
    if (uv == 0) tmp[ti++] = '0';
    while (uv) { tmp[ti++] = (char)('0' + (int)(uv % 10)); uv /= 10; }
    if (neg) tmp[ti++] = '-';
    char out[24];
    int  oi = 0;
    while (ti) out[oi++] = tmp[--ti];   /* reverse */
    nyx_append(d, buf, cap, out, (nyx_u64)oi);
}

/* v0.21 "{expr:wN}" / "{expr:zN}" (optionally composed with x/X): format
 * the number, then right-align it in `width` columns. Zero padding puts
 * the sign FIRST ("-0042", the convention every C formatter follows);
 * space padding puts it with the digits ("  -42"). Hex still shows the
 * u64 bit pattern, so no sign appears in hex mode. A value wider than
 * the field is printed in full — width is a minimum, never a cut. */
void __nyx_fmt_num(nyx_str* d, char* buf, nyx_u64 cap, nyx_i64 v,
                   int hex, int upper, int width, int zero) {
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int  ti = 0;
    int  neg = 0;
    nyx_u64 uv;
    if (hex) {
        uv = (nyx_u64)v;
        if (uv == 0) tmp[ti++] = '0';
        while (uv) { tmp[ti++] = dig[uv & 15]; uv >>= 4; }
    } else {
        if (v < 0) { neg = 1; uv = (nyx_u64)(-(v + 1)) + 1; }  /* INT64_MIN-safe */
        else         uv = (nyx_u64)v;
        if (uv == 0) tmp[ti++] = '0';
        while (uv) { tmp[ti++] = (char)('0' + (int)(uv % 10)); uv /= 10; }
    }
    char out[160];
    int  oi = 0;
    int  pad = width - ti - neg;
    if (zero) {
        if (neg) out[oi++] = '-';
        while (pad > 0) { out[oi++] = '0'; pad--; }
    } else {
        while (pad > 0) { out[oi++] = ' '; pad--; }
        if (neg) out[oi++] = '-';
    }
    while (ti) out[oi++] = tmp[--ti];   /* reverse the digits */
    nyx_append(d, buf, cap, out, (nyx_u64)oi);
}

/* v0.20 "{expr:x}" / "{expr:X}": hex shows the raw bit pattern, so the
 * value arrives as u64 — a negative i64 prints as its two's-complement
 * image, the reading a systems programmer expects from hex. */
void __nyx_fmt_hex(nyx_str* d, char* buf, nyx_u64 cap, nyx_u64 v, int upper) {
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[16];
    int  ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v) { tmp[ti++] = dig[v & 15]; v >>= 4; }
    char out[16];
    int  oi = 0;
    while (ti) out[oi++] = tmp[--ti];   /* reverse */
    nyx_append(d, buf, cap, out, (nyx_u64)oi);
}
