#include "kernel.h"
#include "tcp.h"

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint32_t len, int iface_idx);
extern uint32_t get_ticks(void);

// Testing hook: number of upcoming sequence-consuming TX segments to silently
// drop (still armed for retransmit) so the RTO path can be exercised in-guest.
static int g_tcp_drop_tx = 0;
void tcp_debug_drop(int n) { g_tcp_drop_tx = n; }

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint16_t offset_flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_header_t;

typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_len;
} tcp_pseudo_t;

static tcp_conn_t conns[TCP_MAX_CONNS];
static uint32_t next_isn = 1000;

static int find_slot(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        if (!conns[i].active) return i;
    return -1;
}

static tcp_conn_t* find_conn_by_tuple(uint32_t src_ip, uint32_t dst_ip,
                                       uint16_t src_port, uint16_t dst_port) {
    (void)src_ip;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!conns[i].active) continue;
        if (conns[i].state == TCP_STATE_LISTEN) continue;   // listeners match by port only
        if (conns[i].dst_ip == dst_ip && conns[i].src_port == src_port
            && conns[i].dst_port == dst_port)
            return &conns[i];
    }
    return NULL;
}

// A passive-open socket waiting for connections on `port` (dst unbound).
static tcp_conn_t* find_listener(uint16_t port) {
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        if (conns[i].active && conns[i].state == TCP_STATE_LISTEN
            && conns[i].src_port == port)
            return &conns[i];
    return NULL;
}

