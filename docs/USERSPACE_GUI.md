# User-space GUI — migration audit & plan

**Status: planning / groundwork.** This document audits where the NyxOS GUI runs today,
what user-space plumbing already exists, and a phased plan to move GUI *applications* out
of the kernel and into ring-3 processes. It is a living plan, not a finished migration.

## The problem

Today the **entire GUI runs in ring 0**. The compositor (Hemera), the window manager, the
desktop, and every app — terminal (Erebus), browser (Selene), editor (Mnemosyne), paint,
the games, the file manager, the task manager, the wallpaper picker — live under
`kernel/gui/` and are compiled straight into `nyx-kernel.bin`. Only the `user/*.c` programs
(the coreutils and the `xbm` ports) are ring-3 ELFs.

For a daily-driver-grade OS that is real technical debt:

- **No fault isolation.** A bug in any GUI app faults in kernel context and panics the
  *whole* machine, instead of just closing one window.
- **No memory protection** between GUI components — they share the kernel address space.
- **Full privilege.** Every GUI app runs with kernel privileges; there is no boundary.

The goal is the daily-driver shape: a display server plus GUI **clients that run in ring 3**,
talking to the kernel over a small syscall ABI — so an app crash is contained.

## What already exists (the good news)

NyxOS already has a working **ring-3 windowing ABI**. A user-space program can open a
composited desktop window, blit pixels to it, and receive input — all without kernel
privileges. This is the foundation the migration builds on; it is not a greenfield.

**Syscalls** (see [SYSCALLS.md](SYSCALLS.md), `kernel/core/syscall.c`, `user/syscall.h`):

| # | syscall | what it does |
|---|---------|--------------|
| 54 | `SYS_FBINFO` | screen width/height/bpp (for a fullscreen app) |
| 55 | `SYS_FBPRESENT` | blit a 32bpp buffer to the whole screen, scaled (fullscreen app) |
| 56 | `SYS_GETKEYEVENT` | next raw key make/break event |
| 57 | `SYS_WIN_CREATE` | open a `w×h` composited desktop window → id |
| 58 | `SYS_WIN_DESTROY` | destroy a window (idempotent) |
| 59 | `SYS_WIN_PRESENT` | blit a `w×h` XRGB buffer as the whole client area |
| 60 | `SYS_WIN_POLL_EVENT` | pop one input event, non-blocking |

**Kernel model** — `kernel/gui/core/userwin.c`: a fixed registry of user windows, each owning
its backing buffer and a per-window **event ring** (`UWE_KEY`, `UWE_CLICK`, `UWE_MOVE`, with
client-relative coordinates). `uwin_create` binds a real compositor window whose draw
callback blits the app's last-presented buffer, and whose key/click/move callbacks push
events into that window's ring. Keyboard **and** mouse both reach the client. There is a
known-answer test, `uwin_selftest` (registered in the battery as `userwin`), that exercises
the ring, the registry, present size-checking, event round-trip, release and slot reuse —
so the model is verified headlessly.

**Proof it works end-to-end** — `user/wintest.c`: a ring-3 ELF that `win_create`s a window,
animates it with `win_present`, and pumps `win_poll_event` until closed. This is the
existing demonstration that the whole path composites correctly from user space.

So: **fullscreen** ring-3 graphics (DOOM uses `SYS_FBINFO`/`SYS_FBPRESENT`/`SYS_GETKEYEVENT`)
**and windowed** ring-3 graphics (`wintest` uses `SYS_WIN_*`) already run today.

## The gap

The apps themselves have not been migrated — they are still in-kernel C using the compositor's
internal API directly (`window_create`, `fb_*`, `font_*`), not ring-3 clients using `SYS_WIN_*`.
Moving one means rewriting it as a standalone ELF that renders into its own buffer and presents
it. The client ABI is also still minimal for rich apps; likely additions as real apps are ported:

- **Partial present.** `uwin_present` only accepts a whole-client blit that matches the window
  size exactly; there is no dirty-rect update, so a large window repaints fully each frame.
- **No resize / no title update** after `win_create`; no window-move or focus events surfaced
  to the client (only key/click/move within the client area).
- **No shared widget toolkit** in user space yet (buttons, menus, text fields) — each ported
  app would re-implement its own, or we grow a small ring-3 UI lib.
- The **compositor/display-server stays in the kernel** under this plan (see below); fully
  moving Hemera itself to user space is a separate, much larger step.

## Plan (phased, incremental)

The pragmatic first target is **app fault isolation** — the biggest daily-driver win — while
leaving the compositor in the kernel acting as an in-kernel display server that ring-3 clients
drive through `SYS_WIN_*`.

1. **Audit & document** (this file). ✅ groundwork.
2. **Pick the smallest self-contained app** and port it to a ring-3 ELF client on top of the
   existing `SYS_WIN_*` ABI (e.g. a simple viewer/toy), launched like any other ELF; verify it
   composites and takes input, with the original in-kernel version kept until parity is proven.
3. **Close ABI gaps the port exposes** — most likely partial present and a title/resize path —
   each behind a syscall with its own KAT (extend `uwin_selftest`).
4. **Grow a tiny ring-3 UI helper** (draw text/buttons into a client buffer) so ported apps
   share widget code instead of each re-implementing it.
5. **Port the remaining apps one at a time**, boot-verifying 0-fault at each step, then retire
   the in-kernel version once the ring-3 one reaches parity.
6. **(Stretch, much later)** move the compositor itself toward user space.

Each step is a small, independently verifiable increment — no big-bang rewrite of a working OS.

## Pointers

- Syscall ABI: [SYSCALLS.md](SYSCALLS.md) (rows 54–60).
- Kernel model + KAT: `kernel/gui/core/userwin.{c,h}` (`uwin_selftest`, battery entry `userwin`).
- Working ring-3 client: `user/wintest.c`.
- In-kernel GUI (what migrates): `kernel/gui/core/` (compositor) and `kernel/gui/apps/`.
