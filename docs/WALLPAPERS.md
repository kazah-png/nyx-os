# NyxOS wallpaper & desktop-background system

The desktop background is not a static image — it is **painted from code every time the
desktop composites**, from a chosen *base colour* and a chosen *render style*. Nine styles
ship, six of them animated (a twinkling star field, a shooting star, aurora curtains, drifting
nebula clouds, rising fireflies, moonlight ripples). Everything is drawn with **integer arithmetic only** — the
kernel is built `-mno-sse` with no floating point anywhere (see `docs/ARCHITECTURE.md`) — and
every animated frame is a **pure function of the clock**, so there is no per-frame state to keep
and no flicker. The whole system keeps the Nyx night-goddess identity: purple, moonlit, calm.

This document explains how the background is drawn, the integer-animation pattern that powers the
live styles, how the compositor decides when to repaint, and how to add a new style.

---

## At a glance

| File | Role |
|---|---|
| `kernel/gui/core/wallpaper_win.h` | Public interface: `WP_STYLE_*` enum, `WALLPAPER_COUNT`, `wallpaper_base_color()`, `wallpaper_style()` |
| `kernel/gui/core/wallpaper_win.c` | The **Wallpaper picker** app (base-colour palette, style grid, live previews) + the selected-state getters |
| `kernel/gui/core/compositor.c` | `draw_background()` paints the scene; `wp_isin()` is the integer sine; `compositor_run()` gates the animated repaint |

Two integers hold the entire user choice (`wallpaper_win.c`):

```c
static int g_wallpaper = 0;                    // base-colour index; default 0 = brand purple (morado)
static int g_style     = WP_STYLE_NIGHTFALL;   // render style;      default = moon + stars
```

`wallpaper_base_color()` returns `palette[g_wallpaper]` as a packed RGB, and `wallpaper_style()`
returns `g_style`; `draw_background()` reads both on every composite.

---

## The nine styles

| Enum | Name | Animated | What it draws |
|---|---|:---:|---|
| `WP_STYLE_CLEAN` | Limpio | — | A clean vertical gradient in the base hue (the v6 default look) |
| `WP_STYLE_NIGHTFALL` | Nightfall | — | Gradient **+ moon + star field** — the classic still night (the default style) |
| `WP_STYLE_FLAT` | Plano | — | A single flat solid fill — the calmest possible desktop |
| `WP_STYLE_STARFIELD` | Estrellas | ✓ | Nightfall scene whose stars **twinkle** |
| `WP_STYLE_SHOOTINGSTAR` | Meteoros | ✓ | Nightfall scene with a periodic **shooting star** |
| `WP_STYLE_AURORA` | Aurora | ✓ | Nightfall scene with slow drifting **aurora curtains** |
| `WP_STYLE_NEBULA` | Nebula | ✓ | Nightfall scene with soft drifting **nebula clouds** |
| `WP_STYLE_LUCES` | Luces | ✓ | Nightfall scene with slow rising **glowing orbs** (fireflies) |
| `WP_STYLE_ONDAS` | Ondas | ✓ | Nightfall scene with concentric **moonlight ripples** from the moon |

`WP_STYLE_COUNT` closes the enum; the picker draws exactly that many style buttons.

---

## How the background is painted — `draw_background()`

`draw_background()` (`kernel/gui/core/compositor.c:822`) runs top-to-bottom and short-circuits as
early as the chosen style allows, so simpler styles cost less:

1. **Read the inputs.** Framebuffer size, the base colour split into `br/bg/bb`, and the style.
2. **`FLAT` shortcut.** One `fb_fill_rect` of the whole screen in the base colour, then return.
3. **Vertical gradient** (shared by `CLEAN` and every night style). 96 horizontal bands; band *i*
   is the base colour scaled by `pct = 45 + i*70/96`, i.e. **45 % at the top → ~115 % at the
   bottom** (clamped to 255). The result is a darker sky up top brightening toward the taskbar.
4. **`CLEAN` stops here.** Any style that is not one of the six night styles returns now.
5. **The moon.** Upper-right at `(fw-130, 96)`, radius 40, drawn as four concentric halo rings
   fading inward to a lilac core `rgb(214,202,244)`, plus three faint craters for character.
