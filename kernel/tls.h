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

#endif
