#!/usr/bin/env bash
# build.sh — compile the kernel and emit a BIOS-bootable NyxOS ISO.
# Native Linux replacement for build.ps1 (which wrapped WSL).
# Usage: ./build.sh [--clean|-c]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CROSS="$ROOT/cross/bin"
CLEAN=0

for arg in "$@"; do
    case "$arg" in
        --clean|-c) CLEAN=1 ;;
        -h|--help)
            echo "Usage: $0 [--clean]"
            echo "  --clean, -c   run 'make clean' before building"
            exit 0
            ;;
        *) echo "Unknown argument: $arg" >&2; echo "Try '$0 --help'" >&2; exit 1 ;;
    esac
done

# Prefer cross toolchain if present (Windows/WSL artifact), else host gcc -m64
MAKE_ARGS=()
if [ -d "$CROSS" ]; then
    export PATH="$CROSS:$PATH"
else
    # Host GCC on Arch/CachyOS (gcc 14+/16) defaults to -fstack-protector, which
    # makes freestanding userland objects reference __stack_chk_fail. The Makefile
    # was written for a GCC that doesn't default to it (CI's Ubuntu gcc), so
    # force it off when using host gcc. Harmless if already off.
    MAKE_ARGS+=(CC="gcc -fno-stack-protector")
fi

# Colours — disabled if not a tty
if [ -t 1 ]; then
    CYAN='\033[36m'; YELLOW='\033[33m'; GREEN='\033[32m'; RED='\033[31m'
    DIM='\033[2m'; NC='\033[0m'
else
    CYAN=''; YELLOW=''; GREEN=''; RED=''; DIM=''; NC=''
fi

echo -e "${CYAN}=== Building NyxOS Kernel ===${NC}"

if [ "$CLEAN" -eq 1 ]; then
    echo -e "${YELLOW}[*] Cleaning...${NC}"
    make -C "$ROOT/kernel" clean
fi

echo -e "${YELLOW}[*] Compiling kernel...${NC}"

# Capture build output so we can surface warnings even on success
set +e
BUILD_OUT=$(make -C "$ROOT/kernel" "${MAKE_ARGS[@]}" 2>&1)
BUILD_RC=$?
set -e

# Surface warnings/errors (previously hidden behind a green build)
WARNS=$(printf '%s\n' "$BUILD_OUT" | grep -E 'warning:|error:' || true)
if [ -n "$WARNS" ]; then
    WARN_COUNT=$(printf '%s\n' "$WARNS" | grep -c -E 'warning:|error:' || true)
    echo -e "${YELLOW}[WARN] ${WARN_COUNT} compiler warning(s):${NC}"
    printf '%s\n' "$WARNS" | while IFS= read -r line; do
        echo -e "  ${DIM}${line}${NC}"
    done
else
    if [ "$BUILD_RC" -eq 0 ]; then
        echo -e "${GREEN}[OK] 0 warnings${NC}"
    fi
fi

if [ "$BUILD_RC" -ne 0 ]; then
    echo -e "${RED}[FAIL] Build failed (exit $BUILD_RC)${NC}"
    printf '%s\n' "$BUILD_OUT"
    exit 1
fi

KERNEL_BIN="$ROOT/kernel/nyx-kernel.bin"
if [ -f "$KERNEL_BIN" ]; then
    SIZE_KB=$(awk "BEGIN {printf \"%.1f\", $(stat -c%s "$KERNEL_BIN")/1024}")
    echo -e "${GREEN}[OK] nyx-kernel.bin (${SIZE_KB} KB)${NC}"
else
    echo -e "${RED}[FAIL] nyx-kernel.bin not found!${NC}"
    exit 1
fi

# Build ISO for 64-bit kernel boot
echo -e "${YELLOW}[*] Creating bootable ISO...${NC}"

if ! command -v grub-mkrescue >/dev/null 2>&1; then
    echo -e "${YELLOW}[WARN] grub-mkrescue not found — skipping ISO (apt install grub-common grub-pc-bin xorriso mtools)${NC}"
    exit 0
fi

mkdir -p "$ROOT/iso/boot/grub"
cat > "$ROOT/iso/boot/grub/grub.cfg" <<'GRUBCFG'
set timeout=5
set default=0
insmod all_video
menuentry 'NyxOS' {
    multiboot2 /boot/nyx-kernel.bin
    module2 /boot/nyx-kernel.bin nyxkernel.bin
    boot
}
GRUBCFG

cp -f "$KERNEL_BIN" "$ROOT/iso/boot/"
if grub-mkrescue -o "$ROOT/NyxOS.iso" "$ROOT/iso/" >/dev/null 2>&1; then
    ISO="$ROOT/NyxOS.iso"
    SIZE_KB=$(awk "BEGIN {printf \"%.1f\", $(stat -c%s "$ISO")/1024}")
    echo -e "${GREEN}[OK] NyxOS.iso (${SIZE_KB} KB)${NC}"
else
    echo -e "${YELLOW}[WARN] ISO creation failed (grub-mkrescue error)${NC}"
    exit 1
fi
