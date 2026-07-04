#include "kernel.h"

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint8_t  data[];
} icmp_header_t;

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint32_t len, int iface_idx);

static uint16_t icmp_checksum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2) {
        sum += ((uint16_t)data[i] << 8) | (i + 1 < len ? data[i+1] : 0);
        if (sum & 0xFFFF0000) sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~(sum & 0xFFFF);
}

int icmp_send_echo(uint32_t dst_ip, uint16_t id, uint16_t seq, int iface_idx) {
    uint32_t packet_len = sizeof(icmp_header_t) + 56;
    uint8_t* packet = (uint8_t*)kmalloc(packet_len);
    if (!packet) return -1;
    icmp_header_t* icmp = (icmp_header_t*)packet;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = ((id << 8) & 0xFF00) | ((id >> 8) & 0x00FF);
    icmp->seq = ((seq << 8) & 0xFF00) | ((seq >> 8) & 0x00FF);
    for (uint32_t i = 0; i < 56; i++) icmp->data[i] = i;
    icmp->checksum = icmp_checksum(packet, packet_len);
    int result = ip_send(dst_ip, 1, packet, packet_len, iface_idx);
    kfree(packet);
    return result;
}

extern uint32_t get_ticks(void);
extern void kernel_poll_net(void);
extern uint8_t ip_last_rx_ttl(void);

// Reply-matching state, shared with icmp_ping (same translation unit). The
// client arms (ident, wait_seq); the handler records the receipt on a match.
static volatile uint16_t ping_ident      = 0;
static volatile int      ping_wait_seq    = -1;
static volatile int      ping_got_reply   = 0;
static volatile uint32_t ping_reply_tick  = 0;
static volatile uint8_t  ping_reply_ttl   = 0;
static volatile uint32_t ping_reply_len   = 0;

void icmp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip) {
    if (len < sizeof(icmp_header_t)) return;
    icmp_header_t* icmp = (icmp_header_t*)packet;

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST) {
        // Answer like a real host: bounce the payload back as an echo reply.
        // The id/seq/data are reused verbatim; only type and checksum change.
        // ip_send(-1) loops back automatically when src_ip is one of our own.
        uint8_t* reply = (uint8_t*)kmalloc(len);
        if (!reply) return;
        memcpy(reply, packet, len);
        icmp_header_t* r = (icmp_header_t*)reply;
        r->type = ICMP_TYPE_ECHO_REPLY;
        r->code = 0;
        r->checksum = 0;
        r->checksum = icmp_checksum(reply, len);
        ip_send(src_ip, 1, reply, len, -1);
        kfree(reply);
        return;
    }

    if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        uint16_t id  = ((icmp->id  << 8) & 0xFF00) | ((icmp->id  >> 8) & 0x00FF);
        uint16_t seq = ((icmp->seq << 8) & 0xFF00) | ((icmp->seq >> 8) & 0x00FF);
        if (id == ping_ident && (int)seq == ping_wait_seq) {
            ping_reply_tick = get_ticks();
            ping_reply_ttl  = ip_last_rx_ttl();
            ping_reply_len  = len;
            ping_got_reply  = 1;
        }
    }
}

// Poll the receive path (NIC + loopback ring) until `deadline` or a reply.
static void ping_wait(uint32_t deadline) {
    while ((int32_t)(get_ticks() - deadline) < 0) {
        kernel_poll_net();
        if (ping_got_reply) return;
    }
}

int icmp_ping(uint32_t dst_ip, int count, int iface_idx) {
    ping_ident = (uint16_t)(0xBEEF ^ (get_ticks() & 0xFFFF));
    int received = 0;
    uint32_t rtt_min = 0xFFFFFFFF, rtt_max = 0, rtt_sum = 0;

    for (int i = 0; i < count; i++) {
        int seq = i + 1;
        ping_wait_seq  = seq;
        ping_got_reply = 0;

        uint32_t t0 = get_ticks();
        if (icmp_send_echo(dst_ip, ping_ident, (uint16_t)seq, iface_idx) < 0) {
            printf("ping: send failed for icmp_seq=%d\n", seq);
            ping_wait_seq = -1;
            continue;
        }

        ping_wait(t0 + 1000);   // 1 s timeout, draining RX meanwhile

        if (ping_got_reply) {
            uint32_t rtt = ping_reply_tick - t0;
            received++;
            rtt_sum += rtt;
            if (rtt < rtt_min) rtt_min = rtt;
            if (rtt > rtt_max) rtt_max = rtt;
            printf("%d bytes from %d.%d.%d.%d: icmp_seq=%d ttl=%d time=%d ms\n",
                   ping_reply_len,
                   dst_ip&0xFF, (dst_ip>>8)&0xFF, (dst_ip>>16)&0xFF, (dst_ip>>24)&0xFF,
                   seq, ping_reply_ttl, rtt);
        } else {
            printf("Request timeout for icmp_seq=%d\n", seq);
        }
        ping_wait_seq  = -1;
        ping_got_reply = 0;   // so the cadence wait below runs to its deadline

        // ~300 ms cadence between probes (keep draining RX so nothing stalls).
        if (i + 1 < count) ping_wait(get_ticks() + 300);
    }

    int loss = count > 0 ? ((count - received) * 100) / count : 0;
    printf("--- %d.%d.%d.%d ping statistics ---\n",
           dst_ip&0xFF, (dst_ip>>8)&0xFF, (dst_ip>>16)&0xFF, (dst_ip>>24)&0xFF);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           count, received, loss);
    if (received > 0)
        printf("rtt min/avg/max = %d/%d/%d ms\n",
               rtt_min, rtt_sum / received, rtt_max);
    return received;
}
