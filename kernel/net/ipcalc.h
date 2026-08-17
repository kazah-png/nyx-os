#ifndef IPCALC_H
#define IPCALC_H
#include "../core/kernel.h"
// IPv4 subnet calculator. From a HOST-ORDER address (octet1 = high byte) + a prefix length
// (0..32), derive the network and broadcast addresses, the netmask and wildcard (hostmask),
// the usable host range, and the usable host count — RFC-correct including the /31 point-to-
// point case (RFC 3021: both addresses usable) and the /32 single host. Pure math, verified
// byte-exact against Python's ipaddress across every prefix. cmd_ipcalc parses the CIDR/mask
// text and formats the report.
typedef struct {
    uint32_t network, broadcast, netmask, hostmask, hostmin, hostmax;
    uint32_t hosts;      // usable host count (fits u32: max 2^32-2 for /0)
    int prefix;
} ipcalc_result_t;

void ipcalc_compute(uint32_t host_ip, int prefix, ipcalc_result_t* r);
int  ipcalc_selftest(void);   // KAT: canonical /0../32 vectors incl. /31 and /32 edges
#endif
