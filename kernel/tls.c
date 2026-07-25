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
#include "csprng.h"
#include "der.h"
#include "p256.h"

// Diagnostic prints inside tls_https_fetch are gated on its `verbose` parameter, so the
// `tls` command shows the full handshake while Selene fetches silently. (Only valid where
// a `verbose` variable is in scope — i.e. inside tls_https_fetch.)
#define TLSP(...) do { if (verbose) printf(__VA_ARGS__); } while (0)

static uint8_t tls_hs[1024];        // outbound ClientHello (record + handshake)
static uint8_t tls_rx[16384];       // raw bytes received
static uint8_t tls_hd[16384];       // reassembled handshake message stream
static uint8_t tls_client_random[32];   // saved from our ClientHello (for the key schedule)
static uint8_t tls_server_random[32];   // parsed from the ServerHello
static uint8_t tls_ts[20480];           // handshake transcript (hashed for the Finished verify_data)
static uint8_t tls_ske_params[160];     // ServerECDHParams — the data the SKE signature covers
static uint8_t tls_ske_sig[128];        // the SKE signature (DER ECDSA-Sig-Value)


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
    csprng_bytes(tls_client_random, 32);            // 32-byte random (CSPRNG; saved for the key schedule)
    putn(b, &p, tls_client_random, 32);
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

// Print up to `max` bytes as text, replacing non-printables (except tab/newlines) with '.'.
static void tls_print_text(const uint8_t* b, uint32_t n, uint32_t max) {
    if (n > max) n = max;
    char one[2]; one[1] = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        one[0] = (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127)) ? (char)c : '.';
        printf("%s", one);
    }
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

// Convert a DER INTEGER value (big-endian, maybe with a 0x00 sign pad or fewer than 32 bytes)
// into a fixed 32-byte big-endian buffer (right-aligned).
static void der_int_to_32(uint8_t out[32], const uint8_t* v, uint32_t len) {
    while (len > 32) { v++; len--; }
    for (int i = 0; i < 32; i++) out[i] = 0;
    for (uint32_t i = 0; i < len; i++) out[32 - len + i] = v[i];
}

