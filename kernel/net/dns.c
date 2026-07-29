#include "../core/kernel.h"
#include "dns.h"

#define DNS_PORT 53

static uint32_t dns_server_ip = 0;

void dns_set_server(uint32_t ip) { dns_server_ip = ip; }
uint32_t dns_get_server(void) { return dns_server_ip; }

// DNS header: 12 bytes
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

#define DNS_FLAG_QR (1 << 15)

static volatile int dns_response_ready = 0;
static uint32_t dns_response_ip = 0;

static void dns_response_handler(uint8_t* data, uint32_t len, uint32_t src_ip, uint16_t src_port) {
    (void)src_ip;
    (void)src_port;
    if (len < sizeof(dns_header_t)) return;
    dns_header_t* hdr = (dns_header_t*)data;
    if (!(ntohs(hdr->flags) & DNS_FLAG_QR)) return;
    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) return;

    // Skip header
    uint32_t off = sizeof(dns_header_t);

    // Skip the question section (variable-length name + QTYPE + QCLASS)
    while (off < len) {
        if (data[off] == 0) { off += 5; break; }
        if (data[off] & 0xC0) { off += 2; break; } // compressed
        off += data[off] + 1;
    }

    // Parse answer section
    for (uint16_t a = 0; a < ancount && off < len; a++) {
        // Skip NAME (compressed pointer or sequence)
        if (off < len && (data[off] & 0xC0)) {
            off += 2;
        } else {
            while (off < len && data[off] != 0) off += data[off] + 1;
            if (off < len) off++; // null terminator
        }

        if (off + 10 > len) break;
        uint16_t type = (data[off] << 8) | data[off+1];
        off += 8; // skip TYPE, CLASS, TTL
        uint16_t rdlength = (data[off] << 8) | data[off+1];
        off += 2;

        if (type == 1 && rdlength == 4 && off + 4 <= len) {
            // Store in network order (low byte = first octet) to match
            // net_interfaces[].ip and what ip_send/udp_send put on the wire.
            dns_response_ip = (uint32_t)data[off] | ((uint32_t)data[off+1] << 8) |
                              ((uint32_t)data[off+2] << 16) | ((uint32_t)data[off+3] << 24);
            dns_response_ready = 1;
            return;
        }
        off += rdlength;
    }
}

// Encode a domain name into DNS format (e.g. "www.google.com" -> \x03www\x06google\x03com\x00).
// `cap` bounds every write into buf: a hostname too long for the buffer returns -1 instead of
// overflowing it. dns_resolve's buffer is a fixed 512-byte stack array and some callers pass an
// unbounded hostname (a shell command's argv[1] is NOT capped like Selene's parse_url host[128]),
// so an over-long name would otherwise smash the kernel stack.
static int encode_dns_name(uint8_t* buf, uint32_t cap, const char* name) {
    uint32_t pos = 0;
    while (*name) {
        const char* dot = name;
        while (*dot && *dot != '.') dot++;
        int len = (int)(dot - name);
        if (len > 63) return -1;
        if (pos + 1 + (uint32_t)len + 1 > cap) return -1;   // label-length byte + label + room for the terminating 0
        buf[pos++] = (uint8_t)len;
        for (int i = 0; i < len; i++)
            buf[pos++] = (uint8_t)name[i];
        name = dot;
        if (*dot == '.') name++;
    }
    if (pos + 1 > cap) return -1;
    buf[pos++] = 0;
    return (int)pos;
}

uint32_t dns_resolve(const char* hostname, int iface_idx) {
    if (!hostname || !hostname[0]) return 0;

    // If it's already an IP, parse it directly
    int is_ip = 1;
    int dots = 0;
    for (int i = 0; hostname[i]; i++) {
        if (hostname[i] == '.') dots++;
        else if (hostname[i] < '0' || hostname[i] > '9') { is_ip = 0; break; }
    }
    if (is_ip && dots == 3) {
        uint8_t a = 0, b = 0, c = 0, d = 0;
        // Parse "xxx.xxx.xxx.xxx" manually
        const char* p = hostname;
        while (*p && *p != '.') { a = a * 10 + (*p - '0'); p++; }
        if (*p == '.') p++;
        while (*p && *p != '.') { b = b * 10 + (*p - '0'); p++; }
        if (*p == '.') p++;
        while (*p && *p != '.') { c = c * 10 + (*p - '0'); p++; }
        if (*p == '.') p++;
        while (*p) { d = d * 10 + (*p - '0'); p++; }
        // Network order: low byte = first octet (matches net_interfaces[].ip).
        return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
    }

    if (!dns_server_ip) {
        // Try to use gateway as DNS server fallback, or 8.8.8.8
        for (int i = 0; i < 8; i++) {
            if (net_interfaces[i].gateway) {
                dns_server_ip = net_interfaces[i].gateway;
                break;
            }
        }
        if (!dns_server_ip)
            dns_server_ip = 0x08080808; // 8.8.8.8
    }

    // Build DNS query
    uint8_t pkt[512];
    __builtin_memset(pkt, 0, sizeof(pkt));

    dns_header_t* hdr = (dns_header_t*)pkt;
    hdr->id = htons(0x1234);
    hdr->flags = htons(0x0100);   // standard query, recursion desired
    hdr->qdcount = htons(1);      // was stored LE -> 256 questions on the wire

    int off = sizeof(dns_header_t);
    int nlen = encode_dns_name(pkt + off, (uint32_t)sizeof(pkt) - (uint32_t)off, hostname);
    if (nlen < 0) return 0;
    off += nlen;

    pkt[off++] = 0;    // QTYPE: A record (high byte)
    pkt[off++] = 1;    // QTYPE (low byte)
    pkt[off++] = 0;    // QCLASS: IN (high byte)
    pkt[off++] = 1;    // QCLASS (low byte)

    dns_response_ready = 0;
    dns_response_ip = 0;

    static uint16_t dns_sport = 0xC000;
    uint16_t src_port = dns_sport++ | 0xC000;   // vary the source port per query
    udp_register_listener(src_port, dns_response_handler);
    udp_send(dns_server_ip, DNS_PORT, src_port, pkt, off, iface_idx);

    // Poll for the response. The PIT timer never fires (see kernel.c) so a
    // tick_count-based timeout would spin forever — bound the retries and
    // busy-wait between polls instead (same approach as dhcp_request).
    for (int retry = 0; retry < 400; retry++) {
        kernel_poll_net();
        for (int d = 0; d < 1500; d++) inb(0x80);
        if (dns_response_ready) {
            udp_register_listener(src_port, NULL);
            return dns_response_ip;
        }
    }

    udp_register_listener(src_port, NULL); // unregister on timeout
    return 0;
}
