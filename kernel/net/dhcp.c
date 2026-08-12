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

// Return the value of DHCP option `want` from the options area (BOOTP offset 240) of a
// received packet rx[0..len), copying up to `outcap` bytes into `out` and returning the
// option's length, or -1 if the option is absent / malformed / would read past `len`.
// EVERY access is bounded by len — the code, length and value bytes are all attacker-
// controlled, so a truncated or crafted packet must never read past the received data
// (or past dhcp_rx_buf[] when len is its full size). PAD (code 0) is a single byte with
// no length field; END (255) or running out of bytes stops the walk. This one hardened
// walker replaces the two ad-hoc option loops the OFFER and ACK paths used to inline.
// Pinned by dhcp_options_selftest().
static int dhcp_get_option(const uint8_t* rx, int len, uint8_t want, uint8_t* out, int outcap) {
    int o = 240;
    while (o + 1 < len && rx[o] != DHCP_OPT_END) {
        uint8_t code = rx[o];
        if (code == 0) { o++; continue; }             // PAD: one byte, no length
        uint8_t optlen = rx[o+1];
        if (o + 2 + (int)optlen > len) return -1;      // option body runs past the packet
        if (code == want) {
            int n = (int)optlen < outcap ? (int)optlen : outcap;
            for (int i = 0; i < n; i++) out[i] = rx[o+2+i];
            return (int)optlen;
        }
        o += (int)optlen + 2;
    }
    return -1;
}

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
            uint32_t rx_cookie;              // the cookie we RECEIVED (distinct from the sent `cookie` above)
            memcpy(&rx_cookie, &rx[236], 4);  // magic cookie at BOOTP offset 236

            if (rx_xid == dhcp_xid && rx[0] == 2 && rx_cookie == DHCP_MAGIC_COOKIE) {
                uint8_t msg_type = 0, mt;
                if (dhcp_get_option(rx, dhcp_rx_len, DHCP_OPT_MSG_TYPE, &mt, 1) == 1)
                    msg_type = mt;

                if (msg_type == DHCP_OFFER && dhcp_state == 0) {
                    memcpy(&dhcp_offered_ip, &rx[16], 4);
                    dhcp_server_ip = dhcp_rx_src;
                    printf("[DHCP] OFFER: IP %d.%d.%d.%d from server %d.%d.%d.%d\n",
                        IP4_OCTETS(dhcp_offered_ip), IP4_OCTETS(dhcp_server_ip));
                    dhcp_state = 1;
                }

                if (msg_type == DHCP_ACK && dhcp_state == 2) {
                    net_interfaces[iface_idx].ip = dhcp_offered_ip;
                    // The same hardened walker extracts the network config; it copies at
                    // most 4 bytes and only when the whole option fits in dhcp_rx_len, so
                    // none of these can source bytes from past the received packet.
                    uint8_t val[4];
                    if (dhcp_get_option(rx, dhcp_rx_len, DHCP_OPT_SUBNET_MASK, val, 4) == 4)
                        memcpy(&net_interfaces[iface_idx].netmask, val, 4);
                    if (dhcp_get_option(rx, dhcp_rx_len, DHCP_OPT_ROUTER, val, 4) >= 4)
                        memcpy(&net_interfaces[iface_idx].gateway, val, 4);
                    if (dhcp_get_option(rx, dhcp_rx_len, DHCP_OPT_DNS, val, 4) >= 4) {
                        uint32_t dns_ip; memcpy(&dns_ip, val, 4); dns_set_server(dns_ip);
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

// ---- known-answer self-test (`dhcpopt`) ----
// Pins dhcp_get_option: correct extraction on well-formed OFFER/ACK options, and no
// out-of-bounds read (a safe -1) on truncated, overlong, past-end and END-less packets.
int dhcp_options_selftest(void) {
    static uint8_t b[512];
    uint8_t v[4];
    for (int i = 0; i < 512; i++) b[i] = 0;

    // Well-formed OFFER: option 53 (msg type) len 1 value 2, then END.
    b[240] = DHCP_OPT_MSG_TYPE; b[241] = 1; b[242] = DHCP_OFFER; b[243] = DHCP_OPT_END;
    if (dhcp_get_option(b, 512, DHCP_OPT_MSG_TYPE, v, 1) != 1 || v[0] != DHCP_OFFER) return 1;
    if (dhcp_get_option(b, 512, DHCP_OPT_ROUTER, v, 4) != -1) return 2;   // absent -> -1

    // Well-formed ACK with subnet/router/dns.
    for (int i = 0; i < 512; i++) b[i] = 0;
    int o = 240;
    b[o++] = DHCP_OPT_MSG_TYPE;  b[o++] = 1; b[o++] = DHCP_ACK;
    b[o++] = DHCP_OPT_SUBNET_MASK;b[o++] = 4; b[o++]=255; b[o++]=255; b[o++]=255; b[o++]=0;
    b[o++] = DHCP_OPT_ROUTER;    b[o++] = 4; b[o++]=10;  b[o++]=0;  b[o++]=0;  b[o++]=1;
    b[o++] = DHCP_OPT_DNS;       b[o++] = 4; b[o++]=8;   b[o++]=8;  b[o++]=8;  b[o++]=8;
    b[o++] = DHCP_OPT_END;
    if (dhcp_get_option(b, 512, DHCP_OPT_MSG_TYPE, v, 1) != 1 || v[0] != DHCP_ACK) return 3;
    if (dhcp_get_option(b, 512, DHCP_OPT_SUBNET_MASK, v, 4) != 4 || v[0] != 255 || v[3] != 0) return 4;
    if (dhcp_get_option(b, 512, DHCP_OPT_ROUTER, v, 4) != 4 || v[0] != 10 || v[3] != 1) return 5;
    if (dhcp_get_option(b, 512, DHCP_OPT_DNS, v, 4) != 4 || v[0] != 8) return 6;

    // Adversarial: truncated packet (no options past 240) must never read past len.
    for (int i = 0; i < 512; i++) b[i] = 0;
    if (dhcp_get_option(b, 240, DHCP_OPT_MSG_TYPE, v, 1) != -1) return 7;
    if (dhcp_get_option(b, 10,  DHCP_OPT_MSG_TYPE, v, 1) != -1) return 8;

    // Adversarial: an option whose length runs past the packet end -> reject, no OOB.
    for (int i = 0; i < 512; i++) b[i] = 0;
    b[240] = DHCP_OPT_SUBNET_MASK; b[241] = 200;
    if (dhcp_get_option(b, 250, DHCP_OPT_SUBNET_MASK, v, 4) != -1) return 9;

    // Adversarial: no END, options fill exactly to len — walk stops in-bounds.
    for (int i = 0; i < 512; i++) b[i] = 0;
    b[240] = DHCP_OPT_MSG_TYPE; b[241] = 1; b[242] = DHCP_OFFER;   // no END; len is 243
    if (dhcp_get_option(b, 243, DHCP_OPT_MSG_TYPE, v, 1) != 1 || v[0] != DHCP_OFFER) return 10;
    if (dhcp_get_option(b, 243, DHCP_OPT_ROUTER, v, 4) != -1) return 11;

    // A PAD (code 0, no length) is skipped as a single byte.
    for (int i = 0; i < 512; i++) b[i] = 0;
    b[240] = 0; b[241] = 0; b[242] = DHCP_OPT_MSG_TYPE; b[243] = 1; b[244] = DHCP_ACK; b[245] = DHCP_OPT_END;
    if (dhcp_get_option(b, 512, DHCP_OPT_MSG_TYPE, v, 1) != 1 || v[0] != DHCP_ACK) return 12;

    return 0;
}