static uint16_t tcp_checksum(tcp_conn_t* conn, const uint8_t* tcp_seg, uint32_t tcp_len) {
    // IPs are stored network-order (low byte = first octet), so the pseudo-header
    // bytes are the uint32 bytes in ascending order — must match the IP header on
    // the wire or the checksum is invalid.
    uint8_t pseudo[12];
    pseudo[0] = conn->src_ip & 0xFF;
    pseudo[1] = (conn->src_ip >> 8) & 0xFF;
    pseudo[2] = (conn->src_ip >> 16) & 0xFF;
    pseudo[3] = (conn->src_ip >> 24) & 0xFF;
    pseudo[4] = conn->dst_ip & 0xFF;
    pseudo[5] = (conn->dst_ip >> 8) & 0xFF;
    pseudo[6] = (conn->dst_ip >> 16) & 0xFF;
    pseudo[7] = (conn->dst_ip >> 24) & 0xFF;
    pseudo[8] = 0;
    pseudo[9] = 6;
    pseudo[10] = (tcp_len >> 8) & 0xFF;
    pseudo[11] = tcp_len & 0xFF;

    uint32_t sum = 0;
    for (int i = 0; i < 6; i++) {
        uint16_t w = ((uint16_t)pseudo[i*2] << 8) | pseudo[i*2 + 1];
        sum += w;
        if (sum & 0xFFFF0000) sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint32_t words = (tcp_len + 1) / 2;
    for (uint32_t i = 0; i < words; i++) {
        uint16_t w;
        if (i * 2 + 1 < tcp_len)
            w = ((uint16_t)tcp_seg[i*2] << 8) | tcp_seg[i*2 + 1];
        else
            w = (uint16_t)tcp_seg[i*2] << 8;
        sum += w;
        if (sum & 0xFFFF0000) sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return ~(sum & 0xFFFF);
}

static int send_segment(tcp_conn_t* conn, uint8_t flags, const uint8_t* data, uint32_t data_len) {
    uint32_t tcp_len = sizeof(tcp_header_t) + data_len;
    uint8_t* seg = (uint8_t*)kmalloc(tcp_len);
    if (!seg) return -1;

    tcp_header_t* hdr = (tcp_header_t*)seg;
    hdr->src_port = ((conn->src_port << 8) & 0xFF00) | ((conn->src_port >> 8) & 0x00FF);
    hdr->dst_port = ((conn->dst_port << 8) & 0xFF00) | ((conn->dst_port >> 8) & 0x00FF);
    hdr->seq = ((conn->seq << 24) & 0xFF000000) | ((conn->seq << 8) & 0x00FF0000)
             | ((conn->seq >> 8) & 0x0000FF00) | ((conn->seq >> 24) & 0x000000FF);
    hdr->ack = ((conn->ack << 24) & 0xFF000000) | ((conn->ack << 8) & 0x00FF0000)
             | ((conn->ack >> 8) & 0x0000FF00) | ((conn->ack >> 24) & 0x000000FF);
    // offset_flags and window are 16-bit fields that must be in network byte
    // order on the wire; a plain LE store reversed them (bad data offset/flags,
    // tiny window) so slirp rejected the segment.
    uint16_t of = ((5 << 12) & 0xF000) | (flags & 0x003F);
    hdr->offset_flags = (uint16_t)((of >> 8) | (of << 8));
    hdr->window = (uint16_t)((0x2000 >> 8) | (0x2000 << 8));
    hdr->checksum = 0;
    hdr->urgent = 0;

    if (data && data_len > 0)
        memcpy(seg + sizeof(tcp_header_t), data, data_len);

    // tcp_checksum returns the network-order value as a host integer; store it
    // byte-swapped so the header bytes are network order (same as the IP cksum).
    uint16_t ck = tcp_checksum(conn, seg, tcp_len);
    hdr->checksum = (uint16_t)((ck >> 8) | (ck << 8));

    // Arm retransmission for segments that consume sequence space (SYN / FIN /
    // data): buffer the exact bytes and record the ack that will clear them,
    // computed from conn->seq BEFORE it advances below. Pure ACKs/RST carry no
    // sequence space, so a lost one is harmless and isn't tracked.
    int seq_consuming = (flags & TCP_FLAG_SYN) || (flags & TCP_FLAG_FIN) || data_len > 0;
    if (seq_consuming) {
        if (conn->rt_seg) kfree(conn->rt_seg);
        conn->rt_seg = (uint8_t*)kmalloc(tcp_len);
        if (conn->rt_seg) {
            memcpy(conn->rt_seg, seg, tcp_len);
            conn->rt_len = tcp_len;
            conn->rt_ack_seq = conn->seq
                             + ((flags & TCP_FLAG_SYN) ? 1 : 0)
                             + ((flags & TCP_FLAG_FIN) ? 1 : 0)
                             + data_len;
            conn->rt_sent_tick = get_ticks();
            conn->rt_rto = TCP_RTO_INITIAL;
            conn->rt_retries = 0;
        }
    }

    // Testing hook: silently drop this transmission (it stays armed above, so the
    // RTO timer has to recover it) — this is how retransmission is exercised.
    int result;
    if (g_tcp_drop_tx > 0 && seq_consuming) {
        g_tcp_drop_tx--;
        printf("[TCP] TX dropped (test): seq=%u len=%u — awaiting retransmit\n",
               conn->seq, data_len);
        result = (int)tcp_len;
    } else {
        result = ip_send(conn->dst_ip, 6, seg, tcp_len, -1);
    }

    if (flags & TCP_FLAG_SYN) conn->sent_unacked = conn->seq + 1;
    else if (data_len > 0) conn->sent_unacked = conn->seq + data_len;

    if (!(flags & TCP_FLAG_RST)) {
        if (data_len > 0) conn->seq += data_len;
        if (flags & TCP_FLAG_SYN) conn->seq++;
        if (flags & TCP_FLAG_FIN) conn->seq++;
    }

    kfree(seg);
    return result;
}

// Resend the buffered segment when its ACK hasn't arrived within the RTO, with
// exponential backoff; after TCP_MAX_RETRIES give up and tear the conn down.
// Called from the network poll loop (never an IRQ — it does ip_send/kmalloc).
void tcp_tick(void) {
    uint32_t now = get_ticks();
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        tcp_conn_t* conn = &conns[i];
        if (!conn->active || !conn->rt_seg) continue;
        if ((int32_t)(now - (conn->rt_sent_tick + conn->rt_rto)) < 0) continue;

        if (conn->rt_retries >= TCP_MAX_RETRIES) {
            printf("[TCP] conn %d unresponsive after %d retransmits — resetting\n",
                   i, conn->rt_retries);
            kfree(conn->rt_seg); conn->rt_seg = NULL; conn->rt_len = 0;
            if (conn->recv_buf) { kfree(conn->recv_buf); conn->recv_buf = NULL; }
            conn->active = 0;
            conn->state = TCP_STATE_CLOSED;
            continue;
        }

        conn->rt_retries++;
        conn->rt_rto *= 2;
        if (conn->rt_rto > TCP_RTO_MAX) conn->rt_rto = TCP_RTO_MAX;
        conn->rt_sent_tick = now;
        printf("[TCP] retransmit conn %d (retry %d, next rto %u ms)\n",
               i, conn->rt_retries, conn->rt_rto);
        ip_send(conn->dst_ip, 6, conn->rt_seg, conn->rt_len, -1);
    }
}

int tcp_init(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        conns[i].active = 0;
    return 0;
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port) {
    int slot = find_slot();
    if (slot < 0) return -1;

    // conn->src_ip feeds both the IP header and the TCP pseudo-header checksum, so
    // it must match what actually goes on the wire. A loopback destination sources
    // from lo (127.0.0.1) and needs no NIC; anything else uses the first real NIC.
    uint32_t src_ip;
    if ((dst_ip & 0xFF) == 0x7F) {
        src_ip = 0x0100007F;
    } else {
        int iface_idx = -1;
        for (int i = 0; i < 8; i++) {
            if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
                iface_idx = i; break;
            }
        }
        if (iface_idx < 0) return -1;
        src_ip = net_interfaces[iface_idx].ip;
    }

    tcp_conn_t* conn = &conns[slot];
    conn->active = 1;
    conn->state = TCP_STATE_SYN_SENT;
    conn->accepted = 0;
    conn->src_ip = src_ip;
    conn->dst_ip = dst_ip;
    conn->src_port = src_port;
    conn->dst_port = dst_port;
    conn->seq = next_isn;
    conn->ack = 0;
    conn->recv_buf = NULL;
    conn->recv_len = 0;
    conn->recv_cap = 0;
    conn->sent_unacked = 0;
    conn->rt_seg = NULL;
    conn->rt_len = 0;
    conn->rt_retries = 0;

    next_isn += 1000;

    // Send SYN
    send_segment(conn, TCP_FLAG_SYN, NULL, 0);
    return slot;
}

