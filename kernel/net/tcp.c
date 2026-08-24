#include "../core/kernel.h"
#include "../core/spinlock.h"
#include "tcp.h"

extern int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t* data, uint32_t len, int iface_idx);
extern uint32_t get_ticks(void);

// Testing hook: number of upcoming sequence-consuming TX segments to silently
// drop (still armed for retransmit) so the RTO path can be exercised in-guest.
static int g_tcp_drop_tx = 0;
void tcp_debug_drop(int n) { g_tcp_drop_tx = n; }

// RX segments dropped for a bad TCP checksum (issue #61). Exposed for tests/stats.
static uint32_t g_tcp_rx_csum_drop = 0;
uint32_t tcp_rx_csum_drops(void) { return g_tcp_rx_csum_drop; }

// TCP receive-window flow control. The old stack advertised a FIXED 8192-byte window while
// letting the per-connection recv buffer grow without bound (recv_cap = recv_len+payload each
// segment) — a peer that never lets the app read could grow it until the kernel heap was
// exhausted (a remote memory-DoS). TCP_RECV_MAX caps the buffer; the advertised window now
// reflects the ACTUAL free space (min with the 16-bit field), reaching 0 (zero-window) when
// full so a cooperating sender stops, and the RX path drops anything past the cap so it can't
// grow past it regardless. tcp_advertised_window is pure — pinned by tcp_wnd_selftest.
#define TCP_RECV_MAX (256 * 1024)
uint32_t tcp_advertised_window(uint32_t recv_len) {
    if (recv_len >= TCP_RECV_MAX) return 0;
    uint32_t space = TCP_RECV_MAX - recv_len;
    return space > 0xFFFF ? 0xFFFF : space;   // the TCP window field is 16-bit
}

int tcp_wnd_selftest(void) {
    if (tcp_advertised_window(0) != 0xFFFF) return 1;                       // empty -> full 16-bit window
    if (tcp_advertised_window(TCP_RECV_MAX) != 0) return 2;                 // full -> zero-window
    if (tcp_advertised_window(TCP_RECV_MAX + 100) != 0) return 3;          // over-full clamps to 0
    if (tcp_advertised_window(TCP_RECV_MAX - 1000) != 1000) return 4;      // near-full: exact free space
    if (tcp_advertised_window(TCP_RECV_MAX - 0xFFFF) != 0xFFFF) return 5;  // exactly 65535 free
    if (tcp_advertised_window(TCP_RECV_MAX - 0x10000) != 0xFFFF) return 6; // 65536 free clamps to 65535
    return 0;
}

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

// One lock for the whole connection table. Every public entry point (connect/listen/
// accept/send/recv/close/state + the RX handler tcp_handle_packet + the retransmit
// timer tcp_tick) mutates the shared conns[] — slot allocation, sequence state, and the
// per-conn RX/retransmit buffers — so without it two CPUs could hand out the same slot
// or free a buffer another core is still using (issue #58). The public functions wrap a
// _inner() worker with this lock; the inner workers and the internal helpers they call
// (send_segment/find_slot/find_conn_by_tuple/tcp_conn_release) run with it held and must
// never take it again or block on the net poll, so there is no re-entry/self-deadlock.
static spinlock_t tcp_lock = SPINLOCK_INIT;

// --- Read-only introspection for the `netstat` command (kernel.c) ---
// conns[] is a fixed static array whose slots are reused, never freed, so reading a
// slot's scalar fields without tcp_lock cannot use-after-free; a torn read at worst
// shows a momentarily-stale state, which is fine for a diagnostic snapshot (ss-like).
int tcp_conn_count(void) { return TCP_MAX_CONNS; }

