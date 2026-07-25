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
#include "curve25519.h"
#include "tls_prf.h"
#include "aes_gcm.h"
#include "sha256.h"

static uint8_t tls_hs[1024];        // outbound ClientHello (record + handshake)
static uint8_t tls_rx[16384];       // raw bytes received
static uint8_t tls_hd[16384];       // reassembled handshake message stream
static uint8_t tls_client_random[32];   // saved from our ClientHello (for the key schedule)
static uint8_t tls_server_random[32];   // parsed from the ServerHello

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
    for (int i = 0; i < 32; i++) {                                       // 32-byte random
        tls_client_random[i] = tls_rand_byte();                          // (saved for the key schedule)
        put8(b, &p, tls_client_random[i]);
    }
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

// Print a labelled byte buffer as lowercase hex (does not rely on printf field widths).
static void tls_print_hex(const char* label, const uint8_t* b, int n) {
    static const char hx[] = "0123456789abcdef";
    char two[3]; two[2] = 0;
    printf("%s", label);
    for (int i = 0; i < n; i++) { two[0] = hx[b[i] >> 4]; two[1] = hx[b[i] & 0xf]; printf("%s", two); }
    printf("\n");
}

static int tls_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void tls_hex(uint8_t* out, const char* h, int n) {
    for (int i = 0; i < n; i++) out[i] = (uint8_t)((tls_hexval(h[2 * i]) << 4) | tls_hexval(h[2 * i + 1]));
}

// Known-answer test for the TLS 1.2 key schedule: it chains the (separately KAT'd) X25519
// and PRF primitives with the EXACT labels and seed order the live handshake uses, so a
// wrong label or a swapped client/server-random order is caught here rather than only at a
// real server's Finished check. The pre-master secret is the RFC 7748 §6.1 X25519 shared
// secret; the expected master secret and key block were produced by an independent PRF.
int tls_keyschedule_selftest(void) {
    uint8_t pm[32], cr[32], sr[32], seed[64], master[48], keyblock[40], want[48], wantk[40];
    tls_hex(pm, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);
    for (int i = 0; i < 32; i++) { cr[i] = (uint8_t)i; sr[i] = (uint8_t)(0x20 + i); }

    memcpy(seed, cr, 32); memcpy(seed + 32, sr, 32);
    tls12_prf(pm, 32, "master secret", seed, 64, master, 48);
    tls_hex(want, "3dcd0e1fa717e41ff560509c61c4039922fb8d2a7580728ef991c0748f244b0b"
                  "4125f429b4f71ed8b2084093e40953ae", 48);
    int ok_m = 1; for (int i = 0; i < 48; i++) if (master[i] != want[i]) ok_m = 0;

    memcpy(seed, sr, 32); memcpy(seed + 32, cr, 32);
    tls12_prf(master, 48, "key expansion", seed, 64, keyblock, 40);
    tls_hex(wantk, "0b98d0c76e21f26689a87b7749abe9938329f7c1e746cf68358623233e646082"
                   "eaaa7227ebba690e", 40);
    int ok_k = 1; for (int i = 0; i < 40; i++) if (keyblock[i] != wantk[i]) ok_k = 0;

    if (ok_m) printf("tlskeys: master-secret derivation PASS (48 B, label \"master secret\")\n");
    else      printf("tlskeys: master-secret derivation FAIL\n");
    if (ok_k) printf("tlskeys: key expansion PASS (40 B AES-128-GCM key block, label \"key expansion\")\n");
    else      printf("tlskeys: key expansion FAIL\n");
    printf("tlskeys: self-test %d/2 vectors passed\n", ok_m + ok_k);
    return (ok_m && ok_k) ? 0 : -1;
}

