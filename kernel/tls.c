// ============================================================
// tls.c - TLS handshake start (v5.9.48), step 1 of the https arc
// ============================================================
// Sends a real TLS 1.2 ClientHello over TCP:443 and parses the server's ServerHello /
// Certificate / ServerKeyExchange / ServerHelloDone. This establishes the TLS wire
// format (record layer + handshake framing) and proves NyxOS can talk to a real https
// server. There is NO cryptography here yet: the ephemeral-key exchange (X25519/ECDHE),
// the TLS PRF key schedule, AES-GCM and record decryption — the parts that let us
// actually read an https page — are later increments. Wired to the `tls` command.
#include "kernel.h"
#include "tcp.h"
#include "dns.h"
#include "tls.h"

static uint8_t tls_hs[1024];        // outbound ClientHello (record + handshake)
static uint8_t tls_rx[16384];       // raw bytes received
static uint8_t tls_hd[16384];       // reassembled handshake message stream

// Non-cryptographic PRNG for the ClientHello random. This is fine ONLY because this
// step doesn't derive keys yet; a real handshake needs a CSPRNG (a later concern).
static uint64_t tls_rng;
static uint8_t tls_rand_byte(void) {
    tls_rng = tls_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint8_t)(tls_rng >> 33);
}

static void put8(uint8_t* b, int* p, uint8_t v)   { b[(*p)++] = v; }
static void put16(uint8_t* b, int* p, uint16_t v) { b[(*p)++] = (uint8_t)(v >> 8); b[(*p)++] = (uint8_t)v; }
static void putn(uint8_t* b, int* p, const uint8_t* d, int n) { for (int i = 0; i < n; i++) b[(*p)++] = d[i]; }

// Build the ClientHello (record + handshake) into tls_hs; return its total length.
static int build_client_hello(const char* host) {
    uint8_t* b = tls_hs; int p = 0;

    put8(b, &p, 22);                 // record: handshake
    put16(b, &p, 0x0301);            // record version TLS 1.0 (classic ClientHello compat)
    int rec_len_at = p; put16(b, &p, 0);           // record length (backpatched)
    int hs_start = p;

    put8(b, &p, 1);                  // handshake: ClientHello
    int hs_len_at = p; put8(b, &p, 0); put8(b, &p, 0); put8(b, &p, 0);   // 24-bit length
    int ch_start = p;

    put16(b, &p, 0x0303);            // client_version TLS 1.2
    for (int i = 0; i < 32; i++) put8(b, &p, tls_rand_byte());           // 32-byte random
    put8(b, &p, 0);                  // session_id length 0

    static const uint16_t suites[] = {
        0xC02F, 0xC02B, 0xC030, 0xC02C,   // ECDHE-RSA/ECDSA AES-GCM
        0x009C, 0x009D,                   // RSA AES-GCM
        0x002F, 0x0035                    // RSA AES-CBC
    };
    int nsuites = (int)(sizeof(suites) / sizeof(suites[0]));
    put16(b, &p, (uint16_t)(nsuites * 2));
    for (int i = 0; i < nsuites; i++) put16(b, &p, suites[i]);

    put8(b, &p, 1); put8(b, &p, 0);  // compression: 1 method, null

    int ext_len_at = p; put16(b, &p, 0);           // extensions length (backpatched)
    int ext_start = p;

    int hlen = (int)strlen(host);                  // SNI (server_name)
    put16(b, &p, 0x0000); put16(b, &p, (uint16_t)(hlen + 5));
    put16(b, &p, (uint16_t)(hlen + 3)); put8(b, &p, 0); put16(b, &p, (uint16_t)hlen);
    putn(b, &p, (const uint8_t*)host, hlen);

    put16(b, &p, 0x000A); put16(b, &p, 6);         // supported_groups: x25519, secp256r1
    put16(b, &p, 4); put16(b, &p, 0x001D); put16(b, &p, 0x0017);

    put16(b, &p, 0x000B); put16(b, &p, 2); put8(b, &p, 1); put8(b, &p, 0);   // ec_point_formats: uncompressed

    put16(b, &p, 0x000D); put16(b, &p, 10);        // signature_algorithms
    put16(b, &p, 8);
    put16(b, &p, 0x0401); put16(b, &p, 0x0403); put16(b, &p, 0x0804); put16(b, &p, 0x0601);

    put16(b, &p, 0x002B); put16(b, &p, 3); put8(b, &p, 2); put16(b, &p, 0x0303);  // supported_versions: TLS 1.2

    int ext_total = p - ext_start;
    b[ext_len_at] = (uint8_t)(ext_total >> 8); b[ext_len_at + 1] = (uint8_t)ext_total;
    int ch_total = p - ch_start;
    b[hs_len_at] = 0; b[hs_len_at + 1] = (uint8_t)(ch_total >> 8); b[hs_len_at + 2] = (uint8_t)ch_total;
    int rec_total = p - hs_start;
    b[rec_len_at] = (uint8_t)(rec_total >> 8); b[rec_len_at + 1] = (uint8_t)rec_total;
    return p;
}