int tcp_conn_info(int idx, int* state, uint32_t* src_ip, uint16_t* src_port,
                  uint32_t* dst_ip, uint16_t* dst_port) {
    if (idx < 0 || idx >= TCP_MAX_CONNS || !conns[idx].active) return 0;
    if (state)    *state    = conns[idx].state;
    if (src_ip)   *src_ip   = conns[idx].src_ip;
    if (src_port) *src_port = conns[idx].src_port;
    if (dst_ip)   *dst_ip   = conns[idx].dst_ip;
    if (dst_port) *dst_port = conns[idx].dst_port;
    return 1;
}

const char* tcp_state_name(int state) {
    switch (state) {
        case TCP_STATE_CLOSED:      return "CLOSED";
        case TCP_STATE_SYN_SENT:    return "SYN_SENT";
        case TCP_STATE_SYN_RCVD:    return "SYN_RCVD";
        case TCP_STATE_ESTABLISHED: return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT1:   return "FIN_WAIT1";
        case TCP_STATE_FIN_WAIT2:   return "FIN_WAIT2";
        case TCP_STATE_CLOSE_WAIT:  return "CLOSE_WAIT";
        case TCP_STATE_LAST_ACK:    return "LAST_ACK";
        case TCP_STATE_TIME_WAIT:   return "TIME_WAIT";
        case TCP_STATE_LISTEN:      return "LISTEN";
        default:                    return "?";
    }
}

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

