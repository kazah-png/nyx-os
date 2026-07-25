#ifndef SELENE_WIN_H
#define SELENE_WIN_H
#include "compositor.h"

// Selene - the NyxOS web browser (v5.9.44). A GUI window with a URL bar and a
// rendered (HTML-tag-stripped, word-wrapped) view of a page fetched over HTTP via
// http_get(). It ensures connectivity (DHCP) on load, resolves DNS and fetches the
// real page. HTTP only for now (no TLS). Named for Selene, the Greek moon goddess -
// on-theme with Nyx (night). Launched by the `selene` command and a desktop icon.
#define SELENE_W 720
#define SELENE_H 520

void* selene_create_ctx(void);
void  selene_win_draw(window_t* win, int cx, int cy, uint32_t cw, uint32_t ch);
void  selene_win_key(window_t* win, int key);
void  selene_first_load(window_t* win);   // fetch the default page right after the window opens

#endif
