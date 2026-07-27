#include "../core/kernel.h"

#define ETH_HEADER_LEN 14
#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IP    0x0800

typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t type;
} eth_header_t;

extern int rtl8139_send_packet(const uint8_t* data, uint32_t len);
extern int rtl8139_receive_packet(uint8_t* buffer, uint32_t max_len);
extern void arp_handle_packet(uint8_t* packet, uint32_t len);
extern void ip_handle_packet(uint8_t* packet, uint32_t len);

int eth_send(const uint8_t* dst_mac, uint16_t type, const uint8_t* data, uint32_t len, int iface_idx) {
    uint8_t* packet = (uint8_t*)kmalloc(ETH_HEADER_LEN + len);
    if (!packet) return -1;
    eth_header_t* hdr = (eth_header_t*)packet;
    memcpy(hdr->dst_mac, dst_mac, 6);
    memcpy(hdr->src_mac, net_interfaces[iface_idx].mac, 6);
    hdr->type = htons(type);
    if (data && len > 0) memcpy(packet + ETH_HEADER_LEN, data, len);
    int result = rtl8139_send_packet(packet, ETH_HEADER_LEN + len);
    kfree(packet);
    return result;
}

void eth_poll(int iface_idx) {
    uint8_t buffer[2048];
    int len = rtl8139_receive_packet(buffer, sizeof(buffer));
    if (len <= 0) return;
    if (len < ETH_HEADER_LEN) return;
    // Defensive: never trust the driver to have clamped to the buffer we handed it — a
    // return longer than `buffer` would run the handlers off the end of this stack array.
    // (rtl8139_receive_packet does clamp to max_len today; this keeps eth_poll safe on its
    // own, not merely because the current driver happens to.)
    if (len > (int)sizeof(buffer)) len = (int)sizeof(buffer);
    eth_header_t* hdr = (eth_header_t*)buffer;
    // The NIC is left in promiscuous mode (RCR AAP), so it hands us frames addressed to
    // OTHER hosts too. Only act on a frame that is actually for us — our own unicast MAC,
    // or a group address (broadcast/multicast: the low bit of the first octet is set) — so
    // we don't ARP-cache senders or parse IP headers from traffic destined elsewhere.
    if (iface_idx >= 0 && iface_idx < 8) {
        const uint8_t* om = net_interfaces[iface_idx].mac;
        const uint8_t* dm = hdr->dst_mac;
        int for_us = (dm[0] & 0x01) ||
                     (dm[0] == om[0] && dm[1] == om[1] && dm[2] == om[2] &&
                      dm[3] == om[3] && dm[4] == om[4] && dm[5] == om[5]);
        if (!for_us) return;
    }
    uint16_t type = ntohs(hdr->type);
    uint8_t* payload = buffer + ETH_HEADER_LEN;
    uint32_t payload_len = len - ETH_HEADER_LEN;
    if (type == ETH_TYPE_ARP) arp_handle_packet(payload, payload_len);
    else if (type == ETH_TYPE_IP) ip_handle_packet(payload, payload_len);
}
