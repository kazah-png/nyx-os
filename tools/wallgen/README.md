# wallgen — render the NyxOS wallpapers outside the OS

The desktop background is painted from code, not loaded from a file: the compositor
runs `draw_background()` on every frame, with integer arithmetic only, because the
kernel is built `-mno-sse` and has no floating point at all
(see [docs/WALLPAPERS.md](../../docs/WALLPAPERS.md)).

That makes the wallpapers awkward to share — there is no image to copy out. `wallgen`
solves it without forking the renderer: it compiles the compositor's **real** painter
against a `malloc`'d framebuffer instead of the video hardware, so the PNGs it emits
are the pixels NyxOS actually draws.

## How it stays honest

`extract.sh` copies the source straight out of the kernel tree into `bg_extract.inc`:

| From | What |
|---|---|
| `kernel/gui/core/wallpaper_win.h` | `WALLPAPER_COUNT`, the `WP_STYLE_*` enum |
| `kernel/gui/core/wallpaper_win.c` | `palette[]` (base colours), `style_names[]` |
| `kernel/gui/core/compositor.c` | `bg_fill_circle` … `draw_background`, verbatim |

`wallgen.c` supplies only the surface that code calls into — `fb_fill_rect`, `fb_rgb`,
`fb_get_width/height`, `fb_isqrt` (copied from `fb.c`, so circle spans round
identically), `get_ticks`, `wallpaper_base_color`, `wallpaper_style`. Nothing is
reimplemented, so the renderer cannot drift from the OS. If the kernel side moves far
enough that the markers stop matching, `extract.sh` fails loudly instead of emitting a
stale `.inc`.

## Build and run

```sh
cd tools/wallgen
sh extract.sh
cc -O2 -o wallgen wallgen.c
./wallgen OUTDIR [WIDTH HEIGHT [COLOUR_INDEX]]
```

- `./wallgen out` — every style in every base colour at 1920×1080 (132 files)
- `./wallgen out 3840 2160 0` — every style in the brand purple, natively at 4K
- Output is binary PPM; `pack.py WORKDIR OUT.zip` converts to PNG, deletes the
  intermediates (the full set is ~1 GB of PPM) and zips with a README.

1920×1080 is the desktop's design resolution — the moon geometry and the star count
are absolute pixel values, so only that size reproduces the real desktop exactly.
Larger sizes still render natively rather than upscaling: the gradient, aurora,
nebula, rain and mountains are all framebuffer-relative and gain real detail, while
the moon and stars keep their absolute size, so the scene reads wider.

## Still frames of animated styles

Six styles animate, and each is a pure function of the clock, so a still is just a
chosen tick — `still_frame_tick()` picks a moment where the effect reads well. The
one that actually matters is the meteor: it exists for only 900 ms of every 3600 ms
period, so a tick outside that window renders an empty sky.

The shipped result is `docs/NyxOS-Wallpaper-Pack.zip`, served from
[nyxos.cc/support](https://nyxos.cc/support.html).
