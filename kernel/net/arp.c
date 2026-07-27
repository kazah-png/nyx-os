#include "../core/kernel.h"

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IP       0x0800
#define ARP_HLEN 6
#define ARP_PLEN 4
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define ARP_CACHE_SIZE 16

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} arp_packet_t;

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
} arp_cache_t;

static arp_cache_t arp_cache[ARP_CACHE_SIZE];

extern int eth_send(const uint8_t* dst_mac, uint16_t type, const uint8_t* data, uint32_t len, int iface_idx);
static uint8_t arp_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void arp_init(void) {
    // No static entries: the previous gateway entry used host byte order and our
    // own MAC (wrong). The gateway (10.0.2.2 under QEMU) answers ARP, so it is
    // resolved dynamically like any other host.
    memset_asm(arp_cache, 0, sizeof(arp_cache));
}

static int arp_cache_lookup(uint32_t ip, uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid || arp_cache[i].ip == ip) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    }
    // Replace oldest
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].ip = ip;
}

void arp_send_request(uint32_t target_ip, int iface_idx) {
    arp_packet_t arp;
    arp.htype = htons(ARP_HTYPE_ETHERNET);
    arp.ptype = htons(ARP_PTYPE_IP);
    arp.hlen = ARP_HLEN;
    arp.plen = ARP_PLEN;
    arp.oper = htons(ARP_OP_REQUEST);
    memcpy(arp.sender_mac, net_interfaces[iface_idx].mac, 6);
    // IPs are stored network-order (low byte = first octet), so the wire bytes
    // are the uint32 bytes in ascending order.
    uint32_t sip = net_interfaces[iface_idx].ip;
    arp.sender_ip[0] = sip & 0xFF;
    arp.sender_ip[1] = (sip >> 8) & 0xFF;
    arp.sender_ip[2] = (sip >> 16) & 0xFF;
    arp.sender_ip[3] = (sip >> 24) & 0xFF;
    memset_asm(arp.target_mac, 0, 6);
    arp.target_ip[0] = target_ip & 0xFF;
    arp.target_ip[1] = (target_ip >> 8) & 0xFF;
    arp.target_ip[2] = (target_ip >> 16) & 0xFF;
    arp.target_ip[3] = (target_ip >> 24) & 0xFF;
    eth_send(arp_broadcast, 0x0806, (uint8_t*)&arp, sizeof(arp_packet_t), iface_idx);
}

void arp_handle_packet(uint8_t* packet, uint32_t len) {
    if (len < sizeof(arp_packet_t)) return;
    arp_packet_t* arp = (arp_packet_t*)packet;
    // Only process well-formed IPv4-over-Ethernet ARP (RFC 826 checks the hardware and
    // protocol types first). The sender_mac/sender_ip and target_* fields sit at fixed
    // offsets that assume hlen=6/plen=4; a packet with a different hardware/protocol type
    // or address lengths lays those fields out elsewhere, so caching the sender or
    // matching target_ip would act on misread bytes. Real ARP on our links is always
    // Ethernet/IPv4, so this rejects only malformed or non-IPv4 ARP.
    if (ntohs(arp->htype) != ARP_HTYPE_ETHERNET || ntohs(arp->ptype) != ARP_PTYPE_IP ||
        arp->hlen != ARP_HLEN || arp->plen != ARP_PLEN) return;
    uint16_t oper = ntohs(arp->oper);
    uint32_t sender_ip = (uint32_t)arp->sender_ip[0] |
                         ((uint32_t)arp->sender_ip[1] << 8) |
                         ((uint32_t)arp->sender_ip[2] << 16) |
                         ((uint32_t)arp->sender_ip[3] << 24);
    arp_cache_add(sender_ip, arp->sender_mac);
    if (oper == ARP_OP_REQUEST) {
        for (int i = 0; i < 8; i++) {
            if (!net_interfaces[i].name[0]) continue;
            uint32_t iface_ip = net_interfaces[i].ip;
            uint32_t target_ip = (uint32_t)arp->target_ip[0] |
                                 ((uint32_t)arp->target_ip[1] << 8) |
                                 ((uint32_t)arp->target_ip[2] << 16) |
                                 ((uint32_t)arp->target_ip[3] << 24);
            if (iface_ip == target_ip) {
                arp_packet_t reply;
                reply.htype = arp->htype;
                reply.ptype = arp->ptype;
                reply.hlen = ARP_HLEN;
                reply.plen = ARP_PLEN;
                reply.oper = htons(ARP_OP_REPLY);
                memcpy(reply.sender_mac, net_interfaces[i].mac, 6);
                memcpy(reply.sender_ip, arp->target_ip, 4);
                memcpy(reply.target_mac, arp->sender_mac, 6);
                memcpy(reply.target_ip, arp->sender_ip, 4);
                eth_send(arp->sender_mac, 0x0806, (uint8_t*)&reply, sizeof(arp_packet_t), i);
                break;
            }
        }
    }
}

int arp_resolve(uint32_t ip, uint8_t* mac, int iface_idx) {
    if (arp_cache_lookup(ip, mac)) return 1;
    arp_send_request(ip, iface_idx);
    printf("[ARP] Resolving %d.%d.%d.%d...\n", IP4_OCTETS(ip));
    // sleep() busy-waits on tick_count, which never advances (the PIT timer
    // doesn't fire — see kernel.c), so it would hang forever. Poll with a
    // port-I/O busy-wait between iterations instead.
    for (int retry = 0; retry < 120; retry++) {
        kernel_poll_net();
        for (int d = 0; d < 1500; d++) inb(0x80);
        if (arp_cache_lookup(ip, mac)) {
            printf("[ARP] Resolved\n");
            return 1;
        }
        if (retry == 10 || retry == 50) {
            arp_send_request(ip, iface_idx);
        }
    }
    printf("[ARP] Failed to resolve\n");
    return 0;
}
