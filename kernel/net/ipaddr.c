// ============================================================
// ipaddr.c - a STRICT dotted-quad IPv4 parser + KAT (v6.4.123)
// ============================================================
// The kernel already has parse_ip() in kernel.c, but it is deliberately LENIENT: it is
// the "is this argument an IP or a hostname?" heuristic behind commands like `tcptest`
// (a non-numeric arg parses to 0, which triggers a DNS fallback). That leniency is wrong
// for numeric CONFIG input such as `setip <ip> [mask] [gw]`, where parse_ip() silently
// masks each octet to its low 8 bits — so `setip 300.1.1.1` quietly sets 44.1.1.1 and a
// typo like `setip 1.2.3` sets 1.2.3.0 with no error. ipv4_parse() is the strict
// counterpart: exactly four decimal octets 0..255 separated by single dots, nothing
// else, or it fails. Output is network byte order (first octet in the low byte) to match
// net_interfaces[].ip and parse_ip(). Pinned by ipv4_parse_selftest().
#include "../core/kernel.h"
#include "ipaddr.h"

int ipv4_parse(const char* s, uint32_t* out) {
    if (!s || !out) return -1;
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return -1;            // each octet needs at least one digit
        int val = 0, ndig = 0;
        while (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            if (val > 255) return -1;                    // octet out of range
            if (++ndig > 3) return -1;                   // at most three digits per octet
            s++;
        }
        ip |= (uint32_t)val << (i * 8);                  // first octet -> low byte (network order)
        if (i < 3) {                                     // three dots separate the four octets
            if (*s != '.') return -1;
            s++;
        }
    }
    if (*s != '\0') return -1;                           // trailing junk (a 5th octet, a dot, a space...)
    *out = ip;
    return 0;
}

// KAT: canonical valid addresses (checked against the network-order layout) plus a wide
// set of malformed strings that MUST be rejected. This is an adversarial test — the
// point is that hostile / mistyped input never yields a silently-wrong address.
int ipv4_parse_selftest(void) {
    struct { const char* s; uint32_t ip; } ok[] = {
        { "0.0.0.0",         0x00000000u },
        { "127.0.0.1",       0x0100007Fu },
        { "192.168.1.1",     0x0101A8C0u },
        { "255.255.255.255", 0xFFFFFFFFu },
        { "8.8.8.8",         0x08080808u },
        { "10.0.2.15",       0x0F02000Au },
        { "1.2.3.4",         0x04030201u },
        { "192.168.001.001", 0x0101A8C0u },   // leading zeros are accepted (decimal)
    };
    for (int i = 0; i < 8; i++) {
        uint32_t v = 0xDEADBEEFu;
        if (ipv4_parse(ok[i].s, &v) != 0) return 1;
        if (v != ok[i].ip) return 2;
    }
    const char* bad[] = {
        "256.0.0.1",   // octet > 255
        "300.1.1.1",   // the setip footgun parse_ip() masked to 44.1.1.1
        "999.1.1.1",   // way over
        "1.2.3",       // too few octets
        "1.2.3.4.5",   // too many octets
        "1.2.3.",      // trailing dot
        ".1.2.3.4",    // leading dot
        "1..2.3",      // empty octet
        "1.2.3.4 ",    // trailing space
        " 1.2.3.4",    // leading space
        "1.2.3.x",     // non-digit
        "1.2.3.4444",  // four-digit octet
        "1.2.3.-1",    // '-' is not a digit
        "",            // empty
        "abc",         // pure garbage
    };
    for (int i = 0; i < 15; i++) {
        uint32_t v = 0;
        if (ipv4_parse(bad[i], &v) != -1) return 3;
    }
    // a NULL / NULL-out must fail rather than crash
    uint32_t v = 0;
    if (ipv4_parse((const char*)0, &v) != -1) return 4;
    if (ipv4_parse("1.2.3.4", (uint32_t*)0) != -1) return 5;
    return 0;
}
