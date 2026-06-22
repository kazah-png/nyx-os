#include "kernel.h"

extern net_iface_t net_interfaces[8];

#define MAX_SOCKETS 64

typedef struct {
    int fd;
    int domain;
    int type;
    int protocol;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    int state;
    void* rx_buffer;
    void* tx_buffer;
} socket_t;

static socket_t sockets[MAX_SOCKETS];
static int socket_count = 0;

extern int rtl8139_init(void);

extern void arp_init(void);

void init_net(void) {
    memset_asm(sockets, 0, sizeof(sockets));
    memset_asm(net_interfaces, 0, sizeof(net_interfaces));

    net_iface_t* lo = &net_interfaces[0];
    strcpy(lo->name, "lo");
    lo->ip = 0x7F000001;
    lo->netmask = 0xFF000000;
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
    for (int i = 0; i < 8; i++) {
        if (net_interfaces[i].name[0] && strcmp(net_interfaces[i].name, "lo") != 0) {
            eth_poll(i);
        }
    }
}

int net_create_socket(int domain, int type, int protocol) {
    if (socket_count >= MAX_SOCKETS) return -1;
    int idx = socket_count++;
    sockets[idx].fd = idx;
    sockets[idx].domain = domain;
    sockets[idx].type = type;
    sockets[idx].protocol = protocol;
    sockets[idx].state = 0;
    return idx;
}

int net_bind(int sock, uint32_t ip, uint16_t port) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    sockets[sock].local_ip = ip;
    sockets[sock].local_port = port;
    return 0;
}

int net_listen(int sock, int backlog) {
    (void)backlog;
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    sockets[sock].state = 1;
    return 0;
}

int net_accept(int sock) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    return net_create_socket(2, 1, 0);
}

int net_connect(int sock, uint32_t ip, uint16_t port) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    sockets[sock].remote_ip = ip;
    sockets[sock].remote_port = port;
    sockets[sock].state = 2;
    return 0;
}

int net_send(int sock, const void* buf, size_t len) {
    (void)buf;
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    return len;
}

int net_recv(int sock, void* buf, size_t len) {
    (void)buf;
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    return len;
}

int net_close(int sock) {
    if (sock < 0 || sock >= MAX_SOCKETS) return -1;
    sockets[sock].state = 0;
    return 0;
}
