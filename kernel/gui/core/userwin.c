#include "userwin.h"
#include "compositor.h"
#include "gfx_prims.h"

/* ---------------------------------------------------------------------------
 * Pure event ring: push at (head+count), pop from head, oldest dropped on full.
 * ------------------------------------------------------------------------- */

void uwin_evq_init(uwin_evq_t* q) { q->head = 0; q->count = 0; }

void uwin_evq_push(uwin_evq_t* q, int64_t kind, int64_t a, int64_t b, int64_t c) {
    if (q->count == USERWIN_EVQ_LEN) {                 /* full: drop the oldest */
        q->head = (q->head + 1) % USERWIN_EVQ_LEN;
        q->count--;
    }
    int slot = (q->head + q->count) % USERWIN_EVQ_LEN;
    q->ev[slot].kind = kind;
    q->ev[slot].a = a;
    q->ev[slot].b = b;
    q->ev[slot].c = c;
    q->count++;
}

int uwin_evq_pop(uwin_evq_t* q, uwin_event_t* out) {
    if (q->count == 0) return 0;
    *out = q->ev[q->head];
    q->head = (q->head + 1) % USERWIN_EVQ_LEN;
    q->count--;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Window registry. Each slot owns its backing buffer and event ring; the
 * compositor window (if any) points back here through win->reserved.
 * ------------------------------------------------------------------------- */

typedef struct {
    int        active;
    int        win_id;    /* compositor window id, or -1 (headless / not bound) */
    uint32_t   w, h;      /* client-area dimensions */
    uint32_t*  backing;   /* w*h XRGB, last presented; NULL until first present */
    uwin_evq_t evq;
} user_win_t;

static user_win_t g_uwin[USERWIN_MAX];

static user_win_t* uwin_slot(int id) {
    if (id < 0 || id >= USERWIN_MAX) return 0;
    return g_uwin[id].active ? &g_uwin[id] : 0;
}

/* Allocate + initialise a registry slot (no compositor window). Shared by
 * uwin_create and the KAT so the model is testable without a running desktop. */
static int uwin_alloc_slot(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0 || w > USERWIN_MAX_W || h > USERWIN_MAX_H) return -1;
    for (int i = 0; i < USERWIN_MAX; i++) {
        if (!g_uwin[i].active) {
            user_win_t* s = &g_uwin[i];
            s->w = w; s->h = h; s->backing = 0; s->win_id = -1;
            uwin_evq_init(&s->evq);
            s->active = 1;
            return i;
        }
    }
    return -1;                                          /* table full */
}

/* Release a slot's resources exactly once — re-entrant-safe so the compositor's
 * on_close (fired during window_destroy) and an app-initiated uwin_destroy can
 * both run without a double free. */
static void uwin_release(user_win_t* s) {
    if (!s || !s->active) return;
    s->active = 0;                                      /* mark dead FIRST */
    if (s->backing) { kfree(s->backing); s->backing = 0; }
    s->win_id = -1;
}

/* ---- compositor callbacks ---- */

static void uwin_draw_cb(window_t* win, int clip_x, int clip_y, uint32_t clip_w, uint32_t clip_h) {
    (void)clip_x; (void)clip_y; (void)clip_w; (void)clip_h;
    user_win_t* s = (user_win_t*)win->reserved;
    if (!s || !s->active) return;
    if (s->backing)
        fb_blit(s->backing, 0, 0, s->w, s->h, WIN_CLIENT_X(win), WIN_CLIENT_Y(win));
    else                                                /* not presented yet */
        fb_fill_rect(WIN_CLIENT_X(win), WIN_CLIENT_Y(win), s->w, s->h, 0x00101018);
}

static void uwin_on_key(window_t* win, int key) {
    user_win_t* s = (user_win_t*)win->reserved;
    if (s && s->active) uwin_evq_push(&s->evq, UWE_KEY, key, 0, 0);
}

static void uwin_on_click(window_t* win, int mx, int my, int btn) {
    user_win_t* s = (user_win_t*)win->reserved;
    if (s && s->active)
        uwin_evq_push(&s->evq, UWE_CLICK, mx - WIN_CLIENT_X(win), my - WIN_CLIENT_Y(win), btn);
}

static void uwin_on_move(window_t* win, int mx, int my, int btns) {
    user_win_t* s = (user_win_t*)win->reserved;
    if (s && s->active)
        uwin_evq_push(&s->evq, UWE_MOVE, mx - WIN_CLIENT_X(win), my - WIN_CLIENT_Y(win), btns);
}

static void uwin_on_close_cb(window_t* win) {
    /* window_destroy calls this before freeing win. If the app initiated the
     * teardown, uwin_destroy already released the slot (no-op here); if the
     * compositor's close box fired it, this is where the slot is released and the
     * app learns of it via poll_event returning -1 (its window no longer exists). */
    uwin_release((user_win_t*)win->reserved);
}

/* ---- syscall entry points ---- */

