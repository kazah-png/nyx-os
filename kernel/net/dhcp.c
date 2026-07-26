#include "../core/kernel.h"
#include "dns.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
// Stored little-endian so memcpy() lays the bytes on the wire in the required
// order 63 82 53 63 (99.130.83.99). The RX path compares the received 4 bytes
// read back as a LE uint32 against this same value, so both stay consistent.
// (Was 0x63825363, which memcpy wrote reversed — slirp dropped every DISCOVER.)
#define DHCP_MAGIC_COOKIE 0x63538263

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define DHCP_OPT_SUBNET_MASK   1
#define DHCP_OPT_ROUTER        3
#define DHCP_OPT_DNS           6
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME   51
#define DHCP_OPT_MSG_TYPE     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_END         255

static uint32_t dhcp_xid = 0x12345678;
static int dhcp_state = 0;
static uint32_t dhcp_offered_ip = 0;
static uint32_t dhcp_server_ip = 0;

static uint8_t dhcp_rx_buf[1024];
static int dhcp_rx_len = 0;
static uint32_t dhcp_rx_src = 0;

static void dhcp_rx_handler(uint8_t* data, uint32_t len, uint32_t src_ip, uint16_t src_port) {
    (void)src_port;
    if (len > sizeof(dhcp_rx_buf)) len = sizeof(dhcp_rx_buf);
    memcpy(dhcp_rx_buf, data, len);
    dhcp_rx_len = len;
    dhcp_rx_src = src_ip;
}

int dhcp_request(int iface_idx) {
    if (iface_idx < 0 || iface_idx >= 8) return -1;
    if (!net_interfaces[iface_idx].name[0]) return -1;

    dhcp_state = 0;
    dhcp_offered_ip = 0;
    dhcp_server_ip = 0;
    dhcp_rx_len = 0;
    dhcp_xid = (dhcp_xid + 1) | 0x12340000;

    udp_register_listener(DHCP_CLIENT_PORT, dhcp_rx_handler);

    uint8_t pkt[512];
    memset_asm(pkt, 0, sizeof(pkt));
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6;
    pkt[3] = 0;
    memcpy(&pkt[4], &dhcp_xid, 4);
    pkt[10] = 0x80; pkt[11] = 0x00;
    memcpy(&pkt[28], net_interfaces[iface_idx].mac, 6);
    uint32_t cookie = DHCP_MAGIC_COOKIE;
    memcpy(&pkt[236], &cookie, 4);   // magic cookie at BOOTP offset 236
    int opt = 240;                    // DHCP options begin at 240
    pkt[opt++] = DHCP_OPT_MSG_TYPE;
    pkt[opt++] = 1;
    pkt[opt++] = DHCP_DISCOVER;
    pkt[opt++] = DHCP_OPT_END;

    udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, pkt, opt, iface_idx);
    printf("[DHCP] DISCOVER sent (xid=0x%x)\n", dhcp_xid);

    for (int retry = 0; retry < 400; retry++) {
        kernel_poll_net();
        // Give replies time to arrive between polls. The PIT timer never fires
        // (see kernel.c) so sleep()/tick_count are unavailable; busy-wait on an
        // unused I/O port instead (~timer-independent, works under QEMU TCG).
        for (int d = 0; d < 1500; d++) inb(0x80);

        if (dhcp_rx_len > 0) {
            uint8_t *rx = dhcp_rx_buf;
            uint32_t rx_xid;
            memcpy(&rx_xid, &rx[4], 4);
            uint32_t cookie;
            memcpy(&cookie, &rx[236], 4);   // magic cookie at BOOTP offset 236

            if (rx_xid == dhcp_xid && rx[0] == 2 && cookie == DHCP_MAGIC_COOKIE) {
                int o = 240;                 // options begin at 240
                uint8_t msg_type = 0;
                while (o < dhcp_rx_len && rx[o] != DHCP_OPT_END) {
                    if (rx[o] == DHCP_OPT_MSG_TYPE && rx[o+1] == 1) {
                        msg_type = rx[o+2];
                        break;
                    }
                    o += rx[o+1] + 2;
                }

                if (msg_type == DHCP_OFFER && dhcp_state == 0) {
                    memcpy(&dhcp_offered_ip, &rx[16], 4);
                    dhcp_server_ip = dhcp_rx_src;
                    printf("[DHCP] OFFER: IP %d.%d.%d.%d from server %d.%d.%d.%d\n",
                        IP4_OCTETS(dhcp_offered_ip), IP4_OCTETS(dhcp_server_ip));
                    dhcp_state = 1;
                }

                if (msg_type == DHCP_ACK && dhcp_state == 2) {
                    net_interfaces[iface_idx].ip = dhcp_offered_ip;
                    int o2 = 240;                // options begin at 240
                    while (o2 < dhcp_rx_len && rx[o2] != DHCP_OPT_END) {
                        if (rx[o2] == DHCP_OPT_SUBNET_MASK && rx[o2+1] == 4)
                            memcpy(&net_interfaces[iface_idx].netmask, &rx[o2+2], 4);
                        if (rx[o2] == DHCP_OPT_ROUTER && rx[o2+1] >= 4)
                            memcpy(&net_interfaces[iface_idx].gateway, &rx[o2+2], 4);
                        if (rx[o2] == 6 && rx[o2+1] >= 4) { // DNS server
                            uint32_t dns_ip;
                            memcpy(&dns_ip, &rx[o2+2], 4);
                            dns_set_server(dns_ip);
                        }
                        o2 += rx[o2+1] + 2;
                    }
                    printf("[DHCP] ACK: IP=%d.%d.%d.%d mask=%d.%d.%d.%d gw=%d.%d.%d.%d\n",
                        IP4_OCTETS(dhcp_offered_ip),
                        IP4_OCTETS(net_interfaces[iface_idx].netmask),
                        IP4_OCTETS(net_interfaces[iface_idx].gateway));
                    return 0;
                }
            }
            dhcp_rx_len = 0;
        }

        if (dhcp_state == 1) {
            memset_asm(pkt, 0, sizeof(pkt));
            pkt[0] = 1; pkt[1] = 1; pkt[2] = 6; pkt[3] = 0;
            memcpy(&pkt[4], &dhcp_xid, 4);
            pkt[10] = 0x80; pkt[11] = 0x00;
            memcpy(&pkt[28], net_interfaces[iface_idx].mac, 6);
            cookie = DHCP_MAGIC_COOKIE;
            memcpy(&pkt[236], &cookie, 4);   // magic cookie at 236, options at 240
            opt = 240;
            pkt[opt++] = DHCP_OPT_MSG_TYPE; pkt[opt++] = 1; pkt[opt++] = DHCP_REQUEST;
            pkt[opt++] = DHCP_OPT_REQUESTED_IP; pkt[opt++] = 4;
            memcpy(&pkt[opt], &dhcp_offered_ip, 4); opt += 4;
            pkt[opt++] = DHCP_OPT_SERVER_ID; pkt[opt++] = 4;
            memcpy(&pkt[opt], &dhcp_server_ip, 4); opt += 4;
            pkt[opt++] = DHCP_OPT_END;
            udp_send(0xFFFFFFFF, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, pkt, opt, iface_idx);
            printf("[DHCP] REQUEST sent\n");
            dhcp_state = 2;
        }

    }

    printf("[DHCP] Failed\n");
    return -1;
}

void cmd_dhcp(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("[DHCP] Starting...\n");
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            dhcp_request(i);
            return;
        }
    }
    printf("[DHCP] No interface available\n");
}