// Passive open: reserve a slot that accepts inbound connections on `port`. It
// stays in LISTEN; each inbound SYN spawns a separate child connection that
// tcp_accept() hands out. dst_port==0 marks the slot as "unbound remote".
int tcp_listen(uint16_t port) {
    int slot = find_slot();
    if (slot < 0) return -1;
    tcp_conn_t* conn = &conns[slot];
    conn->active = 1;
    conn->state = TCP_STATE_LISTEN;
    conn->accepted = 0;
    conn->src_ip = 0;
    conn->dst_ip = 0;
    conn->src_port = port;
    conn->dst_port = 0;
    conn->seq = 0;
    conn->ack = 0;
    conn->recv_buf = NULL;
    conn->recv_len = 0;
    conn->recv_cap = 0;
    conn->rt_seg = NULL;
    conn->rt_len = 0;
    conn->rt_retries = 0;
    return slot;
}

// Return the id of a child of `listen_id` that has completed its handshake and
// hasn't been accepted yet, else -1. Children share the listener's local port.
int tcp_accept(int listen_id) {
    if (listen_id < 0 || listen_id >= TCP_MAX_CONNS) return -1;
    tcp_conn_t* l = &conns[listen_id];
    if (!l->active || l->state != TCP_STATE_LISTEN) return -1;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (i == listen_id) continue;
        tcp_conn_t* c = &conns[i];
        if (c->active && !c->accepted && c->src_port == l->src_port
            && (c->state == TCP_STATE_ESTABLISHED || c->state == TCP_STATE_CLOSE_WAIT)) {
            c->accepted = 1;
            return i;
        }
    }
    return -1;
}

int tcp_send(int conn_id, const uint8_t* data, uint32_t len) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return -1;
    tcp_conn_t* conn = &conns[conn_id];
    // CLOSE_WAIT is a valid send state: the peer has finished sending (its FIN
    // arrived) but our half is still open — the classic server half-close, where
    // a client FINs right after its request yet still awaits the response.
    if (!conn->active ||
        (conn->state != TCP_STATE_ESTABLISHED && conn->state != TCP_STATE_CLOSE_WAIT))
        return -1;
    return send_segment(conn, TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
}

int tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return -1;
    tcp_conn_t* conn = &conns[conn_id];
    if (!conn->active) return -1;
    if (conn->recv_len == 0) return 0;
    uint32_t to_copy = conn->recv_len < max_len ? conn->recv_len : max_len;
    memcpy(buf, conn->recv_buf, to_copy);
    if (to_copy < conn->recv_len)
        memmove(conn->recv_buf, conn->recv_buf + to_copy, conn->recv_len - to_copy);
    conn->recv_len -= to_copy;
    if (conn->recv_len == 0) {
        kfree(conn->recv_buf);
        conn->recv_buf = NULL;
        conn->recv_cap = 0;
    }
    return (int)to_copy;
}