int tls_hello(const char* host, int iface_idx) {
    uint32_t ip = dns_resolve(host, iface_idx);
    if (!ip) { printf("TLS: cannot resolve %s\n", host); return -1; }

    int conn = tcp_connect(ip, 443, 12400);
    if (conn < 0) { printf("TLS: connect() to %s:443 failed\n", host); return -1; }

    uint32_t dl = get_ticks() + 5000;              // drive the TCP 3-way handshake
    while ((int32_t)(get_ticks() - dl) < 0) {
        kernel_poll_net();
        if (tcp_state(conn) == TCP_STATE_ESTABLISHED) break;
    }
    if (tcp_state(conn) != TCP_STATE_ESTABLISHED) {
        printf("TLS: TCP handshake to %s:443 timed out\n", host); tcp_close(conn); return -1;
    }
    printf("TLS: TCP connected to %s:443 — sending ClientHello...\n", host);

    tls_rng = ((uint64_t)get_ticks() << 16) ^ 0x9E3779B97F4A7C15ULL ^ (uint64_t)ip;
    int chlen = build_client_hello(host);
    if (tcp_send(conn, tls_hs, chlen) < 0) { printf("TLS: send failed\n"); tcp_close(conn); return -1; }

    uint32_t total = 0, start = get_ticks(), last = start;   // read the server flight
    for (;;) {
        kernel_poll_net();
        int n = tcp_recv(conn, tls_rx + total, (uint32_t)sizeof(tls_rx) - total - 1);
        if (n > 0) { total += (uint32_t)n; last = get_ticks(); if (total >= sizeof(tls_rx) - 1) break; }
        else if (n < 0) break;                     // peer closed
        uint32_t now = get_ticks();
        if (total == 0) { if ((int32_t)(now - (start + 5000)) >= 0) break; }
        else if ((int32_t)(now - (last + 1200)) >= 0) break;   // stream went quiet
        if ((int32_t)(now - (start + 12000)) >= 0) break;
    }
    tcp_close(conn);
    if (total == 0) { printf("TLS: no response from %s\n", host); return -1; }

    // Walk TLS records: gather handshake bytes, catch an Alert.
    uint32_t hn = 0, o = 0; int alert = 0, alert_level = 0, alert_desc = 0;
    while (o + 5 <= total) {
        uint8_t rtype = tls_rx[o];
        uint16_t rlen = (uint16_t)((tls_rx[o+3] << 8) | tls_rx[o+4]);
        if (o + 5 + rlen > total) break;
        if (rtype == 22) {                          // handshake
            if (hn + rlen <= sizeof(tls_hd)) { memcpy(tls_hd + hn, tls_rx + o + 5, rlen); hn += rlen; }
        } else if (rtype == 21 && rlen >= 2) {      // alert
            alert = 1; alert_level = tls_rx[o+5]; alert_desc = tls_rx[o+6];
        }
        o += 5 + rlen;
    }

    if (alert) {
        printf("TLS: %s sent an Alert (level %d, description %d) — handshake refused\n",
               host, alert_level, alert_desc);
        return -1;
    }

    // Parse the handshake messages.
    uint32_t h = 0; uint16_t cipher = 0;
    int got_sh = 0, got_cert = 0, ncerts = 0; uint32_t cert0 = 0; int got_ske = 0, got_shd = 0;
    while (h + 4 <= hn) {
        uint8_t mt = tls_hd[h];
        uint32_t mlen = (uint32_t)((tls_hd[h+1] << 16) | (tls_hd[h+2] << 8) | tls_hd[h+3]);
        if (h + 4 + mlen > hn) break;
        uint8_t* m = tls_hd + h + 4;
        if (mt == 2 && mlen >= 38) {                // ServerHello
            got_sh = 1;
            uint32_t q = 2 + 32;                     // version + random
            uint8_t sidl = m[q]; q += 1 + sidl;      // session id
            if (q + 1 < mlen) cipher = (uint16_t)((m[q] << 8) | m[q+1]);
        } else if (mt == 11 && mlen >= 3) {         // Certificate
            got_cert = 1;
            uint32_t clen = (uint32_t)((m[0] << 16) | (m[1] << 8) | m[2]);
            uint32_t cq = 3;
            while (cq + 3 <= 3 + clen && cq + 3 <= mlen) {
                uint32_t one = (uint32_t)((m[cq] << 16) | (m[cq+1] << 8) | m[cq+2]);
                if (ncerts == 0) cert0 = one;
                ncerts++; cq += 3 + one;
            }
        } else if (mt == 12) { got_ske = 1; }       // ServerKeyExchange
        else if (mt == 14) { got_shd = 1; }         // ServerHelloDone
        h += 4 + mlen;
    }

    if (!got_sh) { printf("TLS: no ServerHello in %u bytes from %s\n", total, host); return -1; }
    printf("TLS: ServerHello OK — negotiated cipher suite 0x%x\n", cipher);
    if (got_cert) printf("TLS: Certificate chain — %d cert(s), leaf %u bytes\n", ncerts, (unsigned)cert0);
    if (got_ske)  printf("TLS: ServerKeyExchange present (ephemeral ECDHE key)\n");
    if (got_shd)  printf("TLS: ServerHelloDone — handshake start complete.\n");
    printf("TLS: [next steps: X25519 key exchange, AES-GCM, record decryption]\n");
    return 0;
}
