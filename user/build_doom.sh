#!/bin/bash
# build_doom.sh (v5.9.32) — compile the recovered doomgeneric engine as a NyxOS
# userspace ELF. Engine sources live in kernel/doom_src (freestanding, with their own
# libc shim headers); the NyxOS platform layer is user/doomgeneric_nyxos.c. This first
# cut is a COMPILE PROBE: it compiles every engine translation unit and reports which
# build against the shims, so the remaining work (a userspace stdio backing, then the
# link) is concrete. Run from the repo root under WSL.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/kernel/doom_src"
OUT=/tmp/doombuild; mkdir -p "$OUT"
CC=gcc
CFLAGS="-std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -w \
  -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DNORMALUNIX -I$SRC"

# Exclude the other platforms' backends and the SDL/allegro/GUS sound modules.
EXCLUDE='doomgeneric_sdl|doomgeneric_win|doomgeneric_xlib|doomgeneric_allegro|doomgeneric_emscripten|doomgeneric_linuxvt|doomgeneric_soso|doomgeneric_sosox|i_sdlsound|i_sdlmusic|i_allegrosound|i_allegromusic|gusconf|doom_utils'

ok=0; fail=0; failed_list=""
for f in "$SRC"/*.c; do
    b=$(basename "$f" .c)
    echo "$b" | grep -qE "^($EXCLUDE)$" && continue
    if $CC $CFLAGS -c "$f" -o "$OUT/$b.o" 2>"$OUT/$b.err"; then
        ok=$((ok+1))
    else
        fail=$((fail+1)); failed_list="$failed_list $b"
    fi
done
# the NyxOS platform layer
if $CC $CFLAGS -c "$ROOT/user/doomgeneric_nyxos.c" -o "$OUT/doomgeneric_nyxos.o" 2>"$OUT/plat.err"; then
    ok=$((ok+1)); echo "platform layer: OK"
else
    fail=$((fail+1)); failed_list="$failed_list doomgeneric_nyxos"; echo "platform layer: FAIL"
fi

echo "===== compile probe: $ok OK, $fail failed ====="
echo "failed:$failed_list"
echo "===== first errors from the first few failures ====="
n=0
for b in $failed_list; do
    [ $n -ge 4 ] && break; n=$((n+1))
    echo "--- $b ---"; grep -E "error:" "$OUT/$b.err" 2>/dev/null | head -4
done
