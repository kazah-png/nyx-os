#include "../core/kernel.h"
#include "dns.h"
#include "../crypto/csprng.h"   // csprng_bytes() — random DNS transaction IDs (anti-spoof)

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
static uint16_t dns_query_id = 0;        // transaction ID of the outstanding query (host order)

// Advance past a DNS name in `data` (bounds-checked against `len`) and return the
// new offset. A name is a run of length-prefixed labels ending in a 0 byte, and
// with compression it may end early in a 2-byte pointer (top two bits of the first
// byte set). This runs on UNTRUSTED response data, so it never reads past `len` and
// always terminates: a bogus label length that overshoots the packet just ends the
// walk (the caller re-checks bounds before reading the record that follows). We only
// SKIP names — never follow a pointer to reconstruct one — so a self-referential
// pointer cannot loop us.
static uint32_t dns_skip_name(const uint8_t* data, uint32_t len, uint32_t off) {
    while (off < len) {
        uint8_t b = data[off];
        if (b == 0)   return off + 1;          // root label: the name ends here
        if (b & 0xC0) return off + 2;          // compression pointer: 2 bytes, ends the name
        off += (uint32_t)b + 1;                // ordinary label: length byte + that many bytes
    }
    return off;                                 // ran off the end of the packet
}

// Pure, global-free core of the response handler (so it can be unit-tested against
// hostile packets). Every access into [data, data+len) is bounds-checked; names are
// SKIPPED, never followed, so a compression-pointer loop cannot hang it. Returns 1
// and the A-record IPv4 (network order) in *out_ip on the first match, else 0.
int dns_parse_a_record(const uint8_t* data, uint32_t len, uint16_t expected_id, uint32_t* out_ip) {
    if (len < sizeof(dns_header_t)) return 0;
    const dns_header_t* hdr = (const dns_header_t*)data;
    if (!(ntohs(hdr->flags) & DNS_FLAG_QR)) return 0;
    // Reject any response whose transaction ID does not match the query we sent.
    // Without this a blind off-path attacker (or any host that reaches the UDP
    // source port) could forge a reply and poison the answer; matching the random
    // 16-bit ID is the standard first line of defence against DNS spoofing.
    if (ntohs(hdr->id) != expected_id) return 0;
    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) return 0;

    uint32_t off = sizeof(dns_header_t);
    // Skip the question section: NAME + QTYPE(2) + QCLASS(2).
    off = dns_skip_name(data, len, off) + 4;

    for (uint16_t a = 0; a < ancount && off < len; a++) {
        off = dns_skip_name(data, len, off);   // NAME: label sequence and/or a compression pointer

        if (off + 10 > len) break;
        uint16_t type = (data[off] << 8) | data[off+1];
        off += 8; // skip TYPE, CLASS, TTL
        uint16_t rdlength = (data[off] << 8) | data[off+1];
        off += 2;

        if (type == 1 && rdlength == 4 && off + 4 <= len) {
            // Store in network order (low byte = first octet) to match
            // net_interfaces[].ip and what ip_send/udp_send put on the wire.
            *out_ip = (uint32_t)data[off] | ((uint32_t)data[off+1] << 8) |
                      ((uint32_t)data[off+2] << 16) | ((uint32_t)data[off+3] << 24);
            return 1;
        }
        off += rdlength;
    }
    return 0;
}

static void dns_response_handler(uint8_t* data, uint32_t len, uint32_t src_ip, uint16_t src_port) {
    (void)src_ip;
    (void)src_port;
    uint32_t ip;
    if (dns_parse_a_record(data, len, dns_query_id, &ip)) {
        dns_response_ip = ip;
        dns_response_ready = 1;
    }
}

