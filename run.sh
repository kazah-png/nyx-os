#!/usr/bin/env bash
# run.sh — launch NyxOS in QEMU.
# Native Linux replacement for run.ps1 (which handled Windows QEMU paths + WSL).
# Usage:
#   ./run.sh [--mode gui|serial|net|debug] [--sound] [--cpus N]
#   ./run.sh -m serial --cpus 2
# Defaults: --mode gui --cpus 4
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
KERNEL="$ROOT/kernel/nyx-kernel.bin"
ISO="$ROOT/NyxOS.iso"

MODE="gui"
SOUND=0
CPUS=4

# Colours
if [ -t 1 ]; then
    CYAN='\033[36m'; YELLOW='\033[33m'; GREEN='\033[32m'; RED='\033[31m'
    GRAY='\033[90m'; NC='\033[0m'
else
    CYAN=''; YELLOW=''; GREEN=''; RED=''; GRAY=''; NC=''
fi

usage() {
    cat <<EOF
Usage: $0 [options]
  -m, --mode <gui|serial|net|debug>  QEMU display/net mode (default: gui)
      --sound                        enable SB16 audio (also on by default in net mode)
      --cpus <N>                     SMP CPU count (default: 4)
  -h, --help                         show this help
Examples:
  $0                      # GUI desktop, 4 CPUs
  $0 --mode serial        # nographic, serial to stdio
  $0 --mode net --cpus 2  # with user networking + audio
  $0 --mode debug         # extra QEMU debug traces
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -m|--mode) MODE="${2:-}"; shift 2 ;;
        --mode=*) MODE="${1#*=}"; shift ;;
        --sound) SOUND=1; shift ;;
        --cpus) CPUS="${2:-}"; shift 2 ;;
        --cpus=*) CPUS="${1#*=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo -e "${RED}[ERROR] Unknown argument: $1${NC}" >&2; usage >&2; exit 1 ;;
    esac
done

case "$MODE" in
    gui|serial|net|debug) ;;
    *) echo -e "${RED}[ERROR] --mode must be gui, serial, net, or debug (got: $MODE)${NC}" >&2; exit 1 ;;
esac

if ! [[ "$CPUS" =~ ^[0-9]+$ ]] || [ "$CPUS" -lt 1 ]; then
    echo -e "${RED}[ERROR] --cpus must be a positive integer (got: $CPUS)${NC}" >&2
    exit 1
fi

if [ ! -f "$KERNEL" ]; then
    echo -e "${RED}[ERROR] nyx-kernel.bin not found. Run ./build.sh first.${NC}" >&2
    exit 1
fi

# Build ISO if missing (needed for 64-bit kernel boot)
if [ ! -f "$ISO" ]; then
    echo -e "${YELLOW}[INFO] Creating bootable ISO (needed for x86_64 kernel)...${NC}"
    if ! command -v grub-mkrescue >/dev/null 2>&1; then
        echo -e "${RED}[ERROR] grub-mkrescue not found — cannot create ISO (apt install grub-common grub-pc-bin xorriso mtools)${NC}" >&2
        exit 1
    fi
    mkdir -p "$ROOT/iso/boot/grub"
    cat > "$ROOT/iso/boot/grub/grub.cfg" <<'GRUBCFG'
set timeout=5
set default=0
menuentry 'NyxOS' {
    multiboot2 /boot/nyx-kernel.bin
    boot
}
GRUBCFG
    cp -f "$KERNEL" "$ROOT/iso/boot/"
    if grub-mkrescue -o "$ISO" "$ROOT/iso/" >/dev/null 2>&1; then
        echo -e "${GREEN}[OK] ISO created${NC}"
    else
        echo -e "${RED}[ERROR] Failed to create bootable ISO.${NC}" >&2
        exit 1
    fi
fi

if [ ! -f "$ISO" ]; then
    echo -e "${RED}[ERROR] Failed to create bootable ISO.${NC}" >&2
    exit 1
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo -e "${RED}[ERROR] qemu-system-x86_64 not found.${NC}" >&2
    echo -e "${YELLOW}Install QEMU: sudo apt install qemu-system-x86 (https://www.qemu.org/download/)${NC}" >&2
    exit 1
fi

QEMU_ARGS=(
    -cdrom "$ISO"
    -m 512M
    -smp "$CPUS"
    -no-reboot
    -cpu qemu64
)

# Attach ext2 data disk — NyxOS auto-mounts it at /mnt (doom1.wad lives there).
DISK="$ROOT/ext2-test.img"
if [ -f "$DISK" ]; then
    QEMU_ARGS+=(-drive "file=$DISK,format=raw,if=ide,index=0,media=disk")
    echo -e "${GRAY}[disk] /mnt from ext2-test.img (doom1.wad)${NC}"
else
    echo -e "${YELLOW}[disk] ext2-test.img not found; /mnt unavailable, DOOM has no WAD${NC}"
    echo -e "${GRAY}       Run 'make -C kernel disk' to create it.${NC}"
fi

# Pick display backend: sdl if we have a desktop, else headless (CI / SSH).
# Allows ./run.sh --mode gui to still work over SSH by falling back to -display none.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && [ "$MODE" != "serial" ]; then
    DISPLAY_BACKEND="none"
else
    DISPLAY_BACKEND="sdl"
fi

case "$MODE" in
    gui)    QEMU_ARGS+=(-display "$DISPLAY_BACKEND" -serial "file:$ROOT/qemu_serial.txt") ;;
    serial) QEMU_ARGS+=(-nographic) ;;  # -nographic already multiplexes serial to stdio
    net)    QEMU_ARGS+=(-display "$DISPLAY_BACKEND" -serial "file:$ROOT/qemu_serial.txt" -nic "user,model=rtl8139") ;;
    debug)  QEMU_ARGS+=(-display "$DISPLAY_BACKEND" -serial "file:$ROOT/qemu_serial.txt" -d "cpu_reset,int") ;;
esac

# Sound — original ps1 used dsound (Windows); on Linux use PulseAudio with ALSA fallback
if [ "$SOUND" -eq 1 ] || [ "$MODE" = "net" ]; then
    # Prefer PulseAudio; fall back to ALSA if pa not available
    if qemu-system-x86_64 -audiodev help 2>&1 | grep -qw pa; then
        QEMU_ARGS+=(-audiodev "pa,id=audio0" -device "sb16,audiodev=audio0")
    elif qemu-system-x86_64 -audiodev help 2>&1 | grep -qw alsa; then
        QEMU_ARGS+=(-audiodev "alsa,id=audio0" -device "sb16,audiodev=audio0")
    else
        # dsound fallback (unlikely on Linux but keeps parity with ps1)
        QEMU_ARGS+=(-audiodev "dsound,id=audio0" -device "sb16,audiodev=audio0")
    fi
fi

echo -e "${CYAN}=== Launching NyxOS (mode: $MODE) ===${NC}"
echo -e "${GRAY}QEMU: $(qemu-system-x86_64 --version | head -n1)${NC}"
echo -e "${GRAY}Args: ${QEMU_ARGS[*]}${NC}"

exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
