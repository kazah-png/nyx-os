#include "kernel.h"
#include "tcp.h"

extern net_iface_t net_interfaces[8];

// Userspace sockets. A socket is a thin handle over the tcp.c/udp.c stack; the
// process fd table (syscall.c) stores UFD_SOCK_MAKE(id) and routes read/write/
// close (TCP) or sendto/recvfrom (UDP) here. SOCK_STREAM = TCP, SOCK_DGRAM = UDP.
#define MAX_SOCKETS 32
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

// A UDP socket buffers received datagrams (with their source) in a small ring;
// inbound datagrams arrive via udp_handle_packet -> nsock_udp_deliver, and
// recvfrom dequeues. The ring is kmalloc'd per DGRAM socket so the nsock table
// stays small (a datagram entry is ~1.5 KB).
#define UDP_DGRAM_MAX 1472       // max payload (Ethernet MTU - IP(20) - UDP(8))
#define UDP_QUEUE     4          // datagrams buffered per socket
typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t len;
    uint8_t  data[UDP_DGRAM_MAX];
} dgram_t;

typedef struct {
    int in_use;
    int type;            // SOCK_STREAM (TCP) or SOCK_DGRAM (UDP)
    int tcp_conn;        // TCP: connected/accepted conn id, LISTEN conn id, or -1
    uint16_t local_port; // bind()'s port (or an ephemeral auto-bind on first sendto)
    int listening;       // TCP: 1 once listen() put it into passive-open
    dgram_t* dq;         // UDP: receive ring (UDP_QUEUE entries), else NULL
    int dq_head, dq_tail;
} nsock_t;

static nsock_t nsocks[MAX_SOCKETS];

extern int rtl8139_init(void);

extern void arp_init(void);

void init_net(void) {
    memset_asm(nsocks, 0, sizeof(nsocks));
    memset_asm(net_interfaces, 0, sizeof(net_interfaces));

    net_iface_t* lo = &net_interfaces[0];
    strcpy(lo->name, "lo");
    lo->ip = 0x0100007F;        // 127.0.0.1 in network order (first octet = low byte)
    lo->netmask = 0x000000FF;   // 255.0.0.0
    lo->mtu = 65536;
    lo->flags = 1;
    printf("[NET] Loopback: 127.0.0.1\n");

    arp_init();

    int ret = rtl8139_init();
    if (ret == 0) printf("[NET] RTL8139 NIC initialized\n");
    else printf("[NET] No RTL8139 NIC found (will retry on kernel_poll)\n");

    printf("[NET] Stack ready\n");
}

void kernel_poll_net(void) {
    extern void eth_poll(int iface_idx);
    extern void ip_loopback_poll(void);
    extern void tcp_tick(void);
    // Not re-entrancy-guarded: socket programs stay SINGLE-PROCESS. The mid-syscall
    // resume-CR3 crash that used to kill a busy-polling syscall is fixed (see
    // irq_scheduler_tick), but two processes busy-polling concurrently (a forked
    // server + client) still garble each other's loopback/TCP state — a separate
    // data-correctness issue, future work. A cli/sti guard here doesn't fix that
    // and only adds overhead, so it's intentionally absent.
    ip_loopback_poll();   // deliver any self-addressed (loopback) packets
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            eth_poll(i);
        }
    }
    tcp_tick();           // drive TCP retransmit timers
    tcp_echo_poll();      // service the built-in loopback echo (port 7)
}

// ---- Userspace TCP socket layer -------------------------------------------
// Each blocking call drives the stack by polling in place (kernel_poll_net),
// the same way dhcp_request/dns_resolve do: these run in a process's syscall
// context, so a bounded busy-poll with an I/O delay between iterations advances
// BOTH endpoints (the client and the loopback echo server) without yielding.

static int nsock_valid(int s) {
    return s >= 0 && s < MAX_SOCKETS && nsocks[s].in_use;
}

int nsock_create(int domain, int type, int protocol) {
    (void)domain; (void)protocol;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!nsocks[i].in_use) {
            nsocks[i].type = type;
            nsocks[i].tcp_conn = -1;
            nsocks[i].local_port = 0;
            nsocks[i].listening = 0;
            nsocks[i].dq = NULL;
            nsocks[i].dq_head = nsocks[i].dq_tail = 0;
            if (type == SOCK_DGRAM) {
                nsocks[i].dq = (dgram_t*)kmalloc(sizeof(dgram_t) * UDP_QUEUE);
                if (!nsocks[i].dq) return -1;     // leave the slot free
            }
            nsocks[i].in_use = 1;                 // last, so a kmalloc fail leaves it free
            return i;
        }
    }
    return -1;
}

