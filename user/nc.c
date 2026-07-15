#include "libc.h"

/* nc — a minimal netcat over NyxOS sockets (v5.8.56). Bridges stdin/stdout to a
 * TCP or UDP socket. Half-duplex (send a stdin chunk, print one response) because
 * NyxOS has blocking sockets and no select(); that's fine for request/response
 * protocols like the built-in echo service or HTTP/1.0. Modes:
 *   nc <ip> <port>       TCP client   (e.g.  echo hi | nc 127.0.0.1 7)
 *   nc -u <ip> <port>    UDP client   (e.g.  echo hi | nc -u 127.0.0.1 7)
 *   nc -l <port>         TCP server   (accept one connection, then bridge)
 */

static unsigned int parse_ip(const char* s) {
    unsigned int ip = 0; int oct = 0, shift = 0;
    for (const char* p = s; ; p++) {
        if (*p >= '0' && *p <= '9') { oct = oct * 10 + (*p - '0'); }
        else { ip |= (unsigned int)(oct & 0xFF) << shift; shift += 8; oct = 0; if (!*p) break; }
    }
    return ip;                        // network order: first octet in the low byte
}

/* Each stdin chunk is sent, then one response is read and printed, until stdin
 * EOF or the peer closes. `recv_first` reads before writing (the accept()ed
 * server side, which speaks second). */
static void bridge(int sock, int recv_first) {
    char buf[1024];
    for (;;) {
        if (recv_first) {
            int m = read(sock, buf, sizeof(buf));
            if (m <= 0) break;
            write(1, buf, m);
        }
        int n = read(0, buf, sizeof(buf));
        if (n <= 0) break;                       // stdin EOF
        if (write(sock, buf, n) != n) break;
        if (!recv_first) {
            int m = read(sock, buf, sizeof(buf));
            if (m > 0) write(1, buf, m);
            else if (m == 0) break;               // peer closed
        }
    }
}

int main(int argc, char** argv) {
    /* nc -l <port> : TCP server — accept one client, then bridge (recv first). */
    if (argc >= 3 && strcmp(argv[1], "-l") == 0) {
        int port = atoi(argv[2]);
        int lfd = socket(AF_INET, SOCK_STREAM, 0);
        if (lfd < 0 || bind(lfd, INADDR_ANY, port) != 0 || listen(lfd, 1) != 0) {
            printf("nc: cannot listen on port %d\n", port); return 1;
        }
        printf("nc: listening on port %d\n", port);
        int c = accept(lfd);
        if (c < 0) { printf("nc: accept failed\n"); return 1; }
        bridge(c, 1);
        close(c); close(lfd);
        return 0;
    }

    /* nc -u <ip> <port> : UDP client. */
    if (argc >= 4 && strcmp(argv[1], "-u") == 0) {
        unsigned int ip = parse_ip(argv[2]);
        int port = atoi(argv[3]);
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) { printf("nc: socket failed\n"); return 1; }
        char buf[1024];
        for (;;) {
            int n = read(0, buf, sizeof(buf));
            if (n <= 0) break;
            sendto(fd, buf, n, 0, ip, port);
            unsigned int sip = 0; int sport = 0;
            int m = recvfrom(fd, buf, sizeof(buf), 0, &sip, &sport);
            if (m > 0) write(1, buf, m);
        }
        close(fd);
        return 0;
    }

    /* nc <ip> <port> : TCP client. */
    if (argc < 3) {
        printf("usage: nc <ip> <port> | nc -u <ip> <port> | nc -l <port>\n");
        return 1;
    }
    unsigned int ip = parse_ip(argv[1]);
    int port = atoi(argv[2]);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { printf("nc: socket failed\n"); return 1; }
    if (connect(fd, ip, port) != 0) {
        printf("nc: connect to %s:%d failed\n", argv[1], port);
        close(fd);
        return 1;
    }
    bridge(fd, 0);
    close(fd);
    return 0;
}
