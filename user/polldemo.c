#include "libc.h"

/* polldemo — I/O multiplexing with poll() (v5.8.60). Watches a pipe AND a TCP
 * socket in a single poll() call: it writes into a pipe and sends to the loopback
 * echo service, then poll() reports each fd readable as its data becomes available
 * and we drain both. A final poll() with a short timeout returns 0 (nothing ready),
 * exercising the timeout path. This is the primitive a full-duplex nc would use. */

int main(void) {
    int p[2];
    if (pipe(p) != 0) { printf("polldemo: pipe() failed\n"); return 1; }
    const char* pmsg = "from the pipe";
    write(p[1], pmsg, strlen(pmsg));        /* keep p[1] open so the pipe isn't at EOF */

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0 || connect(s, inet_ipv4(127, 0, 0, 1), 7) != 0) {
        printf("polldemo: connect to echo failed\n"); return 1;
    }
    const char* smsg = "from the socket";
    write(s, smsg, strlen(smsg));

    struct pollfd pfd[2] = { { p[0], POLLIN, 0 }, { s, POLLIN, 0 } };
    printf("polldemo: poll()ing a pipe (fd %d) + a socket (fd %d) together...\n", p[0], s);

    int got_pipe = 0, got_sock = 0;
    char buf[64];
    for (int r = 0; r < 10 && !(got_pipe && got_sock); r++) {
        int n = poll(pfd, 2, 3000);
        if (n <= 0) { printf("polldemo: poll returned %d\n", n); break; }
        if (!got_pipe && (pfd[0].revents & POLLIN)) {
            int k = read(p[0], buf, sizeof(buf) - 1);
            if (k > 0) { buf[k] = '\0'; printf("polldemo: pipe readable   -> \"%s\"\n", buf); got_pipe = 1; }
        }
        if (!got_sock && (pfd[1].revents & POLLIN)) {
            int k = read(s, buf, sizeof(buf) - 1);
            if (k > 0) { buf[k] = '\0'; printf("polldemo: socket readable -> \"%s\"\n", buf); got_sock = 1; }
        }
    }

    /* Both drained now: a fresh poll on the socket should find nothing and time out. */
    struct pollfd one = { s, POLLIN, 0 };
    printf("polldemo: polling the idle socket (500ms) -- expect a timeout...\n");
    int t = poll(&one, 1, 500);
    printf("polldemo: poll -> %d (%s)\n", t, t == 0 ? "timeout, as expected" : "unexpected");

    close(p[1]); close(p[0]); close(s);
    int ok = got_pipe && got_sock && (t == 0);
    printf("polldemo: %s\n", ok ? "OK -- poll() multiplexing works!" : "MISMATCH");
    return ok ? 0 : 1;
}