int tcp_state(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return -1;
    return conns[conn_id].active ? conns[conn_id].state : TCP_STATE_CLOSED;
}

int tcp_close(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return -1;
    tcp_conn_t* conn = &conns[conn_id];
    if (!conn->active) return -1;
    if (conn->state == TCP_STATE_ESTABLISHED) {
        conn->state = TCP_STATE_FIN_WAIT1;
        send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    } else if (conn->state == TCP_STATE_CLOSE_WAIT) {
        // We already got the peer's FIN; sending ours completes the close.
        conn->state = TCP_STATE_LAST_ACK;
        send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    }
    if (conn->recv_buf) { kfree(conn->recv_buf); conn->recv_buf = NULL; }
    if (conn->rt_seg) { kfree(conn->rt_seg); conn->rt_seg = NULL; conn->rt_len = 0; }
    conn->active = 0;
    conn->state = TCP_STATE_CLOSED;
    return 0;
}

void tcp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip, uint32_t dst_ip) {
    if (len < sizeof(tcp_header_t)) return;
    tcp_header_t* hdr = (tcp_header_t*)packet;

    uint16_t dst_port = ((hdr->dst_port << 8) & 0xFF00) | ((hdr->dst_port >> 8) & 0x00FF);
    uint16_t src_port = ((hdr->src_port << 8) & 0xFF00) | ((hdr->src_port >> 8) & 0x00FF);
    uint32_t seq = ((hdr->seq << 24) & 0xFF000000) | ((hdr->seq << 8) & 0x00FF0000)
                 | ((hdr->seq >> 8) & 0x0000FF00) | ((hdr->seq >> 24) & 0x000000FF);
    uint32_t ackno = ((hdr->ack << 24) & 0xFF000000) | ((hdr->ack << 8) & 0x00FF0000)
                   | ((hdr->ack >> 8) & 0x0000FF00) | ((hdr->ack >> 24) & 0x000000FF);
    uint16_t off_flags = (uint16_t)((hdr->offset_flags >> 8) | (hdr->offset_flags << 8));
    uint8_t flags = off_flags & 0x003F;
    uint8_t data_offset = (off_flags >> 12) & 0x0F;
    uint32_t header_len = data_offset * 4;
    if (header_len > len) return;
    uint8_t* payload = packet + header_len;
    uint32_t payload_len = len - header_len;
    // The address the peer targeted (our NIC IP, or 127.0.0.1 for loopback) is the
    // correct local address for a reply/child — sourcing a server child from it is
    // what makes the TCP pseudo-header checksum match on the wire (before v5.7.20
    // this defaulted to lo, so NIC-side listen produced bad checksums).
    uint32_t local_ip = dst_ip;

    tcp_conn_t* conn = find_conn_by_tuple(local_ip, src_ip, dst_port, src_port);

    if (!conn) {
        // Never answer an RST with an RST — over loopback that would ping-pong
        // forever (each RST re-enters with no matching conn).
        if (flags & TCP_FLAG_RST) return;

        // Passive open: a pure SYN to a listening port spawns a child connection
        // (the listener itself stays in LISTEN for further clients).
        if ((flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
            tcp_conn_t* l = find_listener(dst_port);
            int slot = l ? find_slot() : -1;
            if (l && slot >= 0) {
                tcp_conn_t* ch = &conns[slot];
                ch->active = 1;
                ch->state = TCP_STATE_SYN_RCVD;
                ch->accepted = 0;
                ch->src_ip = local_ip;
                ch->dst_ip = src_ip;
                ch->src_port = dst_port;    // our listen port
                ch->dst_port = src_port;    // the client's port
                ch->seq = next_isn; next_isn += 1000;
                ch->ack = seq + 1;          // acknowledge the client's SYN
                ch->recv_buf = NULL; ch->recv_len = 0; ch->recv_cap = 0;
                ch->rt_seg = NULL; ch->rt_len = 0; ch->rt_retries = 0;
                ch->sent_unacked = 0;
                send_segment(ch, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                return;
            }
        }

        // No matching connection: send RST
        tcp_conn_t temp;
        temp.src_ip = local_ip;
        temp.dst_ip = src_ip;
        temp.src_port = dst_port;
        temp.dst_port = src_port;
        temp.seq = 0;
        temp.ack = seq + 1;
        // Build a minimal RST segment
        uint8_t seg[sizeof(tcp_header_t)];
        tcp_header_t* rhdr = (tcp_header_t*)seg;
        rhdr->src_port = ((dst_port << 8) & 0xFF00) | ((dst_port >> 8) & 0x00FF);
        rhdr->dst_port = ((src_port << 8) & 0xFF00) | ((src_port >> 8) & 0x00FF);
        rhdr->seq = 0;
        rhdr->ack = ((temp.ack << 24) & 0xFF000000) | ((temp.ack << 8) & 0x00FF0000)
                   | ((temp.ack >> 8) & 0x0000FF00) | ((temp.ack >> 24) & 0x000000FF);
        // offset_flags is a 16-bit field that must be network byte order on the
        // wire (like send_segment does). A plain LE store reversed the bytes, so
        // 0x5014 (hdrlen 5, RST|ACK) went out as 0x1450 — data offset 1 and, worse,
        // the RST bit landed outside the flags byte so the peer saw a bare ACK.
        uint16_t rof = (uint16_t)((5 << 12) | (TCP_FLAG_RST | TCP_FLAG_ACK));
        rhdr->offset_flags = (uint16_t)((rof >> 8) | (rof << 8));
        rhdr->window = 0;
        rhdr->checksum = 0;
        rhdr->urgent = 0;
        // tcp_checksum returns the network-order value as a host integer, so it
        // must be stored byte-swapped (same as send_segment) — a plain store put
        // the checksum bytes on the wire reversed, so peers/slirp dropped the RST.
        uint16_t rck = tcp_checksum(&temp, seg, sizeof(tcp_header_t));
        rhdr->checksum = (uint16_t)((rck >> 8) | (rck << 8));
        ip_send(src_ip, 6, seg, sizeof(tcp_header_t), -1);
        return;
    }

    // Cumulative ACK: once the peer acks past our outstanding (buffered) segment,
    // it's delivered — stop the retransmit timer for it. This also finally
    // consumes a bare ACK of our data, which the state machine below ignores.
    if ((flags & TCP_FLAG_ACK) && conn->rt_seg &&
        (int32_t)(ackno - conn->rt_ack_seq) >= 0) {
        kfree(conn->rt_seg);
        conn->rt_seg = NULL;
        conn->rt_len = 0;
    }

    // Passive-open handshake completes when the client's ACK of our SYN-ACK
    // arrives (it may also carry the first data byte, handled just below).
    if ((flags & TCP_FLAG_ACK) && conn->state == TCP_STATE_SYN_RCVD) {
        conn->state = TCP_STATE_ESTABLISHED;
    }

    // Update ack from received segment
    if (payload_len > 0 && (flags & TCP_FLAG_ACK)) {
        // Received data - store it
        conn->ack = seq + payload_len;
        if (conn->recv_buf == NULL) {
            conn->recv_cap = payload_len > 4096 ? payload_len : 4096;
            conn->recv_buf = (uint8_t*)kmalloc(conn->recv_cap);
            conn->recv_len = 0;
        }
        if (conn->recv_len + payload_len > conn->recv_cap) {
            conn->recv_cap = conn->recv_len + payload_len;
            uint8_t* new_buf = (uint8_t*)kmalloc(conn->recv_cap);
            if (conn->recv_len > 0) memcpy(new_buf, conn->recv_buf, conn->recv_len);
            if (conn->recv_buf) kfree(conn->recv_buf);
            conn->recv_buf = new_buf;
        }
        memcpy(conn->recv_buf + conn->recv_len, payload, payload_len);
        conn->recv_len += payload_len;
        send_segment(conn, TCP_FLAG_ACK, NULL, 0);
    } else if (flags & TCP_FLAG_SYN && flags & TCP_FLAG_ACK) {
        if (conn->state == TCP_STATE_SYN_SENT) {
            conn->ack = seq + 1;
            conn->state = TCP_STATE_ESTABLISHED;
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        }
    } else if (flags & TCP_FLAG_FIN) {
        if (conn->state == TCP_STATE_ESTABLISHED) {
            conn->ack = seq + 1;
            conn->state = TCP_STATE_CLOSE_WAIT;
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        } else if (conn->state == TCP_STATE_FIN_WAIT1) {
            conn->ack = seq + 1;
            conn->state = TCP_STATE_TIME_WAIT;
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        }
    }
}