6. **The star field.** A deterministic LCG (seed `0x9E3779B1`) scatters 110 stars across the
   upper ¾ of the screen, **skipping any that fall within `mr+18` px of the moon**. Positions are
   fixed forever (seeded by a constant) so stars never jump — only brightness moves. `NIGHTFALL`
   uses three fixed shades; `STARFIELD` twinkles (see below).
7. **The per-style overlay.** `SHOOTINGSTAR`, `AURORA`, `NEBULA` and `LUCES` each add their own
   layer on top of the moon + stars (detailed below).

Because stars are seeded from a constant and the moon is at a fixed spot, the *still* styles
(`NIGHTFALL`) produce a byte-identical background every composite — the compositor is free to skip
the redraw entirely when nothing else changed.

---

## The integer-animation pattern

The kernel has **no floating point** (`-mno-sse`), so all motion is built from two integer
primitives.

### `wp_isin()` — a fixed-point sine

```c
static int wp_isin(int ph) {          // compositor.c:814
    ph &= 255;
    int sign = 1;
    if (ph >= 128) { ph -= 128; sign = -1; }   // second half mirrors + negates
    int y = ph * (128 - ph);                    // 0..4096 parabola, peak at ph=64
    return sign * (y / 4);                       // -1024..1024
}
```

It approximates `sin` with a parabola over each half-period. The **phase is 0..255** (a full turn)
and the **output is −1024..+1024**, i.e. fixed-point with `1024 == 1.0`. A value scaled by an
amplitude *A* is `wp_isin(phase) * A / 1024`. This one function drives the aurora crests, the
nebula plasma field, and the firefly sway.

### Time is the only clock

Every animated style samples `get_ticks()` (milliseconds since boot) and computes positions as a
**pure function of time (and a fixed per-element seed)**. There is no velocity to integrate and no
mutable animation state: at any instant *t* the frame is fully determined, so two repaints at the
same tick are identical. Motion is simply the clock advancing between repaints. This is why the
styles never accumulate drift or flicker, and why they survive the desktop idling and resuming.

---

## When does it repaint? — the animation gate

A still desktop must not burn CPU recompositing. `compositor_run()` (`compositor.c:3018`) forces a
repaint **only while an animated style is selected**, at a per-style interval:

```c
uint32_t wp_iv = (wp_st == WP_STYLE_SHOOTINGSTAR) ? 70u : 120u;   // ~14 fps vs ~8 fps
if ((wp_st == WP_STYLE_STARFIELD || wp_st == WP_STYLE_SHOOTINGSTAR ||
     wp_st == WP_STYLE_AURORA || wp_st == WP_STYLE_NEBULA ||
     wp_st == WP_STYLE_LUCES) && now - wp_anim_ms >= wp_iv) {
    wp_anim_ms = now;
    redraw = 1;
}
```

The shooting star needs ~14 fps to read as smooth streaking motion; the softer styles look fine at
~8 fps. For `CLEAN`, `NIGHTFALL` and `FLAT` this branch never fires, so the desktop composites only
in response to real events (a window moving, a click) — no needless work, no wasted power.

---

## Per-style techniques

All five animated styles share one trick that keeps them from looking pasted-on: **each lit pixel
blends from the *local* sky-gradient colour toward the effect's hue**, by an intensity that fades to
zero at the edges. Because the starting point is the sky right there (not a fixed base colour), the
faded margins are invisible and the effect melts into the night.

- **Estrellas / `STARFIELD`.** Each star's luminance rides a triangle wave phased by its own seed,
  sampled from `get_ticks()` (~2.5 s period). The brightest moments swell a star from 1 px to 2 px,
  so the sky gently shimmers.
- **Meteoros / `SHOOTINGSTAR`.** One meteor every ~3.6 s, lasting ~0.9 s (`PERIOD=3600`,
  `STREAK=900` ms). The launch point is varied by an integer hash of the period index so successive
  meteors don't retrace the same line. The streak is a ~44 px line on a 2:1 down-left diagonal with
  a fat glowing 3 px head fading to a 1 px tail.
- **Aurora / `AURORA`.** Three curtains (green, violet, teal — Nyx palette) whose crest *y* is a sum
  of two slow `wp_isin` terms; each column fades from a bright core to nothing over the curtain's
  half-height, so they hang as soft vertical light.
