#ifndef DNS_H
#define DNS_H

uint32_t dns_resolve(const char* hostname, int iface_idx);
void dns_set_server(uint32_t ip);
uint32_t dns_get_server(void);

// Parse a DNS response for the first A record. Returns 1 and writes the IPv4
// (network order) to *out_ip if found, else 0. Bounds-checked over [data,data+len)
// with no global state; rejects a response whose QR bit or transaction id
// (host order) does not match `expected_id`. The security-critical core of the
// response handler, pinned by dns_parse_selftest.
int dns_parse_a_record(const uint8_t* data, uint32_t len, uint16_t expected_id, uint32_t* out_ip);
int dns_parse_selftest(void);   // KAT: valid + hostile responses (0 = PASS)

#endif