int nsock_connect(int s, uint32_t ip, uint16_t port) {
    if (!nsock_valid(s) || nsocks[s].type != SOCK_STREAM) return -1;   // TCP only
    static uint16_t ephemeral = 40000;            // client source-port pool
    uint16_t sport = ephemeral++;
    if (ephemeral >= 60000) ephemeral = 40000;
    int c = tcp_connect(ip, port, sport);
    if (c < 0) return -1;
    nsocks[s].tcp_conn = c;
    // Drive the 3-way handshake to completion: tcp_connect fired the SYN; the
    // SYN-ACK is processed on poll. Bounded busy-wait (same pattern as HTTP).
    for (int i = 0; i < 3000; i++) {
        kernel_poll_net();
        int st = tcp_state(c);
        if (st == TCP_STATE_ESTABLISHED) return 0;
        if (st == TCP_STATE_CLOSED) break;        // reset / handshake gave up
        for (volatile int d = 0; d < 1500; d++) inb(0x80);
    }
    return -1;
}

int nsock_bind(int s, uint32_t ip, uint16_t port) {
    (void)ip;                                     // single-homed: bind records the port
    if (!nsock_valid(s)) return -1;
    nsocks[s].local_port = port;
    return 0;
}

int nsock_listen(int s, int backlog) {
    (void)backlog;
    if (!nsock_valid(s) || nsocks[s].local_port == 0) return -1;
    int l = tcp_listen(nsocks[s].local_port);     // passive open in tcp.c
    if (l < 0) return -1;
    nsocks[s].tcp_conn = l;
    nsocks[s].listening = 1;
    return 0;
}

int nsock_accept(int s) {
    if (!nsock_valid(s) || !nsocks[s].listening || nsocks[s].tcp_conn < 0) return -1;
    int lc = nsocks[s].tcp_conn;
    // Block (busy-poll) until a client's handshake completes on our listen port,
    // then hand it out as a fresh socket. tcp_accept returns an ESTABLISHED child.
    for (int i = 0; i < 6000; i++) {
        int child = tcp_accept(lc);
        if (child >= 0) {
            for (int j = 0; j < MAX_SOCKETS; j++) {
                if (!nsocks[j].in_use) {
                    nsocks[j].in_use = 1;
                    nsocks[j].type = SOCK_STREAM;
                    nsocks[j].tcp_conn = child;
                    nsocks[j].local_port = nsocks[s].local_port;
                    nsocks[j].listening = 0;
                    return j;                     // caller wraps this in a new fd
                }
            }
            return -1;                            // socket table full
        }
        kernel_poll_net();
        for (volatile int d = 0; d < 1500; d++) inb(0x80);
    }
    return -1;                                    // timed out with no client
}

int nsock_send(int s, const void* buf, int len) {
    if (!nsock_valid(s) || nsocks[s].tcp_conn < 0 || len < 0) return -1;
    // tcp_send returns the on-wire segment length (IP+TCP+payload); a socket
    // write() must report the number of *payload* bytes accepted, so map any
    // success to `len` and only a hard failure to -1.
    int r = tcp_send(nsocks[s].tcp_conn, (const uint8_t*)buf, (uint32_t)len);
    return (r < 0) ? -1 : len;
}

int nsock_recv(int s, void* buf, int len) {
    if (!nsock_valid(s) || nsocks[s].tcp_conn < 0 || len <= 0) return -1;
    int c = nsocks[s].tcp_conn;
    // Block (busy-poll) until some data arrives, the peer fully closes, or a
    // timeout. Returns >0 bytes, or 0 for EOF — like a real recv().
    for (int i = 0; i < 6000; i++) {
        int n = tcp_recv(c, (uint8_t*)buf, (uint32_t)len);
        if (n > 0) return n;
        if (tcp_state(c) == TCP_STATE_CLOSED) return 0;   // closed, nothing buffered
        kernel_poll_net();
        for (volatile int d = 0; d < 1500; d++) inb(0x80);
    }
    return 0;                                     // timed out -> treat as EOF
}

// ---- UDP (SOCK_DGRAM) socket layer ------------------------------------------

int nsock_sendto(int s, const void* buf, int len, uint32_t ip, uint16_t port) {
    if (!nsock_valid(s) || nsocks[s].type != SOCK_DGRAM || len < 0) return -1;
    if (nsocks[s].local_port == 0) {              // auto-bind an ephemeral source port
        static uint16_t eph = 48000;
        nsocks[s].local_port = eph++;
        if (eph >= 60000) eph = 48000;
    }
    int r = udp_send(ip, port, nsocks[s].local_port, (const uint8_t*)buf, (uint32_t)len, -1);
    return (r < 0) ? -1 : len;                    // report payload bytes, not on-wire length
}