- **Nebula / `NEBULA`.** A cheap **low-frequency plasma field** — the sum of three `wp_isin` samples
  of `x`, `y` and `x+y` — picks out big blobs; only the field's *upper* range glows (`f > 1100`), so
  most of the sky and all the stars show through and the clouds read as diffuse purple-magenta gas
  rather than a hard plasma. Confined to the upper 62 % of the screen.
- **Luces / `LUCES`.** 16 lilac orbs, each with its own LCG-seeded lane, phase, speed and sway
  amplitude. An orb rises up the screen (`y` wraps over `fh+48`), sways horizontally via `wp_isin`,
  and pulses in brightness; it is drawn as a small disc (`dx²+dy² ≤ 16`) blending toward a lilac core
  `rgb(214,194,248)` with a soft radial falloff, and skips any position over the moon.
- **Ondas / `ONDAS`.** Concentric moonlight ripples expand out from the moon. Four thin rings ride the
  same clock at even radius offsets (`base + k·maxr/4`, wrapping), each drawn by an integer midpoint-
  circle (`bg_ripple_ring`) and fading as it grows, so a few soft lilac rings always hang around the
  moon — like light on still water. Each ring pixel blends from the local sky toward lilac and clips
  off-screen; rings inside the moon disc are skipped.

---

## The Wallpaper picker

Opened from the **desktop right-click menu → "Wallpaper"** (`compositor.c:767`), which creates a
400×440 window bound to `wallpaper_win_draw` / `wallpaper_win_click`. The picker shows:

- **A style grid** — all `WP_STYLE_COUNT` styles as a **2-row × 4-column** grid of buttons, the
  selected one highlighted.
- **A base-colour palette** — 11 swatches (index 0 is the brand purple), each rendered as a **live
  mini-scene preview**: `wallpaper_draw_preview` paints a shrunk version of the *actual* current
  style in that hue (gradient + moon + stars for the night styles), not a flat block, so switching
  style live-updates every swatch.

A click sets `g_style` or `g_wallpaper` and the compositor repaints; the choice takes effect
immediately across the whole desktop.

---

## Adding a new style

1. **Declare it.** Add a `WP_STYLE_*` before `WP_STYLE_COUNT` in `wallpaper_win.h`.
2. **Name it.** Add its display name to `style_names[]` in `wallpaper_win.c` (this is what the picker
   button shows).
3. **Render it.** In `draw_background()` (`compositor.c`): if it should sit on the night scene, add it
   to the night-style condition (the `style != …` guard around the moon/stars), then add a
   `if (style == WP_STYLE_YOURS) { … }` block. Use `wp_isin` for any motion, sample `get_ticks()` for
   time, and **blend from the local sky gradient** so faded edges stay invisible. Keep any bright
   elements clear of the moon.
4. **Animate it (if live).** Add the style to the repaint gate in `compositor_run()` and pick an
   interval (70 ms for fast motion, 120 ms otherwise).
5. **Expose it.** The picker grid sizes itself from `WP_STYLE_COUNT`, so a new style appears
   automatically once steps 1–2 are done (the grid now holds nine over three rows of four; a 10th would
   start a fourth row and want a taller Wallpaper window).

Keep it in the Nightfall palette — purple, moonlit, calm. See also *Add a wallpaper style* in
`docs/ARCHITECTURE.md`.

---

## Conventions & constraints

- **Integer only.** No floats anywhere — the kernel is `-mno-sse`. Use `wp_isin` and fixed-point
  (`/1024`) scaling.
- **Deterministic frames.** Position must be a pure function of `get_ticks()` (+ a fixed seed). Never
  keep mutable per-frame animation state; it would drift and could flicker.
- **Blend from the local sky.** Layer effects by interpolating from the sky-gradient colour at that
  pixel toward the effect hue, fading to zero at the edges — never paint a hard-edged shape.
- **Respect the moon.** Skip effect pixels that fall within the moon's radius so nothing draws over
  it.
- **Stay on-brand.** Purple / lilac / teal-violet, a calm moonlit night — the Nyx identity, never a
  generic desktop.