int64_t uwin_create(uint32_t w, uint32_t h, const char* title) {
    int id = uwin_alloc_slot(w, h);
    if (id < 0) return -1;

    /* Bind a compositor window: client area is w x h, the frame adds TITLE_H. */
    int px = 80 + id * 24, py = 80 + id * 24;
    window_t* win = window_create(px, py, w, h + TITLE_H, title ? title : "app", uwin_draw_cb);
    if (win) {
        win->reserved     = &g_uwin[id];
        win->on_key       = uwin_on_key;
        win->on_click     = uwin_on_click;
        win->on_mousemove = uwin_on_move;
        win->on_close     = uwin_on_close_cb;
        g_uwin[id].win_id = win->id;
    }
    /* No compositor window (desktop not up / table full) still yields a usable
     * headless slot: present/poll work, it simply isn't drawn. */
    return id;
}

int64_t uwin_destroy(int id) {
    user_win_t* s = uwin_slot(id);
    if (!s) return 0;                                   /* idempotent */
    int win_id = s->win_id;
    uwin_release(s);                                    /* free backing, mark dead */
    if (win_id >= 0) window_destroy(win_id);            /* fires on_close_cb -> no-op */
    return 0;
}

int64_t uwin_present(int id, const uint32_t* px, uint32_t w, uint32_t h) {
    user_win_t* s = uwin_slot(id);
    if (!s || !px) return -1;
    if (w != s->w || h != s->h) return -1;              /* whole client-area blit only */
    if (!s->backing) {
        s->backing = (uint32_t*)kmalloc((size_t)s->w * s->h * 4);
        if (!s->backing) return -1;
    }
    uint32_t n = s->w * s->h;
    for (uint32_t i = 0; i < n; i++) s->backing[i] = px[i];
    return 0;
}

int64_t uwin_poll_event(int id, uwin_event_t* out) {
    user_win_t* s = uwin_slot(id);
    if (!s || !out) return -1;
    return uwin_evq_pop(&s->evq, out) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * KAT — exercises the event ring, the registry (alloc / present / poll / release /
 * reuse / exhaustion) with no compositor window, entirely on kernel buffers.
 * ------------------------------------------------------------------------- */

int uwin_selftest(void) {
    uwin_event_t e;

    /* 1. event ring: empty, FIFO order, overflow drops oldest. (0 = PASS.) */
    uwin_evq_t q; uwin_evq_init(&q);
    if (uwin_evq_pop(&q, &e) != 0) return 1;
    for (int i = 0; i < 3; i++) uwin_evq_push(&q, UWE_KEY, i, 0, 0);
    if (uwin_evq_pop(&q, &e) != 1 || e.a != 0) return 2;
    if (uwin_evq_pop(&q, &e) != 1 || e.a != 1) return 3;
    uwin_evq_init(&q);
    for (int i = 0; i < USERWIN_EVQ_LEN + 5; i++) uwin_evq_push(&q, UWE_KEY, i, 0, 0);
    if (q.count != USERWIN_EVQ_LEN) return 4;            /* capped */
    if (uwin_evq_pop(&q, &e) != 1 || e.a != 5) return 5; /* oldest 5 dropped */

    /* 2. registry: alloc, present (size-checked), event round-trip, release. */
    int id = uwin_alloc_slot(64, 48);
    if (id < 0 || !uwin_slot(id)) return 6;
    static uint32_t buf[64 * 48];
    for (int i = 0; i < 64 * 48; i++) buf[i] = 0x00334455;
    if (uwin_present(id, buf, 64, 48) != 0) return 7;
    if (uwin_present(id, buf, 64, 47) != -1) return 8;  /* wrong size rejected */
    if (!uwin_slot(id)->backing || uwin_slot(id)->backing[10] != 0x00334455) return 9;
    uwin_evq_push(&uwin_slot(id)->evq, UWE_CLICK, 3, 4, 1);
    if (uwin_poll_event(id, &e) != 1 || e.kind != UWE_CLICK || e.a != 3 || e.c != 1) return 10;
    if (uwin_poll_event(id, &e) != 0) return 11;        /* drained */
    uwin_release(uwin_slot(id));
    if (uwin_slot(id)) return 12;                       /* now dead */
    if (uwin_present(id, buf, 64, 48) != -1) return 13; /* dead slot rejects */
    if (uwin_poll_event(id, &e) != -1) return 14;
    if (uwin_destroy(id) != 0) return 15;               /* idempotent on a free slot */

    /* 3. exhaustion + freed-id reuse. */
    int ids[USERWIN_MAX];
    for (int i = 0; i < USERWIN_MAX; i++) { ids[i] = uwin_alloc_slot(8, 8); if (ids[i] < 0) return 16; }
    if (uwin_alloc_slot(8, 8) != -1) return 17;         /* table full */
    uwin_release(uwin_slot(ids[3]));
    if (uwin_alloc_slot(8, 8) != ids[3]) return 18;     /* freed id reused */
    for (int i = 0; i < USERWIN_MAX; i++) if (uwin_slot(i)) uwin_release(uwin_slot(i));
    return 0;
}
