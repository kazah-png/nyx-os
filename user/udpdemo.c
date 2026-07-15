#include "libc.h"

/* udpdemo — userspace UDP sockets (v5.8.55). Single process over loopback:
 *   rx = socket(SOCK_DGRAM); bind(rx, 9001)     -- a receiver on UDP port 9001
 *   tx = socket(SOCK_DGRAM); sendto(tx, ..., 9001) -- send it a datagram
 *   recvfrom(rx, ..., &ip, &port)               -- receive it, with the sender's addr
 * Exercises SOCK_DGRAM + sendto()/recvfrom() end to end, fully self-contained
 * (no NIC/host). sendto queues the datagram into the loopback ring; recvfrom
 * busy-polls the net, which delivers it into rx's queue. */

int main(void) {
    int rx = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx < 0)                          { printf("udpdemo: socket() failed\n"); return 1; }
    if (bind(rx, INADDR_ANY, 9001) != 0) { printf("udpdemo: bind() failed\n");   return 1; }
    printf("udpdemo: receiver bound to UDP port 9001 (fd=%d)\n", rx);

    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (tx < 0)                          { printf("udpdemo: socket() failed\n"); return 1; }

    const char* m = "hello over UDP";
    int slen = strlen(m);
    int r = sendto(tx, m, slen, 0, inet_ipv4(127, 0, 0, 1), 9001);
    printf("udpdemo: sent %d bytes to 127.0.0.1:9001\n", r);

    char buf[64];
    unsigned int sip = 0;
    int sport = 0;
    int n = recvfrom(rx, buf, sizeof(buf) - 1, 0, &sip, &sport);   /* blocks for the datagram */
    if (n > 0) {
        buf[n] = '\0';
        printf("udpdemo: received \"%s\" (%d bytes) from %d.%d.%d.%d:%d\n",
               buf, n, sip & 0xFF, (sip >> 8) & 0xFF, (sip >> 16) & 0xFF, (sip >> 24) & 0xFF, sport);
    } else {
        printf("udpdemo: recvfrom() returned %d\n", n);
    }

    close(tx);
    close(rx);
    int ok = (n == slen);
    printf("udpdemo: %s\n", ok ? "OK -- UDP sockets work!" : "MISMATCH");
    return ok ? 0 : 1;
}