int nsock_recvfrom(int s, void* buf, int len, uint32_t* src_ip, uint16_t* src_port) {
    if (!nsock_valid(s) || nsocks[s].type != SOCK_DGRAM || !nsocks[s].dq || len <= 0) return -1;
    // Block (busy-poll) until a datagram is queued, or a timeout. Datagrams are
    // enqueued by nsock_udp_deliver from the receive path during these polls.
    for (int i = 0; i < 6000; i++) {
        if (nsocks[s].dq_head != nsocks[s].dq_tail) {
            dgram_t* d = &nsocks[s].dq[nsocks[s].dq_tail];
            int n = (int)d->len < len ? (int)d->len : len;
            memcpy(buf, d->data, n);
            if (src_ip)   *src_ip   = d->src_ip;
            if (src_port) *src_port = d->src_port;
            nsocks[s].dq_tail = (nsocks[s].dq_tail + 1) % UDP_QUEUE;
            return n;
        }
        kernel_poll_net();
        for (volatile int d = 0; d < 1500; d++) inb(0x80);
    }
    return -1;                                     // timed out, no datagram
}

// Called from udp_handle_packet: enqueue an inbound datagram to the DGRAM socket
// bound to dst_port, if any. Returns 1 if a socket claimed this port (delivered,
// or dropped-because-full), 0 if none (so the caller falls back to kernel
// listeners like dhcp/dns).
int nsock_udp_deliver(uint16_t dst_port, uint8_t* data, uint32_t len,
                      uint32_t src_ip, uint16_t src_port) {
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!nsocks[i].in_use || nsocks[i].type != SOCK_DGRAM || !nsocks[i].dq) continue;
        if (nsocks[i].local_port != dst_port) continue;
        int next = (nsocks[i].dq_head + 1) % UDP_QUEUE;
        if (next == nsocks[i].dq_tail) return 1;  // ring full -> drop (still ours)
        dgram_t* d = &nsocks[i].dq[nsocks[i].dq_head];
        uint32_t n = len > UDP_DGRAM_MAX ? UDP_DGRAM_MAX : len;
        memcpy(d->data, data, n);
        d->len = (uint16_t)n;
        d->src_ip = src_ip;
        d->src_port = src_port;
        nsocks[i].dq_head = next;
        return 1;
    }
    return 0;
}

int nsock_close(int s) {
    if (!nsock_valid(s)) return -1;
    if (nsocks[s].tcp_conn >= 0) tcp_close(nsocks[s].tcp_conn);
    if (nsocks[s].dq) { kfree(nsocks[s].dq); nsocks[s].dq = NULL; }   // UDP receive ring
    nsocks[s].in_use = 0;
    nsocks[s].tcp_conn = -1;
    return 0;
}

// ---- Built-in loopback echo service (port 7, TCP + UDP; RFC 862) -----------
// A tiny always-on, inetd-style echo server so a userspace socket program has
// something to talk to over loopback (127.0.0.1) — fully self-contained, no
// external host or NIC required. It also answers on the NIC address if reached.
#define ECHO_PORT 7
#define ECHO_MAX  8
static int echo_listen = -1;
static int echo_conns[ECHO_MAX];

// UDP echo: bounce each datagram straight back to its sender. Registered as a
// kernel udp listener, so it only fires when no userspace socket claims port 7
// (nsock_udp_deliver runs first in udp_handle_packet).
static void udp_echo_handler(uint8_t* data, uint32_t len, uint32_t src_ip, uint16_t src_port) {
    udp_send(src_ip, src_port, ECHO_PORT, data, len, -1);
}

void tcp_echo_init(void) {
    for (int i = 0; i < ECHO_MAX; i++) echo_conns[i] = -1;
    echo_listen = tcp_listen(ECHO_PORT);
    udp_register_listener(ECHO_PORT, udp_echo_handler);   // echo UDP datagrams too
}

void tcp_echo_poll(void) {
    if (echo_listen < 0) return;
    int c;
    while ((c = tcp_accept(echo_listen)) >= 0) {          // adopt newly-accepted clients
        for (int i = 0; i < ECHO_MAX; i++)
            if (echo_conns[i] < 0) { echo_conns[i] = c; break; }
    }
    for (int i = 0; i < ECHO_MAX; i++) {                  // bounce back any received bytes
        int ec = echo_conns[i];
        if (ec < 0) continue;
        if (tcp_state(ec) == TCP_STATE_CLOSED) { echo_conns[i] = -1; continue; }
        uint8_t buf[512];
        int n = tcp_recv(ec, buf, sizeof(buf));
        if (n > 0) tcp_send(ec, buf, (uint32_t)n);
    }
}