// KAT: pins the A-record parser against a valid response and hostile ones (spoofed
// id, a query not a response, truncation at several offsets, an oversized rdlength,
// a self-referential compression pointer). Convention: 0 = PASS, else the failing case.
int dns_parse_selftest(void) {
    // A minimal well-formed response: header + question(www.xyz) + one A answer 1.2.3.4,
    // the answer NAME being a compression pointer (0xC00C) back to the question at off 12.
    uint8_t pkt[64];
    int n = 0;
    pkt[n++]=0x12; pkt[n++]=0x34; pkt[n++]=0x80; pkt[n++]=0x00;   // id=0x1234, flags=QR
    pkt[n++]=0x00; pkt[n++]=0x01; pkt[n++]=0x00; pkt[n++]=0x01;   // qd=1, an=1
    pkt[n++]=0x00; pkt[n++]=0x00; pkt[n++]=0x00; pkt[n++]=0x00;   // ns=0, ar=0
    pkt[n++]=3; pkt[n++]='w'; pkt[n++]='w'; pkt[n++]='w';         // question NAME
    pkt[n++]=3; pkt[n++]='x'; pkt[n++]='y'; pkt[n++]='z'; pkt[n++]=0;
    pkt[n++]=0x00; pkt[n++]=0x01; pkt[n++]=0x00; pkt[n++]=0x01;   // QTYPE=A, QCLASS=IN
    pkt[n++]=0xC0; pkt[n++]=0x0C;                                 // answer NAME: pointer -> off 12
    pkt[n++]=0x00; pkt[n++]=0x01;                                 // TYPE=A
    pkt[n++]=0x00; pkt[n++]=0x01;                                 // CLASS=IN
    pkt[n++]=0x00; pkt[n++]=0x00; pkt[n++]=0x00; pkt[n++]=0x00;   // TTL
    pkt[n++]=0x00; pkt[n++]=0x04;                                 // RDLENGTH=4
    pkt[n++]=1; pkt[n++]=2; pkt[n++]=3; pkt[n++]=4;               // A = 1.2.3.4
    uint32_t plen = (uint32_t)n;
    uint32_t ip = 0;

    // 1. valid, matching id -> extracts 1.2.3.4 (network order: low byte = first octet)
    if (dns_parse_a_record(pkt, plen, 0x1234, &ip) != 1) return 1;
    if (ip != ((uint32_t)1 | (2u<<8) | (3u<<16) | (4u<<24))) return 2;
    // 2. spoofed / wrong transaction id -> rejected
    if (dns_parse_a_record(pkt, plen, 0x9999, &ip) != 0) return 3;
    // 3. QR bit clear (a query, not a response) -> rejected
    { uint8_t s = pkt[2]; pkt[2] = 0x00; int r = dns_parse_a_record(pkt, plen, 0x1234, &ip); pkt[2] = s; if (r) return 4; }
    // 4. truncated inside the answer RDATA -> no OOB, not found
    if (dns_parse_a_record(pkt, plen - 2, 0x1234, &ip) != 0) return 5;
    // 5. truncated mid-header -> not found
    if (dns_parse_a_record(pkt, 6, 0x1234, &ip) != 0) return 6;
    // 6. ancount = 0 -> not found
    { uint8_t s = pkt[7]; pkt[7] = 0x00; int r = dns_parse_a_record(pkt, plen, 0x1234, &ip); pkt[7] = s; if (r) return 7; }
    // 7. oversized RDLENGTH on a non-A record must not read past the packet (off += rdlength
    //    overshoots, the loop's off<len guard ends it). Retype the answer + claim 0xFFFF.
    { uint8_t t0 = pkt[28], t1 = pkt[29], l0 = pkt[36], l1 = pkt[37];
      pkt[28]=0x00; pkt[29]=0x10;                 // TYPE = 16 (TXT), not A
      pkt[36]=0xFF; pkt[37]=0xFF;                 // RDLENGTH = 65535
      int r = dns_parse_a_record(pkt, plen, 0x1234, &ip);
      pkt[28]=t0; pkt[29]=t1; pkt[36]=l0; pkt[37]=l1;
      if (r) return 8; }
    // 8. a self-referential compression pointer as the whole question name must terminate
    //    (skip_name returns off+2, never follows) — parse just yields not-found, no hang.
    { uint8_t loop[16];
      for (int i = 0; i < 12; i++) loop[i] = pkt[i];   // header (id, QR, qd=1, an=1)
      loop[12] = 0xC0; loop[13] = 0x0C;                // question NAME -> points to itself
      loop[14] = 0x00; loop[15] = 0x00;
      if (dns_parse_a_record(loop, 16, 0x1234, &ip)) return 9; }
    return 0;
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
    // Random transaction ID (not a fixed constant): a predictable ID lets an
    // off-path attacker forge a matching response, so draw it from the CSPRNG and
    // remember it for the response handler to check.
    uint16_t qid = 0;
    csprng_bytes((uint8_t*)&qid, sizeof(qid));
    dns_query_id = qid;
    hdr->id = htons(qid);
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

// Known-answer + adversarial test for the DNS response parser, which consumes
// UNTRUSTED network data. Asserts a well-formed A-record reply (with a compressed
// answer name) is parsed to the right address, and that a battery of malformed
// packets — too short, not a response, wrong transaction ID, no answers, oversized
// labels that walk off the end, a huge RDLENGTH, a non-A record — is rejected with
// no spurious answer and (since we return at all) without reading out of bounds or
// looping. Runs offline in the CI self-test battery. Returns 0 on pass, else the
// failing case number. Restores the DNS state it touches so a later real query is
// unaffected.
int dns_response_selftest(void) {
    uint16_t saved_id = dns_query_id;
    int rc = 0;

    // Positive: id 0x1234, QR set, 1 answer; question www.google.com; answer name is
    // a compression pointer (0xC0 0x0C) back to the question; A record 93.184.216.34.
    {
        uint8_t p[] = {
            0x12,0x34, 0x81,0x80, 0x00,0x01, 0x00,0x01, 0x00,0x00, 0x00,0x00,
            0x03,'w','w','w', 0x06,'g','o','o','g','l','e', 0x03,'c','o','m', 0x00,
            0x00,0x01, 0x00,0x01,
            0xC0,0x0C,
            0x00,0x01, 0x00,0x01, 0x00,0x00,0x00,0x3C, 0x00,0x04,
            93,184,216,34
        };
        dns_query_id = 0x1234;
        dns_response_ready = 0; dns_response_ip = 0;
        dns_response_handler(p, sizeof(p), 0, 0);
        uint32_t want = (uint32_t)93 | (184u << 8) | (216u << 16) | (34u << 24);
        if (!dns_response_ready || dns_response_ip != want) { rc = 1; goto done; }
    }

    dns_query_id = 0x1234;   // negatives: none of these may set dns_response_ready

    { uint8_t p[] = {0x12,0x34,0x81}; dns_response_ready = 0;                     // 1) shorter than header
      dns_response_handler(p, sizeof(p), 0, 0); if (dns_response_ready) { rc = 2; goto done; } }

    { uint8_t p[] = {0x12,0x34, 0x00,0x00, 0x00,0x01, 0x00,0x01, 0,0,0,0, 0};     // 2) QR clear (a query)
      dns_response_ready = 0; dns_response_handler(p, sizeof(p), 0, 0); if (dns_response_ready) { rc = 3; goto done; } }

    { uint8_t p[] = {0x99,0x99, 0x81,0x80, 0x00,0x01, 0x00,0x01, 0,0,0,0, 0};     // 3) wrong transaction ID
      dns_response_ready = 0; dns_response_handler(p, sizeof(p), 0, 0); if (dns_response_ready) { rc = 4; goto done; } }

    { uint8_t p[] = {0x12,0x34, 0x81,0x80, 0x00,0x01, 0x00,0x00, 0,0,0,0, 0};     // 4) ancount = 0
      dns_response_ready = 0; dns_response_handler(p, sizeof(p), 0, 0); if (dns_response_ready) { rc = 5; goto done; } }

    { uint8_t p[] = {0x12,0x34,0x81,0x80,0x00,0x01,0x00,0x01,0,0,0,0,            // 5) oversized labels off the end
                     0x3F,'a','a','a', 0x3F,'b','b'};
      dns_response_ready = 0; dns_response_handler(p, sizeof(p), 0, 0); if (dns_response_ready) { rc = 6; goto done; } }

    { uint8_t p[] = {0x12,0x34,0x81,0x80,0x00,0x01,0x00,0x01,0,0,0,0,            // 6) CNAME + huge RDLENGTH
                     0x00, 0x00,0x01,0x00,0x01,
                     0xC0,0x0C, 0x00,0x05, 0x00,0x01, 0,0,0,0x3C, 0xFF,0xFF, 0xAA};
      dns_response_ready = 0; dns_response_handler(p, sizeof(p), 0, 0); if (dns_response_ready) { rc = 7; goto done; } }

done:
    dns_response_ready = 0; dns_response_ip = 0;
    dns_query_id = saved_id;
    return rc;
}
