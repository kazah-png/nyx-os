#ifndef URLPARSE_H
#define URLPARSE_H
// Hardened URL parser — one audited splitter for the untrusted URLs the browser (Selene)
// and the package manager (xbm) both handle. It replaces two ad-hoc copies that shared two
// real bugs: the port was accumulated into a uint16_t with NO bound, so `host:70000` silently
// wrapped to a wrong port (and `:65536` wrapped to 0), and an over-long host was truncated to
// the buffer instead of rejected — i.e. a crafted URL could redirect a connection to a
// different host/port than it reads. This parser bounds the port to 1..65535 (rejecting the
// overflow) and rejects a host that does not fit, so a malformed URL fails cleanly.
#include "kernel.h"

// Split url into host / port / path (and note https). The scheme is optional: "http://" and
// "https://" set the default port (80 / 443) and *is_https; with no scheme the URL is treated
// as http (so a bare "example.com" from the address bar still works). host is rejected (return
// -1, nothing connects) if empty or >= hostsz; the port is rejected if it is 0 or > 65535 or a
// ':' is not followed by a digit; path defaults to "/". Returns 0 on success, -1 on any
// malformed field. is_https may be NULL.
int url_parse(const char* url, char* host, int hostsz, uint16_t* port,
              char* path, int pathsz, int* is_https);

// Known-answer test (`urlparse`): valid http/https/scheme-less/explicit-port URLs plus the
// rejection cases (port overflow, port 0, ':' without a digit, empty host, over-long host).
int url_parse_selftest(void);

#endif // URLPARSE_H
