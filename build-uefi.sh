#!/usr/bin/env bash
# build-uefi.sh — compile the kernel, then emit a UEFI-bootable NyxOS ISO.
# Native Linux replacement for build-uefi.ps1 (which wrapped WSL).
# The regular build.sh makes a legacy-BIOS ISO; this one targets modern UEFI
# machines (GRUB x86_64-efi, GOP framebuffer). Flash NyxOS-uefi.iso to a USB
# with dd or Rufus (DD mode).
# See docs/REAL_HARDWARE.md for firmware settings and the physical-boot checklist.
# Usage: ./build-uefi.sh [--clean|-c]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CLEAN_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --clean|-c) CLEAN_ARGS=(--clean) ;;
        -h|--help)
            echo "Usage: $0 [--clean]"
            echo "  --clean, -c   clean before building"
            exit 0
            ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

if [ -t 1 ]; then
    CYAN='\033[36m'; YELLOW='\033[33m'; GREEN='\033[32m'; RED='\033[31m'; NC='\033[0m'
else
    CYAN=''; YELLOW=''; GREEN=''; RED=''; NC=''
fi

echo -e "${CYAN}=== Building NyxOS (UEFI) ===${NC}"

# 1) Compile kernel + BIOS ISO (also confirms 0-warn build)
if [ ${#CLEAN_ARGS[@]} -gt 0 ]; then
    "$ROOT/build.sh" "${CLEAN_ARGS[@]}"
else
    "$ROOT/build.sh"
fi

# 2) Wrap freshly built kernel into a GRUB-EFI ISO
echo -e "${YELLOW}[*] Creating UEFI ISO...${NC}"

if ! bash "$ROOT/tools/mkuefi.sh" "$ROOT/NyxOS-uefi.iso"; then
    echo -e "${RED}[FAIL] UEFI ISO not produced (need grub-efi-amd64-bin + mtools)${NC}"
    exit 1
fi

ISO="$ROOT/NyxOS-uefi.iso"
if [ -f "$ISO" ]; then
    SIZE_KB=$(awk "BEGIN {printf \"%.1f\", $(stat -c%s "$ISO")/1024}")
    echo -e "${GREEN}[OK] NyxOS-uefi.iso (${SIZE_KB} KB)${NC}"
    echo -e "${GREEN}     Flash to USB with 'dd if=NyxOS-uefi.iso of=/dev/sdX bs=4M status=progress' (or Rufus DD mode). Secure Boot must be OFF.${NC}"
else
    echo -e "${RED}[FAIL] UEFI ISO not produced (need grub-efi-amd64-bin + mtools in WSL)${NC}"
    exit 1
fi
