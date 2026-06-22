#ifndef TCP_H
#define TCP_H

#include "kernel.h"

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_SYN_RCVD    2
#define TCP_STATE_ESTABLISHED 3
#define TCP_STATE_FIN_WAIT1   4
#define TCP_STATE_FIN_WAIT2   5
#define TCP_STATE_CLOSE_WAIT  6
#define TCP_STATE_LAST_ACK    7
#define TCP_STATE_TIME_WAIT   8
#define TCP_STATE_LISTEN      9

#define TCP_MAX_CONNS 8

typedef struct {
    int active;
    int state;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t* recv_buf;
    uint32_t recv_len;
    uint32_t recv_cap;
    uint32_t sent_unacked;
} tcp_conn_t;

int tcp_init(void);
int tcp_connect(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_send(int conn_id, const uint8_t* data, uint32_t len);
int tcp_recv(int conn_id, uint8_t* buf, uint32_t max_len);
int tcp_close(int conn_id);
void tcp_handle_packet(uint8_t* packet, uint32_t len, uint32_t src_ip);

#endif
