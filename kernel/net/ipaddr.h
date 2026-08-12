#ifndef IPADDR_H
#define IPADDR_H
#include "../core/kernel.h"

// Strict dotted-quad IPv4 parser. On success writes the address in NETWORK byte order
// (first octet in the low byte, matching net_interfaces[].ip) and returns 0; returns -1
// on any malformed input (octet > 255, not exactly four octets, a non-digit, an empty
// octet, or trailing junk). Unlike the lenient parse_ip() heuristic used for the
// IP-or-hostname command arguments, this validates — it is the right parser for numeric
// config like `setip`, where a typo should be rejected rather than silently truncated.
int ipv4_parse(const char* s, uint32_t* out);

int ipv4_parse_selftest(void);   // KAT: valid vectors + adversarial malformed rejections

// Strict RFC 4291 IPv6 text parser. On success writes the 16 address bytes in network
// order (out[0] = high byte of the first hextet) and returns 0; returns -1 on any
// malformed input. Enforces: at most eight 16-bit hextets of 1..4 hex digits; at most one
// "::" zero-run, which must elide at least one group; and an optional dotted-quad IPv4
// tail (e.g. ::ffff:192.168.1.1) occupying the final two groups.
int ipv6_parse(const char* s, uint8_t out[16]);

int ipv6_parse_selftest(void);   // KAT: canonical forms (byte-checked) + malformed rejections

#endif