static int tls_eq(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

// ---- TLS 1.2 AES-128-GCM record protection (RFC 5288) -------------------------------
// GCM nonce = write_IV(4) || explicit_nonce(8). AAD = seq(8) || type || version || len.
// The on-wire fragment is explicit_nonce(8) || GCM_ciphertext || GCM_tag(16); we send the
// 64-bit sequence number as the explicit nonce.

static void tls_rec_aad(uint8_t aad[13], uint64_t seq, uint8_t type, uint16_t plen) {
    for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(seq >> (8 * (7 - i)));
    aad[8]  = type;
    aad[9]  = 0x03; aad[10] = 0x03;                  // TLS 1.2
    aad[11] = (uint8_t)(plen >> 8); aad[12] = (uint8_t)plen;
}

// Seal pt into out = explicit_nonce(8) || ct || tag(16); returns the fragment length.
static int tls_seal_record(const uint8_t key[16], const uint8_t iv4[4], uint64_t seq,
                           uint8_t type, const uint8_t* pt, uint32_t pt_len, uint8_t* out) {
    uint8_t nonce[12], aad[13];
    for (int i = 0; i < 4; i++) nonce[i] = iv4[i];
    for (int i = 0; i < 8; i++) { nonce[4 + i] = (uint8_t)(seq >> (8 * (7 - i))); out[i] = nonce[4 + i]; }
    tls_rec_aad(aad, seq, type, (uint16_t)pt_len);
    aes128_gcm_encrypt(key, nonce, aad, 13, pt, pt_len, out + 8, out + 8 + pt_len);
    return (int)(8 + pt_len + 16);
}

// Open a fragment (explicit_nonce(8) || ct || tag(16)); returns pt_len, or -1 on a bad tag.
static int tls_open_record(const uint8_t key[16], const uint8_t iv4[4], uint64_t seq,
                           uint8_t type, const uint8_t* in, uint32_t in_len, uint8_t* pt) {
    if (in_len < 8 + 16) return -1;
    uint32_t ct_len = in_len - 8 - 16;
    uint8_t nonce[12], aad[13];
    for (int i = 0; i < 4; i++) nonce[i] = iv4[i];
    for (int i = 0; i < 8; i++) nonce[4 + i] = in[i];            // explicit nonce read off the wire
    tls_rec_aad(aad, seq, type, (uint16_t)ct_len);              // GCM plaintext length == ct length
    if (aes128_gcm_decrypt(key, nonce, aad, 13, in + 8, ct_len, in + 8 + ct_len, pt) != 0) return -1;
    return (int)ct_len;
}

// Finished.verify_data = PRF(master, label, SHA256(handshake transcript))[0..11].
static void tls_verify_data(const uint8_t master[48], const char* label,
                            const uint8_t* transcript, uint32_t tlen, uint8_t out[12]) {
    uint8_t hash[32];
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, transcript, tlen);
    sha256_final(&c, hash);
    tls12_prf(master, 48, label, hash, 32, out, 12);
}

