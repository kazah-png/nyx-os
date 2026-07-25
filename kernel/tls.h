#ifndef TLS_H
#define TLS_H

// TLS handshake start (v5.9.48) — step 1 of the https arc.
// tls_hello() opens a TCP connection to host:443, sends a real TLS 1.2 ClientHello
// (record layer + SNI, cipher-suite and extension lists), then reads and parses the
// server's flight: ServerHello (chosen cipher), Certificate, ServerKeyExchange and
// ServerHelloDone. It proves NyxOS can start a TLS conversation with a real https
// server. No cryptography yet — the key exchange, AES-GCM and record decryption that
// actually fetch an https page come in later steps.
int tls_hello(const char* host, int iface_idx);

// Full https fetch over TLS 1.2: handshake with host:443, GET <path>, decrypt the reply
// into out[cap]. Returns the response length (>= 0) or -1 on failure; verbose gates prints.
int tls_https_fetch(const char* host, const char* path, int iface_idx,
                    uint8_t* out, uint32_t cap, int verbose);

// Known-answer self-test for the TLS 1.2 key schedule (master secret + AES key block).
int tls_keyschedule_selftest(void);

// Known-answer self-test for the AES-128-GCM record layer + Finished verify_data.
int tls_record_selftest(void);

#endif