// Full https fetch: TLS 1.2 handshake with host:443, GET <path>, decrypt the response into
// out[cap]. Returns the decrypted response length (>= 0) or -1 on failure. `verbose` gates
// the step-by-step diagnostics (the `tls` command sets it; Selene does not).
int tls_https_fetch(const char* host, const char* path, int iface_idx,
                    uint8_t* out, uint32_t cap, int verbose) {
    uint32_t ip = dns_resolve(host, iface_idx);
    if (!ip) { TLSP("TLS: cannot resolve %s\n", host); return -1; }

    int conn = tcp_connect(ip, 443, 12400);
    if (conn < 0) { TLSP("TLS: connect() to %s:443 failed\n", host); return -1; }

    uint32_t dl = get_ticks() + 5000;              // drive the TCP 3-way handshake
    while ((int32_t)(get_ticks() - dl) < 0) {
        kernel_poll_net();
        if (tcp_state(conn) == TCP_STATE_ESTABLISHED) break;
    }
    if (tcp_state(conn) != TCP_STATE_ESTABLISHED) {
        TLSP("TLS: TCP handshake to %s:443 timed out\n", host); tcp_close(conn); return -1;
    }
    TLSP("TLS: TCP connected to %s:443 — sending ClientHello...\n", host);

    int chlen = build_client_hello(host);
    if (tcp_send(conn, tls_hs, chlen) < 0) { TLSP("TLS: send failed\n"); tcp_close(conn); return -1; }

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
    if (total == 0) { TLSP("TLS: no response from %s\n", host); tcp_close(conn); return -1; }

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
        TLSP("TLS: %s sent an Alert (level %d, description %d) — handshake refused\n",
               host, alert_level, alert_desc);
        tcp_close(conn); return -1;
    }

    // Parse the handshake messages.
    uint32_t h = 0; uint16_t cipher = 0;
    int got_sh = 0, got_cert = 0, ncerts = 0; uint32_t cert0 = 0; int got_ske = 0, got_shd = 0;
    uint16_t ske_curve = 0; int ske_pub_len = 0; uint8_t ske_pub[80];   // server's ECDHE public key
    const uint8_t* leaf_ptr = 0;                                        // leaf certificate bytes (in tls_hd)
    uint32_t ske_params_len = 0, ske_sig_len = 0; uint16_t ske_sig_alg = 0;
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
                if (ncerts == 0) { cert0 = one; leaf_ptr = m + cq + 3; }   // the leaf cert
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
                    uint32_t pl = 4 + (uint32_t)plen;          // ServerECDHParams = the signed data
                    if (pl <= sizeof(tls_ske_params)) { memcpy(tls_ske_params, m, pl); ske_params_len = pl; }
                    if (pl + 4 <= mlen) {                      // SignatureAndHashAlgorithm(2) + length(2) + signature
                        ske_sig_alg = (uint16_t)((m[pl] << 8) | m[pl+1]);
                        uint32_t sl = (uint32_t)((m[pl+2] << 8) | m[pl+3]);
                        if (pl + 4 + sl <= mlen && sl <= sizeof(tls_ske_sig)) { memcpy(tls_ske_sig, m + pl + 4, sl); ske_sig_len = sl; }
                    }
                }
            }
        }
        else if (mt == 14) { got_shd = 1; }         // ServerHelloDone
        h += 4 + mlen;
    }

    if (!got_sh) { TLSP("TLS: no ServerHello in %u bytes from %s\n", total, host); tcp_close(conn); return -1; }
    TLSP("TLS: ServerHello OK — negotiated cipher suite 0x%x\n", cipher);
    if (got_cert) TLSP("TLS: Certificate chain — %d cert(s), leaf %u bytes\n", ncerts, (unsigned)cert0);
    if (got_ske)  TLSP("TLS: ServerKeyExchange present (ephemeral ECDHE key, curve 0x%x)\n", ske_curve);
    if (got_shd)  TLSP("TLS: ServerHelloDone — handshake start complete.\n");

    // ECDHE key agreement + the TLS 1.2 key schedule (v5.9.51). Only x25519 is implemented.
    if (ske_curve == 0x001D && ske_pub_len == 32) {
        uint8_t eph_priv[32], eph_pub[32], premaster[32];
        csprng_bytes(eph_priv, 32);                  // cryptographically-strong ephemeral private key
        x25519_base(eph_pub, eph_priv);              // our ephemeral public key
        x25519(premaster, eph_priv, ske_pub);        // shared secret = the pre-master secret

        uint8_t seed[64], master[48], keyblock[40];
        memcpy(seed, tls_client_random, 32); memcpy(seed + 32, tls_server_random, 32);
        tls12_prf(premaster, 32, "master secret", seed, 64, master, 48);
        memcpy(seed, tls_server_random, 32); memcpy(seed + 32, tls_client_random, 32);
        tls12_prf(master, 48, "key expansion", seed, 64, keyblock, 40);   // client_key|server_key|client_iv|server_iv
        const uint8_t* ckey = keyblock;              const uint8_t* skey = keyblock + 16;
        const uint8_t* civ  = keyblock + 32;         const uint8_t* siv  = keyblock + 36;
        TLSP("TLS: ECDHE(x25519) — pre-master secret computed; master secret + AES-128-GCM keys derived\n");

        // ---- Verify the ServerKeyExchange signature against the leaf certificate (v5.9.61) ----
        // Proves the ECDHE parameters were signed by the private key matching the certificate's
        // public key (ECDSA-P256). This does NOT yet verify the certificate's chain to a trusted
        // root — a MITM presenting its own valid cert would still pass here; the trust-anchor
        // check is the next increment.
        if (leaf_ptr && ske_sig_len && ske_sig_alg == 0x0403) {        // 0x0403 = ecdsa + sha256
            int checked = -2;
            const uint8_t* Q; uint32_t Qlen;
            if (der_x509_ec_pubkey(leaf_ptr, cert0, &Q, &Qlen) == 0 && Qlen == 65 && Q[0] == 0x04) {
                der_t sd = { tls_ske_sig, tls_ske_sig + ske_sig_len }, sq;
                uint8_t tg; const uint8_t* iv; uint32_t il; uint8_t rr[32], ss[32];
                if (der_enter(&sd, 0x30, &sq) == 0 &&
                    der_read(&sq, &tg, &iv, &il) == 0 && tg == 0x02) {
                    der_int_to_32(rr, iv, il);
                    if (der_read(&sq, &tg, &iv, &il) == 0 && tg == 0x02) {
                        der_int_to_32(ss, iv, il);
                        uint8_t e[32]; sha256_ctx_t hc; sha256_init(&hc);      // e = SHA256(cr||sr||params)
                        sha256_update(&hc, tls_client_random, 32);
                        sha256_update(&hc, tls_server_random, 32);
                        sha256_update(&hc, tls_ske_params, ske_params_len);
                        sha256_final(&hc, e);
                        checked = p256_ecdsa_verify(Q + 1, Q + 33, e, rr, ss);
                    }
                }
            }
            if (checked == 0) {
                TLSP("TLS: ServerKeyExchange signature VERIFIED against the leaf certificate (ECDSA-P256)\n");
            } else if (checked == -1) {
                TLSP("TLS: ServerKeyExchange signature INVALID — aborting the handshake (possible MITM)\n");
                tcp_close(conn); return -1;
            } else {
                TLSP("TLS: ServerKeyExchange signature not checked (cert key not P-256) — proceeding unverified\n");
            }
        } else if (got_ske) {
            TLSP("TLS: no ECDSA-P256 signature on the ServerKeyExchange (sig alg 0x%x) — proceeding unverified\n", ske_sig_alg);
        }

        // ---- Finished exchange (v5.9.54) ------------------------------------------------
        // The transcript is every handshake-layer message, in order, with no record headers:
        // ClientHello || ServerHello..ServerHelloDone || our ClientKeyExchange (+ our Finished).
        if ((uint32_t)(chlen - 5) + hn + 37 + 16 > sizeof(tls_ts)) {
            TLSP("TLS: handshake transcript too large to buffer — skipping Finished\n");
            tcp_close(conn); return -1;
        }
        uint32_t tn = 0;
        memcpy(tls_ts + tn, tls_hs + 5, (uint32_t)chlen - 5); tn += (uint32_t)chlen - 5;   // ClientHello
        memcpy(tls_ts + tn, tls_hd, hn); tn += hn;                                         // server flight

        uint8_t cke[37];                             // ClientKeyExchange (type 16): len 33 = pointlen(1) + point(32)
        cke[0] = 0x10; cke[1] = 0; cke[2] = 0; cke[3] = 0x21;
        cke[4] = 0x20; memcpy(cke + 5, eph_pub, 32);
        memcpy(tls_ts + tn, cke, 37); tn += 37;

        uint8_t cvd[12], cfin[16];                   // client Finished = verify_data over the transcript so far
        tls_verify_data(master, "client finished", tls_ts, tn, cvd);
        cfin[0] = 0x14; cfin[1] = 0; cfin[2] = 0; cfin[3] = 0x0c; memcpy(cfin + 4, cvd, 12);
        memcpy(tls_ts + tn, cfin, 16); tn += 16;

        uint8_t svd_exp[12];                         // the value the server's Finished must carry
        tls_verify_data(master, "server finished", tls_ts, tn, svd_exp);

        uint8_t f2[128]; uint32_t on = 0;            // flight 2: ClientKeyExchange + ChangeCipherSpec + Finished
        f2[on++] = 0x16; f2[on++] = 0x03; f2[on++] = 0x03; f2[on++] = 0x00; f2[on++] = 0x25;   // CKE record
        memcpy(f2 + on, cke, 37); on += 37;
        f2[on++] = 0x14; f2[on++] = 0x03; f2[on++] = 0x03; f2[on++] = 0x00; f2[on++] = 0x01; f2[on++] = 0x01;  // CCS
        uint8_t sealed[64];
        int sl = tls_seal_record(ckey, civ, 0, 0x16, cfin, 16, sealed);   // GCM-seal the Finished, client seq 0
        f2[on++] = 0x16; f2[on++] = 0x03; f2[on++] = 0x03; f2[on++] = (uint8_t)(sl >> 8); f2[on++] = (uint8_t)sl;
        memcpy(f2 + on, sealed, (uint32_t)sl); on += (uint32_t)sl;

        TLSP("TLS: sending ClientKeyExchange + ChangeCipherSpec + encrypted Finished...\n");
        if (tcp_send(conn, f2, (int)on) < 0) { TLSP("TLS: flight-2 send failed\n"); tcp_close(conn); return -1; }

        uint32_t t2 = 0, s2 = get_ticks(), l2 = s2;  // read the server's ChangeCipherSpec + Finished
        for (;;) {
            kernel_poll_net();
            int n = tcp_recv(conn, tls_rx + t2, (uint32_t)sizeof(tls_rx) - t2 - 1);
            if (n > 0) { t2 += (uint32_t)n; l2 = get_ticks(); }
            else if (n < 0) break;
            uint32_t now = get_ticks();
            if (t2 == 0) { if ((int32_t)(now - (s2 + 5000)) >= 0) break; }
            else if ((int32_t)(now - (l2 + 1000)) >= 0) break;
            if ((int32_t)(now - (s2 + 10000)) >= 0) break;
        }

        int verified = 0, sawccs = 0, s_alert = 0, s_desc = 0;
        uint32_t pp = 0;
        while (pp + 5 <= t2) {
            uint8_t rt = tls_rx[pp];
            uint16_t rl = (uint16_t)((tls_rx[pp+3] << 8) | tls_rx[pp+4]);
            if (pp + 5 + rl > t2) break;
            if (rt == 20) { sawccs = 1; }                       // ChangeCipherSpec
            else if (rt == 21 && rl >= 2 && !sawccs) { s_alert = 1; s_desc = tls_rx[pp+6]; }  // plaintext alert
            else if (rt == 22 && sawccs) {                      // the encrypted Finished
                uint8_t fin[64];
                int pl = tls_open_record(skey, siv, 0, 0x16, tls_rx + pp + 5, rl, fin);   // server seq 0
                if (pl == 16 && fin[0] == 0x14) {
                    if (tls_eq(fin + 4, svd_exp, 12)) verified = 1;
                    else TLSP("TLS: server Finished verify_data MISMATCH — transcript/keys disagree\n");
                } else if (pl < 0) {
                    TLSP("TLS: could not decrypt the server Finished (bad key or tag)\n");
                }
            }
            pp += 5 + rl;
        }
        if (s_alert) {
            TLSP("TLS: %s sent an Alert (description %d) after our Finished — handshake rejected\n", host, s_desc);
            tcp_close(conn); return -1;
        }
        if (!verified) {
            TLSP("TLS: handshake did not complete — no verified server Finished received\n");
            tcp_close(conn); return -1;
        }
        TLSP("TLS: *** HANDSHAKE COMPLETE — server Finished verified; secure channel established with %s ***\n", host);

        // ---- Encrypted https GET over the established channel ---------------------------
        // Our client Finished was record seq 0, so this GET is client seq 1; the server's
        // Finished was seq 0, so its first application-data reply is server seq 1.
        uint8_t req[512]; uint32_t reqn = 0;
        const char* r_get  = "GET ";
        const char* r_ver  = " HTTP/1.1\r\nHost: ";
        const char* r_tail = "\r\nConnection: close\r\nUser-Agent: NyxOS-Selene\r\n\r\n";
        for (const char* s = r_get;  *s; s++) req[reqn++] = (uint8_t)*s;
        for (const char* s = path;   *s && reqn < sizeof(req) - 128; s++) req[reqn++] = (uint8_t)*s;
        for (const char* s = r_ver;  *s; s++) req[reqn++] = (uint8_t)*s;
        for (const char* s = host;   *s && reqn < sizeof(req) - 64;  s++) req[reqn++] = (uint8_t)*s;
        for (const char* s = r_tail; *s; s++) req[reqn++] = (uint8_t)*s;

        uint8_t grec[5 + 512 + 24];                  // one application-data record (type 0x17)
        int gl = tls_seal_record(ckey, civ, 1, 0x17, req, reqn, grec + 5);   // client seq 1
        grec[0] = 0x17; grec[1] = 0x03; grec[2] = 0x03; grec[3] = (uint8_t)(gl >> 8); grec[4] = (uint8_t)gl;
        TLSP("TLS: sending an encrypted \"GET %s\" over the channel...\n", path);
        if (tcp_send(conn, grec, 5 + gl) < 0) { TLSP("TLS: GET send failed\n"); tcp_close(conn); return -1; }

        uint32_t r3 = 0, s3 = get_ticks(), l3 = s3;  // read the encrypted response
        for (;;) {
            kernel_poll_net();
            int n = tcp_recv(conn, tls_rx + r3, (uint32_t)sizeof(tls_rx) - r3 - 1);
            if (n > 0) { r3 += (uint32_t)n; l3 = get_ticks(); if (r3 >= sizeof(tls_rx) - 1) break; }
            else if (n < 0) break;                   // server closed (Connection: close)
            uint32_t now = get_ticks();
            if (r3 == 0) { if ((int32_t)(now - (s3 + 8000)) >= 0) break; }
            else if ((int32_t)(now - (l3 + 1500)) >= 0) break;
            if ((int32_t)(now - (s3 + 15000)) >= 0) break;
        }
        tcp_close(conn);

        uint32_t pn = 0; uint64_t sseq = 1;          // decrypt each application-data record into out
        uint32_t q = 0;
        while (q + 5 <= r3) {
            uint8_t rt = tls_rx[q];
            uint16_t rl2 = (uint16_t)((tls_rx[q+3] << 8) | tls_rx[q+4]);
            if (q + 5 + rl2 > r3) break;
            if (rt == 0x17) {                        // application data
                if (pn + rl2 > cap) break;
                int pl = tls_open_record(skey, siv, sseq, 0x17, tls_rx + q + 5, rl2, out + pn);
                if (pl < 0) { TLSP("TLS: could not decrypt application-data record %u\n", (unsigned)sseq); break; }
                pn += (uint32_t)pl; sseq++;
            } else if (rt == 0x15) { break; }        // encrypted close_notify alert — end of stream
            q += 5 + rl2;
        }

        if (pn == 0) { TLSP("TLS: no application data decrypted\n"); return -1; }
        if (verbose) {
            TLSP("TLS: decrypted %u bytes of https response:\n", pn);
            printf("------------------------------------------------------------\n");
            tls_print_text(out, pn, 480);
            printf("------------------------------------------------------------\n");
            TLSP("TLS: *** https fetch over TLS succeeded for %s ***\n", host);
            TLSP("TLS: [note] the ephemeral key now uses a CSPRNG; the server certificate is not verified yet.\n");
        }
        return (int)pn;
    } else if (got_ske) {
        TLSP("TLS: server chose curve 0x%x; only x25519 (0x001d) is wired up so far — no key agreement this run.\n", ske_curve);
    }
    tcp_close(conn);
    return -1;   // handshake reached but nothing was fetched
}

// The `tls <host>` diagnostic command: verbosely fetch host:443 "/" into a scratch buffer.
int tls_hello(const char* host, int iface_idx) {
    int n = tls_https_fetch(host, "/", iface_idx, tls_hd, sizeof(tls_hd), 1);
    return (n >= 0) ? 0 : -1;
}
