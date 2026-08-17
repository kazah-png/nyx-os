// ============================================================
// ipcalc.c - IPv4 subnet calculator (pure math + KAT)
// ============================================================
// From a host-order address + prefix, derive network/broadcast/netmask/wildcard, the usable
// host range and count. RFC-correct at the edges: /31 is RFC 3021 point-to-point (2 usable
// addresses, no network/broadcast reserve) and /32 is a single host. Verified byte-exact
// against Python's ipaddress across every prefix before landing (see scratchpad/ipcalc_proto.c).
#include "ipcalc.h"

void ipcalc_compute(uint32_t hip, int p, ipcalc_result_t* r) {
    uint32_t mask = (p == 0) ? 0u : (0xFFFFFFFFu << (32 - p));   // p==32 -> <<0 -> all ones
    r->prefix    = p;
    r->netmask   = mask;
    r->hostmask  = ~mask;
    r->network   = hip & mask;
    r->broadcast = r->network | r->hostmask;
    if (p >= 31) {                       // /31: both usable (RFC 3021); /32: the host itself
        r->hosts   = (p == 32) ? 1u : 2u;
        r->hostmin = r->network;
        r->hostmax = r->broadcast;       // /32: == network; /31: network+1
    } else {
        r->hosts   = (uint32_t)(((uint64_t)1 << (32 - p)) - 2);
        r->hostmin = r->network + 1;
        r->hostmax = r->broadcast - 1;
    }
}

#define IP(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))

int ipcalc_selftest(void) {
    struct { uint32_t ip; int p; uint32_t net, bc, nm, hmin, hmax, hosts; } v[] = {
        { IP(192,168,1,130), 26, IP(192,168,1,128), IP(192,168,1,191), IP(255,255,255,192), IP(192,168,1,129), IP(192,168,1,190), 62u },
        { IP(10,0,0,1),       8, IP(10,0,0,0),       IP(10,255,255,255), IP(255,0,0,0),       IP(10,0,0,1),       IP(10,255,255,254), 16777214u },
        { IP(172,16,5,23),   30, IP(172,16,5,20),    IP(172,16,5,23),    IP(255,255,255,252), IP(172,16,5,21),    IP(172,16,5,22),    2u },
        { IP(192,168,0,0),   31, IP(192,168,0,0),    IP(192,168,0,1),    IP(255,255,255,254), IP(192,168,0,0),    IP(192,168,0,1),    2u },
        { IP(8,8,8,8),       32, IP(8,8,8,8),        IP(8,8,8,8),        IP(255,255,255,255), IP(8,8,8,8),        IP(8,8,8,8),        1u },
        { IP(0,0,0,0),        0, IP(0,0,0,0),        IP(255,255,255,255),IP(0,0,0,0),         IP(0,0,0,1),        IP(255,255,255,254), 4294967294u },
    };
    for (int i = 0; i < (int)(sizeof(v)/sizeof(v[0])); i++) {
        ipcalc_result_t r; ipcalc_compute(v[i].ip, v[i].p, &r);
        if (r.network != v[i].net || r.broadcast != v[i].bc || r.netmask != v[i].nm ||
            r.hostmin != v[i].hmin || r.hostmax != v[i].hmax || r.hosts != v[i].hosts) return 10 + i;
        if (r.hostmask != (uint32_t)~v[i].nm) return 20 + i;
    }
    return 0;
}
