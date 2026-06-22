#include "kernel.h"

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
    memset_asm(arp_cache, 0, sizeof(arp_cache));
    // Static ARP entry for QEMU user-mode gateway (10.0.2.2)
    int idx = 0;
    arp_cache[idx].ip = 0x0A000202;
    arp_cache[idx].mac[0] = 0x52; arp_cache[idx].mac[1] = 0x54;
    arp_cache[idx].mac[2] = 0x00; arp_cache[idx].mac[3] = 0x12;
    arp_cache[idx].mac[4] = 0x34; arp_cache[idx].mac[5] = 0x56;
    arp_cache[idx].valid = 1;
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
    arp.htype = (ARP_HTYPE_ETHERNET << 8) | (ARP_HTYPE_ETHERNET >> 8);
    arp.ptype = 0x0008;
    arp.hlen = ARP_HLEN;
    arp.plen = ARP_PLEN;
    arp.oper = (ARP_OP_REQUEST << 8) | (ARP_OP_REQUEST >> 8);
    memcpy(arp.sender_mac, net_interfaces[iface_idx].mac, 6);
    arp.sender_ip[0] = (net_interfaces[iface_idx].ip >> 24) & 0xFF;
    arp.sender_ip[1] = (net_interfaces[iface_idx].ip >> 16) & 0xFF;
    arp.sender_ip[2] = (net_interfaces[iface_idx].ip >> 8) & 0xFF;
    arp.sender_ip[3] = net_interfaces[iface_idx].ip & 0xFF;
    memset_asm(arp.target_mac, 0, 6);
    arp.target_ip[0] = (target_ip >> 24) & 0xFF;
    arp.target_ip[1] = (target_ip >> 16) & 0xFF;
    arp.target_ip[2] = (target_ip >> 8) & 0xFF;
    arp.target_ip[3] = target_ip & 0xFF;
    eth_send(arp_broadcast, 0x0806, (uint8_t*)&arp, sizeof(arp_packet_t), iface_idx);
}

void arp_handle_packet(uint8_t* packet, uint32_t len) {
    if (len < sizeof(arp_packet_t)) return;
    arp_packet_t* arp = (arp_packet_t*)packet;
    uint16_t oper = ((arp->oper << 8) & 0xFF00) | ((arp->oper >> 8) & 0x00FF);
    uint32_t sender_ip = ((uint32_t)arp->sender_ip[0] << 24) |
                         ((uint32_t)arp->sender_ip[1] << 16) |
                         ((uint32_t)arp->sender_ip[2] << 8) |
                         arp->sender_ip[3];
    arp_cache_add(sender_ip, arp->sender_mac);
    if (oper == ARP_OP_REQUEST) {
        for (int i = 0; i < 8; i++) {
            if (!net_interfaces[i].name[0]) continue;
            uint32_t iface_ip = net_interfaces[i].ip;
            uint32_t target_ip = ((uint32_t)arp->target_ip[0] << 24) |
                                 ((uint32_t)arp->target_ip[1] << 16) |
                                 ((uint32_t)arp->target_ip[2] << 8) |
                                 arp->target_ip[3];
            if (iface_ip == target_ip) {
                arp_packet_t reply;
                reply.htype = arp->htype;
                reply.ptype = arp->ptype;
                reply.hlen = ARP_HLEN;
                reply.plen = ARP_PLEN;
                reply.oper = (ARP_OP_REPLY << 8) | (ARP_OP_REPLY >> 8);
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
    if (arp_cache_lookup(ip, mac)) {
        printf("[ARP] Cache hit for %d.%d.%d.%d\n",
               (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF);
        return 1;
    }
    arp_send_request(ip, iface_idx);
    printf("[ARP] Resolving %d.%d.%d.%d...\n",
           (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF);
    for (int retry = 0; retry < 20; retry++) {
        sleep(50);
        kernel_poll_net();
        if (arp_cache_lookup(ip, mac)) {
            printf("[ARP] Resolved\n");
            return 1;
        }
        if (retry == 3 || retry == 10) {
            printf("[ARP] Retry %d...\n", retry);
            arp_send_request(ip, iface_idx);
        }
    }
    printf("[ARP] Failed to resolve\n");
    return 0;
}