// Internet checksum over the TCP pseudo-header + segment, with the two IPs given
// explicitly (both stored network-order: low byte = first octet). The result is the
// value to place in the checksum field of an outgoing segment; run over a RECEIVED
// segment whose checksum field is already filled in, a valid segment sums to 0.
static uint16_t tcp_csum_over(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t* tcp_seg, uint32_t tcp_len) {
    uint8_t pseudo[12];
    pseudo[0] = src_ip & 0xFF;
    pseudo[1] = (src_ip >> 8) & 0xFF;
    pseudo[2] = (src_ip >> 16) & 0xFF;
    pseudo[3] = (src_ip >> 24) & 0xFF;
    pseudo[4] = dst_ip & 0xFF;
    pseudo[5] = (dst_ip >> 8) & 0xFF;
    pseudo[6] = (dst_ip >> 16) & 0xFF;
    pseudo[7] = (dst_ip >> 24) & 0xFF;
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

// A connection's pseudo-header uses its own src/dst IPs (the TX direction).
static uint16_t tcp_checksum(tcp_conn_t* conn, const uint8_t* tcp_seg, uint32_t tcp_len) {
    return tcp_csum_over(conn->src_ip, conn->dst_ip, tcp_seg, tcp_len);
}

// CI self-test (issue #61): a segment stamped with a valid checksum must validate to
// zero on the RX path, and any single-byte corruption (payload OR the checksum field
// itself) must make it nonzero. Returns 0 on pass, else the number of the failing case.
int tcp_checksum_selftest(void) {
    uint32_t s_ip = 0x0100007F;   // 127.0.0.1, stored low-byte-first (as the stack does)
    uint32_t d_ip = 0x0100007F;
    uint8_t seg[sizeof(tcp_header_t) + 4];
    tcp_header_t* h = (tcp_header_t*)seg;
    h->src_port = htons(1234);
    h->dst_port = htons(80);
    h->seq = htonl(0x11223344u);
    h->ack = htonl(0x55667788u);
    h->offset_flags = htons((uint16_t)((5 << 12) | TCP_FLAG_ACK));
    h->window = htons(0x2000);
    h->checksum = 0;
    h->urgent = 0;
    seg[sizeof(tcp_header_t) + 0] = 'N';
    seg[sizeof(tcp_header_t) + 1] = 'y';
    seg[sizeof(tcp_header_t) + 2] = 'x';
    seg[sizeof(tcp_header_t) + 3] = '!';
    h->checksum = htons(tcp_csum_over(s_ip, d_ip, seg, sizeof(seg)));

    if (tcp_csum_over(s_ip, d_ip, seg, sizeof(seg)) != 0) return 1;   // valid -> must sum to 0
    seg[sizeof(tcp_header_t)] ^= 0x20;                                // corrupt a payload byte
    if (tcp_csum_over(s_ip, d_ip, seg, sizeof(seg)) == 0) return 2;   // must now be nonzero
    seg[sizeof(tcp_header_t)] ^= 0x20;                                // restore the payload
    seg[16] ^= 0xFF;                                                  // corrupt the checksum field
    if (tcp_csum_over(s_ip, d_ip, seg, sizeof(seg)) == 0) return 3;   // must be nonzero
    return 0;
}

// Free every buffered retransmit segment and empty the queue.
static void tcp_rtx_clear_all(tcp_conn_t* conn) {
    for (int i = 0; i < conn->rtx_count; i++) {
        if (conn->rtx[i].seg) kfree(conn->rtx[i].seg);
        conn->rtx[i].seg = NULL;
    }
    conn->rtx_count = 0;
}

static int send_segment(tcp_conn_t* conn, uint8_t flags, const uint8_t* data, uint32_t data_len) {
    uint32_t tcp_len = sizeof(tcp_header_t) + data_len;
    uint8_t* seg = (uint8_t*)kmalloc(tcp_len);
    if (!seg) return -1;

    tcp_header_t* hdr = (tcp_header_t*)seg;
    hdr->src_port = htons(conn->src_port);
    hdr->dst_port = htons(conn->dst_port);
    hdr->seq = htonl(conn->seq);
    hdr->ack = htonl(conn->ack);
    // offset_flags and window are 16-bit fields that must be network byte order
    // on the wire; a plain LE store reversed them (bad data offset/flags, tiny
    // window) so slirp rejected the segment.
    uint16_t of = ((5 << 12) & 0xF000) | (flags & 0x003F);
    hdr->offset_flags = htons(of);
    hdr->window = htons((uint16_t)tcp_advertised_window(conn->recv_len));   // real free space
    hdr->checksum = 0;
    hdr->urgent = 0;

    if (data && data_len > 0)
        memcpy(seg + sizeof(tcp_header_t), data, data_len);

    // tcp_checksum returns the network-order value as a host integer; htons puts
    // the bytes in network order in the header (same as the IP checksum).
    uint16_t ck = tcp_checksum(conn, seg, tcp_len);
    hdr->checksum = htons(ck);

    // Arm retransmission for segments that consume sequence space (SYN / FIN /
    // data): buffer the exact bytes and record the ack that will clear them,
    // computed from conn->seq BEFORE it advances below. Pure ACKs/RST carry no
    // sequence space, so a lost one is harmless and isn't tracked.
    int seq_consuming = (flags & TCP_FLAG_SYN) || (flags & TCP_FLAG_FIN) || data_len > 0;
    if (seq_consuming && conn->rtx_count < TCP_RTX_QUEUE) {
        // Append (don't overwrite): a second write before the first is acked must
        // keep the earlier segment recoverable (issue #60). A full queue means the
        // window is saturated — the segment still goes out below, just untracked.
        tcp_rtx_t* e = &conn->rtx[conn->rtx_count];
        e->seg = (uint8_t*)kmalloc(tcp_len);
        if (e->seg) {
            memcpy(e->seg, seg, tcp_len);
            e->len = tcp_len;
            e->ack_seq = conn->seq
                       + ((flags & TCP_FLAG_SYN) ? 1 : 0)
                       + ((flags & TCP_FLAG_FIN) ? 1 : 0)
                       + data_len;
            e->sent_tick = get_ticks();
            e->rto = TCP_RTO_INITIAL;
            e->retries = 0;
            conn->rtx_count++;
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
static void tcp_tick_inner(void) {
    uint32_t now = get_ticks();
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        tcp_conn_t* conn = &conns[i];
        if (!conn->active || conn->rtx_count == 0) continue;

        // The oldest unacked segment (index 0) governs liveness: once it has been
        // resent too many times the peer is unresponsive, so tear the conn down.
        if (conn->rtx[0].retries >= TCP_MAX_RETRIES) {
            printf("[TCP] conn %d unresponsive after %d retransmits — resetting\n",
                   i, conn->rtx[0].retries);
            tcp_rtx_clear_all(conn);
            if (conn->recv_buf) { kfree(conn->recv_buf); conn->recv_buf = NULL; }
            conn->active = 0;
            conn->state = TCP_STATE_CLOSED;
            continue;
        }

        // Resend every buffered segment whose RTO has elapsed (per-segment
        // exponential backoff). They were sent close together, so usually all fire.
        for (int q = 0; q < conn->rtx_count; q++) {
            tcp_rtx_t* e = &conn->rtx[q];
            if ((int32_t)(now - (e->sent_tick + e->rto)) < 0) continue;
            e->retries++;
            e->rto *= 2;
            if (e->rto > TCP_RTO_MAX) e->rto = TCP_RTO_MAX;
            e->sent_tick = now;
            printf("[TCP] retransmit conn %d seg %d (retry %d, next rto %u ms)\n",
                   i, q, e->retries, e->rto);
            ip_send(conn->dst_ip, 6, e->seg, e->len, -1);
        }
    }
}

void tcp_tick(void) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    tcp_tick_inner();
    spin_unlock_irqrestore(&tcp_lock, fl);
}

int tcp_init(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        conns[i].active = 0;
    return 0;
}

static int tcp_connect_inner(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port) {
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
    conn->rtx_count = 0;

    next_isn += 1000;

    // Send SYN
    send_segment(conn, TCP_FLAG_SYN, NULL, 0);
    return slot;
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = tcp_connect_inner(dst_ip, dst_port, src_port);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

// Passive open: reserve a slot that accepts inbound connections on `port`. It
// stays in LISTEN; each inbound SYN spawns a separate child connection that
// tcp_accept() hands out. dst_port==0 marks the slot as "unbound remote".
static int tcp_listen_inner(uint16_t port) {
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
    conn->rtx_count = 0;
    return slot;
}

int tcp_listen(uint16_t port) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = tcp_listen_inner(port);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

// Return the id of a child of `listen_id` that has completed its handshake and
// hasn't been accepted yet, else -1. Children share the listener's local port.
static int tcp_accept_inner(int listen_id) {
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

int tcp_accept(int listen_id) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = tcp_accept_inner(listen_id);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

static int tcp_send_inner(int conn_id, const uint8_t* data, uint32_t len) {
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

int tcp_send(int conn_id, const uint8_t* data, uint32_t len) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = tcp_send_inner(conn_id, data, len);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

static int tcp_recv_inner(int conn_id, uint8_t* buf, uint32_t max_len) {
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

int tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = tcp_recv_inner(conn_id, buf, max_len);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

int tcp_state(int conn_id) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = (conn_id < 0 || conn_id >= TCP_MAX_CONNS) ? -1
            : (conns[conn_id].active ? conns[conn_id].state : TCP_STATE_CLOSED);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

// 1 if tcp_recv() would return immediately (buffered data, or EOF because the
// connection is closed) — the readiness check behind poll()/select(). A live
// connection with no buffered data is "not ready" (a read would block).
int tcp_recv_ready(int conn_id) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r;
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) {
        r = 1;
    } else {
        tcp_conn_t* c = &conns[conn_id];
        r = !c->active ? 1                          // dead -> a read errors: treat as ready
                       : (c->recv_len > 0 || c->state == TCP_STATE_CLOSED);
    }
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

// Release a connection: free its buffers and return the slot to the pool. Used once
// the close handshake reaches CLOSED (or a state with no graceful close is torn down).
static void tcp_conn_release(tcp_conn_t* conn) {
    tcp_rtx_clear_all(conn);
    if (conn->recv_buf) { kfree(conn->recv_buf); conn->recv_buf = NULL; conn->recv_len = 0; conn->recv_cap = 0; }
    conn->active = 0;
    conn->state = TCP_STATE_CLOSED;
}

static int tcp_close_inner(int conn_id) {
    if (conn_id < 0 || conn_id >= TCP_MAX_CONNS) return -1;
    tcp_conn_t* conn = &conns[conn_id];
    if (!conn->active) return -1;
    if (conn->state == TCP_STATE_ESTABLISHED) {
        // Active close: send our FIN and KEEP the conn alive (with its retransmit buffer
        // armed) so a lost FIN is resent by tcp_tick and the peer's ACK + FIN still match
        // this conn. The state machine in tcp_handle_packet drives it to CLOSED. The old
        // code marked the conn inactive and freed rt_seg right here, so the handshake
        // could never complete and a lost FIN was unrecoverable (issue #59).
        conn->state = TCP_STATE_FIN_WAIT1;
        send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        return 0;
    }
    if (conn->state == TCP_STATE_CLOSE_WAIT) {
        // Passive close: the peer already FIN'd (we are in CLOSE_WAIT); our FIN moves us
        // to LAST_ACK and the peer's ACK of it completes the close.
        conn->state = TCP_STATE_LAST_ACK;
        send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        return 0;
    }
    // No graceful close is possible from other states (e.g. SYN_SENT): drop it outright.
    tcp_conn_release(conn);
    return 0;
}

int tcp_close(int conn_id) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    int r = tcp_close_inner(conn_id);
    spin_unlock_irqrestore(&tcp_lock, fl);
    return r;
}

static void tcp_handle_packet_inner(uint8_t* packet, uint32_t len, uint32_t src_ip, uint32_t dst_ip) {
    if (len < sizeof(tcp_header_t)) return;
    // Verify the TCP checksum over the pseudo-header + full segment BEFORE reading any
    // field. Passing the IP checksum only proves the IP header is intact; a segment
    // corrupted in flight could still flip flags/seq/ack or the payload and so ACK
    // outstanding data, inject bytes, or tear a connection down. A segment carrying
    // the sender's correct checksum sums to zero here; anything else is dropped (#61).
    if (tcp_csum_over(src_ip, dst_ip, packet, len) != 0) { g_tcp_rx_csum_drop++; return; }
    tcp_header_t* hdr = (tcp_header_t*)packet;

    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t src_port = ntohs(hdr->src_port);
    uint32_t seq = ntohl(hdr->seq);
    uint32_t ackno = ntohl(hdr->ack);
    uint16_t off_flags = ntohs(hdr->offset_flags);
    uint8_t flags = off_flags & 0x003F;
    uint8_t data_offset = (off_flags >> 12) & 0x0F;
    uint32_t header_len = data_offset * 4;
    // A valid TCP data offset is >= 5 (a 20-byte header). Reject a short one as well as one
    // past the segment: header_len < 20 makes `payload` overlap the header and payload_len
    // over-count, so header bytes would be delivered as data and ack would advance past bytes
    // never received. The IP layer already lower-bounds IHL the same way (ip.c). A segment
    // with a valid checksum can still carry data_offset < 5, so this is not redundant.
    if (header_len < sizeof(tcp_header_t) || header_len > len) return;
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
                ch->rtx_count = 0;
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
        rhdr->src_port = htons(dst_port);
        rhdr->dst_port = htons(src_port);
        rhdr->seq = 0;
        rhdr->ack = htonl(temp.ack);
        // offset_flags is a 16-bit field that must be network byte order on the
        // wire (like send_segment does). A plain LE store reversed the bytes, so
        // 0x5014 (hdrlen 5, RST|ACK) went out as 0x1450 — data offset 1 and, worse,
        // the RST bit landed outside the flags byte so the peer saw a bare ACK.
        uint16_t rof = (uint16_t)((5 << 12) | (TCP_FLAG_RST | TCP_FLAG_ACK));
        rhdr->offset_flags = htons(rof);
        rhdr->window = 0;
        rhdr->checksum = 0;
        rhdr->urgent = 0;
        // tcp_checksum returns the network-order value as a host integer, so it
        // must be stored byte-swapped (same as send_segment) — a plain store put
        // the checksum bytes on the wire reversed, so peers/slirp dropped the RST.
        uint16_t rck = tcp_checksum(&temp, seg, sizeof(tcp_header_t));
        rhdr->checksum = htons(rck);
        ip_send(src_ip, 6, seg, sizeof(tcp_header_t), -1);
        return;
    }

    // Cumulative ACK: once the peer acks past our outstanding (buffered) segment,
    // it's delivered — stop the retransmit timer for it. This also finally
    // consumes a bare ACK of our data, which the state machine below ignores.
    if ((flags & TCP_FLAG_ACK) && conn->rtx_count > 0) {
        // Cumulative ACK clears a prefix of the queue: segments are buffered in
        // ascending seq order so their ack_seq values increase monotonically —
        // everything acked is a run from the oldest. Pop those and shift down.
        int cleared = 0;
        while (cleared < conn->rtx_count &&
               (int32_t)(ackno - conn->rtx[cleared].ack_seq) >= 0) {
            kfree(conn->rtx[cleared].seg);
            cleared++;
        }
        if (cleared > 0) {
            for (int j = cleared; j < conn->rtx_count; j++)
                conn->rtx[j - cleared] = conn->rtx[j];
            conn->rtx_count -= cleared;
        }
    }

    // Close-handshake progression driven by the peer's ACK of OUR FIN. send_segment()
    // advanced conn->seq past the FIN, so ackno >= conn->seq means everything including
    // our FIN is acknowledged. This is what completes the close tcp_close() now defers
    // instead of aborting (issue #59); an idle ESTABLISHED conn hits this with an equal
    // ack but is ignored by the state guards.
    if ((flags & TCP_FLAG_ACK) && (int32_t)(ackno - conn->seq) >= 0) {
        if (conn->state == TCP_STATE_LAST_ACK) {       // passive close acknowledged -> done
            tcp_conn_release(conn);
            return;
        }
        if (conn->state == TCP_STATE_FIN_WAIT1)        // our FIN acked; await the peer's FIN
            conn->state = TCP_STATE_FIN_WAIT2;
    }

    // Passive-open handshake completes when the client's ACK of our SYN-ACK
    // arrives (it may also carry the first data byte, handled just below).
    if ((flags & TCP_FLAG_ACK) && conn->state == TCP_STATE_SYN_RCVD) {
        conn->state = TCP_STATE_ESTABLISHED;
    }

    // Update ack from the received segment. `data_accepted` records whether the
    // payload was actually stored, so a FIN piggybacked on the same segment (below)
    // is not acked past data we had to drop under memory pressure.
    int data_accepted = 0;
    if (payload_len > 0 && (flags & TCP_FLAG_ACK)) {
        // In-order gate: accept data only if it starts exactly at conn->ack (the next
        // byte we expect). Before this the payload was appended blindly, so a retransmitted
        // duplicate (the peer resent because our ACK was lost — seq before ack) had its
        // bytes stored a SECOND time, and an out-of-order segment (a gap — seq past ack)
        // was stored as if contiguous and jumped ack over data we never received. We do
        // not buffer out-of-order data, so drop the payload and re-send an ACK of our true
        // position, which tells the peer to resend from there. (Signed compare tolerates
        // seq wraparound, like the cumulative-ACK check above.) A FIN in the same segment
        // sequences past this data, so recv_drop skips it too via the data_accepted guard.
        if ((int32_t)(seq - conn->ack) != 0) {
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
            goto recv_drop;
        }
        // Received data — store it (the recv buffer grows on demand). On an allocation FAILURE we
        // drop the segment: don't advance recv_len/ack and don't ACK, so the peer retransmits later.
        // (The old code dereferenced a NULL recv_buf / new_buf right below and crashed the kernel
        // under memory pressure — a remote peer sending data while the heap was exhausted.)
        // Flow control cap: never buffer past TCP_RECV_MAX. Drop the segment WITHOUT ACKing its
        // data (don't advance recv_len/ack) so the peer retransmits once the app drains and space
        // frees up; the advertised window (0 when full) already tells a cooperating peer to pause.
        // This is what bounds the recv buffer against a remote memory-DoS.
        if (conn->recv_len + payload_len > TCP_RECV_MAX) goto recv_drop;
        if (conn->recv_buf == NULL) {
            conn->recv_cap = payload_len > 4096 ? payload_len : 4096;
            conn->recv_buf = (uint8_t*)kmalloc(conn->recv_cap);
            conn->recv_len = 0;
            if (!conn->recv_buf) { conn->recv_cap = 0; goto recv_drop; }
        }
        if (conn->recv_len + payload_len > conn->recv_cap) {
            uint32_t new_cap = conn->recv_len + payload_len;
            uint8_t* new_buf = (uint8_t*)kmalloc(new_cap);
            if (!new_buf) goto recv_drop;                    // keep the existing buffer + its data
            if (conn->recv_len > 0) memcpy(new_buf, conn->recv_buf, conn->recv_len);
            if (conn->recv_buf) kfree(conn->recv_buf);
            conn->recv_buf = new_buf;
            conn->recv_cap = new_cap;
        }
        conn->ack = seq + payload_len;                       // advance ack only now that we can store it
        memcpy(conn->recv_buf + conn->recv_len, payload, payload_len);
        conn->recv_len += payload_len;
        data_accepted = 1;
        send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        recv_drop: ;
    } else if (flags & TCP_FLAG_SYN && flags & TCP_FLAG_ACK) {
        if (conn->state == TCP_STATE_SYN_SENT) {
            conn->ack = seq + 1;
            conn->state = TCP_STATE_ESTABLISHED;
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        }
    }

    // A FIN can arrive on its own OR piggybacked on the peer's final data segment
    // (its last bytes and FIN in one segment — common when a server closes right
    // after responding). This used to be an `else if` after the data branch, so a
    // combined data+FIN segment had its data stored but its FIN ignored: we stayed
    // ESTABLISHED and never acked the FIN, recovering only once the peer retransmitted
    // a bare FIN. Handle it after the data path instead. Skip it when the segment
    // carried data we could not store (the OOM drop above): the FIN sequences just
    // past that data, so acking it would falsely ack bytes we dropped — leave the
    // whole segment for the peer to resend.
    if ((flags & TCP_FLAG_FIN) && !(payload_len > 0 && !data_accepted)) {
        uint32_t fin_seq = seq + payload_len;   // the FIN occupies the seq just past any data
        if (conn->state == TCP_STATE_ESTABLISHED) {
            conn->ack = fin_seq + 1;
            conn->state = TCP_STATE_CLOSE_WAIT;
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
        } else if (conn->state == TCP_STATE_FIN_WAIT1 || conn->state == TCP_STATE_FIN_WAIT2) {
            // The peer's FIN closes our active close (its ACK of our FIN either arrived
            // just above, moving us to FIN_WAIT2, or rides this same segment). Ack it and
            // finish — this stack collapses TIME_WAIT straight to CLOSED (no 2 MSL linger),
            // freeing the slot instead of leaving the conn stranded (issue #59).
            conn->ack = fin_seq + 1;
            send_segment(conn, TCP_FLAG_ACK, NULL, 0);
            tcp_conn_release(conn);
        }
    }
}

void tcp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip, uint32_t dst_ip) {
    uint64_t fl = spin_lock_irqsave(&tcp_lock);
    tcp_handle_packet_inner(packet, len, src_ip, dst_ip);
    spin_unlock_irqrestore(&tcp_lock, fl);
}
