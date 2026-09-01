#include "libc.h"

/* yes — repeatedly print a line until killed. With no args prints "y";
 * with args prints them space-separated (like `yes foo bar` -> "foo bar").
 * Stops on write error / SIGPIPE (when piped to `head` etc). */

int main(int argc, char** argv) {
    char buf[1024];
    int len = 0;

    if (argc < 2) {
        buf[0] = 'y';
        buf[1] = '\n';
        len = 2;
    } else {
        for (int i = 1; i < argc; i++) {
            const char* s = argv[i];
            while (*s && len < (int)sizeof(buf) - 2) buf[len++] = *s++;
            if (i + 1 < argc && len < (int)sizeof(buf) - 2) buf[len++] = ' ';
        }
        if (len < (int)sizeof(buf) - 1) buf[len++] = '\n';
    }

    while (1) {
        long r = write(1, buf, len);
        if (r < 0) break;
    }
    return 0;
}
