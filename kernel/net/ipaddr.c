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

// ============================================================
// ipv6_parse - a STRICT RFC 4291 IPv6 text -> 16-byte address parser + KAT
// ============================================================
// The sibling of ipv4_parse() for the other address family — same contract: parse the
// WHOLE string or fail, no silent truncation. IPv6 text is unusually bug-prone (it is a
// classic source of address-confusion vulnerabilities), so every rule is enforced and
// bounded: at most eight 16-bit hextets of 1..4 hex digits; at most ONE "::" zero-run,
// which must stand for at least one omitted group; and an optional dotted-quad IPv4 tail
// (e.g. ::ffff:192.168.1.1) that occupies the final two groups. Output is the 16 address
// bytes in network order (out[0] = the high byte of the first hextet). Pinned by
// ipv6_parse_selftest().
static int ip6_ishex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int ip6_hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

int ipv6_parse(const char* s, uint8_t out[16]) {
    if (!s || !out) return -1;
    uint16_t g[8];
    int n = 0;                 // hextets parsed so far (excluding the "::" zero-run)
    int gap = -1;              // index in g[] where "::" elides one-or-more zero groups
    const char* p = s;

    if (*p == ':') {           // a leading ':' is only valid as part of "::"
        if (p[1] != ':') return -1;
        gap = 0; p += 2;
        if (*p == '\0') { for (int i = 0; i < 16; i++) out[i] = 0; return 0; }   // "::"
    }

    for (;;) {
        // A token containing a '.' is the dotted-quad IPv4 tail (must be last, fills 2 groups).
        const char* q = p; int has_dot = 0;
        while (*q && *q != ':') { if (*q == '.') has_dot = 1; q++; }
        if (has_dot) {
            uint32_t v4;
            if (n > 6) return -1;                        // no room for two more groups
            if (ipv4_parse(p, &v4) != 0) return -1;      // strict quad, runs to '\0'
            g[n++] = (uint16_t)(((v4 & 0xFF) << 8) | ((v4 >> 8) & 0xFF));
            g[n++] = (uint16_t)((((v4 >> 16) & 0xFF) << 8) | ((v4 >> 24) & 0xFF));
            p = q;                                        // == end of string
            break;
        }
        if (!ip6_ishex(*p)) return -1;                   // a group needs at least one hex digit
        int val = 0, nd = 0;
        while (ip6_ishex(*p)) { val = val * 16 + ip6_hexv(*p); if (++nd > 4) return -1; p++; }
        if (n >= 8) return -1;                           // more than eight groups
        g[n++] = (uint16_t)val;
        if (*p == '\0') break;
        if (*p != ':') return -1;                        // junk after a group
        p++;                                             // consume the separator ':'
        if (*p == ':') {                                 // "::"
            if (gap != -1) return -1;                    // only one "::" allowed
            gap = n; p++;
            if (*p == '\0') break;                       // trailing "::"
        } else if (*p == '\0') {
            return -1;                                   // a trailing single ':' is invalid
        }
    }

    uint16_t full[8];
    if (gap == -1) {
        if (n != 8) return -1;                           // no "::" -> exactly eight groups
        for (int i = 0; i < 8; i++) full[i] = g[i];
    } else {
        if (n >= 8) return -1;                           // "::" must elide at least one group
        int zeros = 8 - n, idx = 0;
        for (int i = 0; i < gap; i++)  full[idx++] = g[i];
        for (int i = 0; i < zeros; i++) full[idx++] = 0;
        for (int i = gap; i < n; i++)  full[idx++] = g[i];
    }
    for (int i = 0; i < 8; i++) { out[i * 2] = (uint8_t)(full[i] >> 8); out[i * 2 + 1] = (uint8_t)(full[i] & 0xFF); }
    return 0;
}

// KAT (`ipv6`): canonical valid forms — including "::" at each position, the all-zeros and
// all-ones extremes, an explicit eight-group form, and an IPv4-mapped tail — each checked
// byte-for-byte, plus an adversarial set of malformed strings that MUST be rejected
// (double "::", a 5-digit hextet, too few/many groups, a "::" that elides nothing, a bad
// embedded quad, a bare IPv4, trailing/leading colons).
int ipv6_parse_selftest(void) {
    struct { const char* s; uint8_t b[16]; } ok[] = {
        { "::",                   { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 } },
        { "::1",                  { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 } },
        { "2001:db8::1",          { 0x20,0x01,0x0d,0xb8, 0,0,0,0, 0,0,0,0, 0,0,0,1 } },
        { "fe80::1",              { 0xfe,0x80,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1 } },
        { "1:2:3:4:5:6:7:8",      { 0,1,0,2,0,3,0,4, 0,5,0,6,0,7,0,8 } },
        { "0:0:0:0:0:0:0:0",      { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 } },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
                                  { 0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff } },
        { "::ffff:192.168.1.1",   { 0,0,0,0, 0,0,0,0, 0,0,0xff,0xff, 0xc0,0xa8,0x01,0x01 } },
        { "2001:db8::c0a8:101",   { 0x20,0x01,0x0d,0xb8, 0,0,0,0, 0,0,0,0, 0xc0,0xa8,0x01,0x01 } },
        { "1::",                  { 0,1,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0 } },
    };
    for (int i = 0; i < (int)(sizeof(ok) / sizeof(ok[0])); i++) {
        uint8_t b[16];
        for (int j = 0; j < 16; j++) b[j] = 0xAA;
        if (ipv6_parse(ok[i].s, b) != 0) return 1;
        for (int j = 0; j < 16; j++) if (b[j] != ok[i].b[j]) return 2;
    }
    const char* bad[] = {
        "",                       // empty
        ":",                      // lone colon
        ":::",                    // triple colon
        "1::2::3",                // two "::"
        "12345::",                // 5-digit hextet
        "1:2:3:4:5:6:7",          // too few groups, no "::"
        "1:2:3:4:5:6:7:8:9",      // nine groups
        "1:2:3:4:5:6:7::8",       // "::" that elides nothing (already eight groups)
        "gggg::",                 // non-hex
        "::ffff:192.168.1.256",   // bad embedded IPv4 octet
        "1.2.3.4",                // a bare IPv4 is not an IPv6 address
        "2001:db8:::1",           // ":::" in the middle
        "1:2:3:4:5:6:7:8:",       // trailing colon
        "12345",                  // lone over-long hextet
        "::12345",                // over-long hextet after "::"
    };
    for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
        uint8_t b[16];
        if (ipv6_parse(bad[i], b) != -1) return 3;
    }
    uint8_t b[16];
    if (ipv6_parse((const char*)0, b) != -1) return 4;         // NULL input
    if (ipv6_parse("::1", (uint8_t*)0) != -1) return 5;        // NULL output
    return 0;
}
