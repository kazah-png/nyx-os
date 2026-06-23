# NyxOS Status Report

## Version
**v2.1.1** (released 2026-06-22)

## Completed This Release

### Network Stack
- [x] TCP implementation: full connection state machine (CLOSED, SYN_SENT, SYN_RCVD, ESTABLISHED, FIN_WAIT1/2, CLOSE_WAIT, LAST_ACK, TIME_WAIT)
- [x] TCP API: `tcp_connect`, `tcp_send`, `tcp_recv`, `tcp_close`
- [x] TCP checksum (pseudo-header)
- [x] ARP cache with static entries
- [x] RTL8139 driver fixes:
  - [x] CONFIG1 register wake-up (disable PM)
  - [x] 256-byte aligned RX/TX buffers
  - [x] 16-bit RCR register writes
  - [x] CAPR updates via 8-bit writes + ISR ROK clear
  - [x] MAC re-read after soft reset

### GUI & Desktop
- [x] Window compositor: 16 windows max, z-order, title bars with close buttons, drag-by-title-bar, per-window clipping
- [x] VGA 8x16 bitmap font (256 glyphs from Linux kernel font_8x16.c)
- [x] PC speaker driver: PIT channel 2 square wave, `speaker_beep`, `speaker_play_note`
- [x] GUI demo: mouse-driven paint program (6 colors, Bresenham lines)
- [x] PS/2 mouse: IRQ12 handler, 3-byte packet decode, cursor position API

### Shell Commands Added
- [x] `desktop` - window compositor demo
- [x] `fonttest` - font rendering sample
- [x] `gui` - mouse paint demo
- [x] `beep [freq] [ms]` - PC speaker tone
- [x] `play` - demo melody
- [x] `tcptest <ip> <port>` - TCP HTTP GET test
- [x] `setip <ip> <mask> <gw>` - static IP config

## Known Issues
- RTL8139 RX path doesn't update ISR/CAPR in QEMU user-mode networking (works on real hardware / tap / socket backends)
- TCP handshake requires static ARP entry in QEMU user-mode
- EXT2 filesystem is still a stub (read-only not implemented)

## Next Priorities
1. **Sound Blaster 16** -- DMA/IRQ audio for DOOM sound effects
2. **Real EXT2 read support** -- currently stub only
3. **Window compositor polish** -- keyboard focus, input routing, resize/minimize
4. **Initramfs + ELF loader** -- run userspace binaries

## Build/Test
```bash
make -C kernel
tools/build.sh
qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -serial stdio -display none -nic user,model=rtl8139
```

## Testing Commands
```
setip 10.0.2.15 255.255.255.0 10.0.2.2
desktop
fonttest
gui
beep 440 200
play
tcptest 10.0.2.2 80
```