// Known-answer test for the record layer + Finished verify_data, so the exact GCM nonce/AAD
// framing and the transcript-hash PRF are pinned offline before the live handshake (v5.9.54)
// relies on them. The expected fragment and verify_data were produced by independent code.
int tls_record_selftest(void) {
    int pass = 0, total = 0;

    uint8_t key[16], iv4[4], pt[16], frag[64], want[64], out[32];
    tls_hex(key, "000102030405060708090a0b0c0d0e0f", 16);
    tls_hex(iv4, "10111213", 4);
    memcpy(pt, "hello, nyxos tls", 16);                          // 16 bytes
    tls_hex(want, "000000000000000009e3145e244c52c6bffab247c55a84bbf924abd2649283e96c0f657ac55f4bb3", 40);

    int flen = tls_seal_record(key, iv4, 0, 0x17, pt, 16, frag);
    total++;
    if (flen == 40 && tls_eq(frag, want, 40)) { pass++; printf("tlsrec: GCM record seal PASS (exact wire bytes)\n"); }
    else                                        printf("tlsrec: GCM record seal FAIL\n");

    int olen = tls_open_record(key, iv4, 0, 0x17, frag, 40, out);
    total++;
    if (olen == 16 && tls_eq(out, pt, 16)) { pass++; printf("tlsrec: GCM record open PASS (plaintext recovered)\n"); }
    else                                     printf("tlsrec: GCM record open FAIL\n");

    frag[12] ^= 0x01;                                            // corrupt one ciphertext byte
    total++;
    if (tls_open_record(key, iv4, 0, 0x17, frag, 40, out) == -1) { pass++; printf("tlsrec: record tamper rejected PASS\n"); }
    else                                                          printf("tlsrec: record tamper rejected FAIL\n");
    frag[12] ^= 0x01;

    uint8_t master[48], trans[32], vd[12], wc[12], ws[12];
    tls_hex(master, "3dcd0e1fa717e41ff560509c61c4039922fb8d2a7580728ef991c0748f244b0b"
                    "4125f429b4f71ed8b2084093e40953ae", 48);
    for (int i = 0; i < 32; i++) trans[i] = (uint8_t)i;
    tls_hex(wc, "82ddaf4f4cd337eecc6526d3", 12);
    tls_hex(ws, "31337bf745fbcf3fe2e1915a", 12);

    tls_verify_data(master, "client finished", trans, 32, vd);
    total++;
    if (tls_eq(vd, wc, 12)) { pass++; printf("tlsrec: client Finished verify_data PASS\n"); }
    else                      printf("tlsrec: client Finished verify_data FAIL\n");

    tls_verify_data(master, "server finished", trans, 32, vd);
    total++;
    if (tls_eq(vd, ws, 12)) { pass++; printf("tlsrec: server Finished verify_data PASS\n"); }
    else                      printf("tlsrec: server Finished verify_data FAIL\n");

    printf("tlsrec: self-test %d/%d checks passed\n", pass, total);
    return (pass == total) ? 0 : -1;
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
    uint16_t ske_curve = 0; int ske_pub_len = 0; uint8_t ske_pub[80];   // server's ECDHE public key
    while (h + 4 <= hn) {
        uint8_t mt = tls_hd[h];
        uint32_t mlen = (uint32_t)((tls_hd[h+1] << 16) | (tls_hd[h+2] << 8) | tls_hd[h+3]);
        if (h + 4 + mlen > hn) break;
        uint8_t* m = tls_hd + h + 4;
        if (mt == 2 && mlen >= 38) {                // ServerHello
            got_sh = 1;
            memcpy(tls_server_random, m + 2, 32);    // 2-byte version, then the 32-byte random
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
        } else if (mt == 12) {                      // ServerKeyExchange (ECDHE parameters)
            got_ske = 1;
            // ServerECDHParams: curve_type(1). named_curve => named_curve(2) + point_len(1) + point.
            if (mlen >= 4 && m[0] == 3) {           // 3 = named_curve
                ske_curve = (uint16_t)((m[1] << 8) | m[2]);
                uint8_t plen = m[3];
                if ((uint32_t)(4 + plen) <= mlen && plen <= sizeof(ske_pub)) {
                    ske_pub_len = plen;
                    memcpy(ske_pub, m + 4, plen);
                }
            }
        }
        else if (mt == 14) { got_shd = 1; }         // ServerHelloDone
        h += 4 + mlen;
    }

    if (!got_sh) { printf("TLS: no ServerHello in %u bytes from %s\n", total, host); return -1; }
    printf("TLS: ServerHello OK — negotiated cipher suite 0x%x\n", cipher);
    if (got_cert) printf("TLS: Certificate chain — %d cert(s), leaf %u bytes\n", ncerts, (unsigned)cert0);
    if (got_ske)  printf("TLS: ServerKeyExchange present (ephemeral ECDHE key, curve 0x%x)\n", ske_curve);
    if (got_shd)  printf("TLS: ServerHelloDone — handshake start complete.\n");

    // ECDHE key agreement + the TLS 1.2 key schedule (v5.9.51). Only x25519 is implemented.
    if (ske_curve == 0x001D && ske_pub_len == 32) {
        uint8_t eph_priv[32], eph_pub[32], premaster[32];
        for (int i = 0; i < 32; i++) eph_priv[i] = tls_rand_byte();   // NON-crypto RNG — see note below
        x25519_base(eph_pub, eph_priv);              // our ephemeral public key
        x25519(premaster, eph_priv, ske_pub);        // shared secret = the pre-master secret

        uint8_t seed[64], master[48], keyblock[40];
        memcpy(seed, tls_client_random, 32); memcpy(seed + 32, tls_server_random, 32);
        tls12_prf(premaster, 32, "master secret", seed, 64, master, 48);
        memcpy(seed, tls_server_random, 32); memcpy(seed + 32, tls_client_random, 32);
        tls12_prf(master, 48, "key expansion", seed, 64, keyblock, 40);

        printf("TLS: ECDHE(x25519) — computed the 32-byte pre-master secret from the server's key\n");
        tls_print_hex("TLS:   pre-master = ", premaster, 32);
        tls_print_hex("TLS:   master     = ", master, 48);
        printf("TLS: derived the AES-128-GCM key block (client_key[16] server_key[16] "
               "client_iv[4] server_iv[4])\n");
        tls_print_hex("TLS:   client_write_key = ", keyblock, 16);
        tls_print_hex("TLS:   server_write_key = ", keyblock + 16, 16);
        printf("TLS: key exchange complete — next: AES-128-GCM record protection + Finished.\n");
        printf("TLS: [note] the ephemeral key uses a NON-crypto RNG for now; a CSPRNG is a later step.\n");
    } else if (got_ske) {
        printf("TLS: server chose curve 0x%x; only x25519 (0x001d) is wired up so far — "
               "no key agreement this run.\n", ske_curve);
    }
    return 0;
}
