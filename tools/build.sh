#!/bin/bash
# ============================================================
# tools/build.sh - Script completo de compilación NyxOS v1.0.1
# ============================================================
set -e

export NYX_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export TARGET="x86_64-elf"
export PREFIX="$NYX_ROOT/cross"
export PATH="$PREFIX/bin:$PATH"

# Colores
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[+]${NC} $1"; }
warn() { echo -e "${YELLOW}[!]${NC} $1"; }

# 1. Toolchain
build_toolchain() {
    log "Building cross-compiler..."
    mkdir -p "$PREFIX"
    
    if [ ! -f "$PREFIX/bin/$TARGET-gcc" ]; then
        # Binutils
        wget -q https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.gz
        tar xf binutils-2.41.tar.gz
        mkdir -p build-binutils && cd build-binutils
        ../binutils-2.41/configure --target=$TARGET --prefix="$PREFIX" \
            --with-sysroot --disable-nls --disable-werror
        make -j$(nproc) > /dev/null 2>&1
        make install > /dev/null 2>&1
        cd ..
        
        # GCC
        wget -q https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz
        tar xf gcc-13.2.0.tar.gz
        mkdir -p build-gcc && cd build-gcc
        ../gcc-13.2.0/configure --target=$TARGET --prefix="$PREFIX" \
            --disable-nls --enable-languages=c,c++ --without-headers
        make -j$(nproc) all-gcc > /dev/null 2>&1
        make install-gcc > /dev/null 2>&1
        cd ..
        
        log "Cross-compiler built"
    else
        log "Cross-compiler already exists"
    fi
}

# 2. Kernel
build_kernel() {
    log "Building kernel..."
    cd "$NYX_ROOT/kernel"
    make clean 2>/dev/null || true
    make -j$(nproc)
    cp nyx-kernel.bin "$NYX_ROOT/iso/boot/"
    log "Kernel built: nyx-kernel.bin"
}

# 3. Initramfs
build_initramfs() {
    log "Building initramfs..."
    cd "$NYX_ROOT/initramfs"
    
    mkdir -p bin dev etc/tor etc/dnscrypt proc sys tmp opt/nyx
    
    if [ -f /bin/busybox ]; then
        cp /bin/busybox bin/
    else
        warn "busybox not found, downloading..."
        wget -q -O bin/busybox https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox
        chmod +x bin/busybox
    fi
    
    cd bin
    for cmd in sh ls cat cp mv rm mkdir mount umount ps kill; do
        ln -sf busybox "$cmd" 2>/dev/null || true
    done
    cd ..
    
    find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "$NYX_ROOT/iso/boot/initramfs.cpio.gz"
    
    log "Initramfs built"
}

# 4. ISO - CORREGIDA PARA VIRTUALBOX
build_iso() {
    log "Creating ISO for VirtualBox..."
    cd "$NYX_ROOT"

    mkdir -p iso/boot/grub

    # Buscar módulos de GRUB i386-pc
    if [ ! -d iso/boot/grub/i386-pc ]; then
        log "Copying GRUB i386-pc modules..."
        GRUB_DIR=$(find /usr/lib/grub -type d -name i386-pc 2>/dev/null | head -1)
        if [ -d "$GRUB_DIR" ]; then
            cp -r "$GRUB_DIR" iso/boot/grub/
        else
            warn "GRUB i386-pc modules not found. Attempting grub-mkrescue fallback..."
            grub-mkrescue -o NyxOS.iso iso/ 2>/dev/null || {
                warn "Fallback with xorriso (may not boot in VirtualBox)..."
                xorriso -as mkisofs -b boot/grub/i386-pc/eltorito.img \
                    -no-emul-boot -boot-load-size 4 -boot-info-table \
                    -o NyxOS.iso iso/
            }
            return
        fi
    fi

    # grub.cfg
    cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

menuentry "NyxOS 1.0.1" {
    multiboot /boot/nyx-kernel.bin
    module /boot/initramfs.cpio.gz
    module /boot/doom1.wad doom1.wad
    boot
}

menuentry "NyxOS (safe mode)" {
    multiboot /boot/nyx-kernel.bin safe_mode=1
    module /boot/initramfs.cpio.gz
    module /boot/doom1.wad doom1.wad
    boot
}
EOF

    # Download or copy WAD file
    if [ -f "$NYX_ROOT/doom1.wad" ]; then
        cp "$NYX_ROOT/doom1.wad" iso/boot/doom1.wad
        log "WAD file copied (doom1.wad)"
    elif command -v wget &> /dev/null; then
        log "Downloading shareware doom1.wad..."
        wget -q -O iso/boot/doom1.wad "https://www.jbserver.com/doom/doom1.zip" 2>/dev/null || \
        wget -q -O iso/boot/doom1.wad "https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad" 2>/dev/null || \
        wget -q -O iso/boot/doom1.wad "https://files.kapsi.fi/doom/doom1.zip" 2>/dev/null || \
        true
        if [ -f "iso/boot/doom1.wad" ] && [ -s "iso/boot/doom1.wad" ]; then
            log "WAD downloaded"
        else
            rm -f iso/boot/doom1.wad
            warn "Could not download doom1.wad. DOOM will not work."
        fi
    else
        warn "doom1.wad not found and wget not available. DOOM will not work."
    fi

    # Generar ISO con xorriso o grub-mkrescue
    if command -v xorriso &> /dev/null; then
        xorriso -as mkisofs -b boot/grub/i386-pc/eltorito.img \
            -no-emul-boot -boot-load-size 4 -boot-info-table \
            -o NyxOS.iso iso/
        log "ISO created with xorriso: NyxOS.iso"
    else
        grub-mkrescue -o NyxOS.iso iso/ -- -- -boot-info-table -no-emul-boot -boot-load-size 4
        log "ISO created with grub-mkrescue: NyxOS.iso"
    fi
}

# Main
case "$1" in
    toolchain)
        build_toolchain
        ;;
    kernel)
        build_kernel
        ;;
    initramfs)
        build_initramfs
        ;;
    iso)
        build_iso
        ;;
    all)
        build_toolchain
        build_kernel
        build_initramfs
        build_iso
        log "Build complete! ISO: NyxOS.iso"
        ;;
    *)
        echo "Usage: $0 {toolchain|kernel|initramfs|iso|all}"
        echo ""
        echo "  toolchain  - Build cross-compiler"
        echo "  kernel     - Build the kernel"
        echo "  initramfs  - Build the initial ramdisk"
        echo "  iso        - Create bootable ISO"
        echo "  all        - Full build"
        ;;
esac