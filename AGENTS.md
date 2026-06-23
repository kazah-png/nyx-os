# NyxOS — Agent Context

## Goal
Evolve NyxOS into a functional 32-bit x86 kernel with filesystem, networking, shell, process multitasking, GUI, and multimedia.

## Build & Test
- Cross-compiler: `i686-elf-gcc` at `cross/bin/` (WSL)
- Build: `make -C kernel` → `kernel/nyx-kernel.bin` (~70 KB)
- ISO: `tools/build.sh` → `NyxOS.iso`
- QEMU test (serial): `qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -serial stdio`
- QEMU test (interactive): `qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -nographic`
- QEMU with net: `-nic user,model=rtl8139`
- Known working: boots fully, reaches shell, modules load successfully
- Repo: `https://github.com/kazah-png/nyx-os`

## Releases
- v1.0.0 — Base kernel
- v1.1.0 — Ramdisk VFS + shell commands
- v1.2.0 — Real networking (RTL8139, ARP, IP, UDP, ICMP/ping)
- v2.0.0 — Clean slate: removed all hacking/offensive code
- v2.0.1 — DOOM game integration (VGA mode 13h, doomgeneric, shell command)
- v2.1.0 — Preemptive multitasking, interrupt-driven I/O, pipe support, diff command
- v2.1.1 — Full TCP stack, RTL8139 fixes, GUI/window compositor, PC speaker, bitmap font

## Architecture
### Boot flow
1. `boot.asm` (multiboot header, GDT, paging off) → `kernel_main`
2. `init_gdt()` → `init_idt()` → `init_isr()` → `init_irq()`
3. `init_memory()` (bitmap allocator) → `init_paging()` (identity-map 1:1 up to 4MB) → `init_heap()` (16MB heap)
4. `vbe_init()` → `init_timer(1000)` → `init_keyboard()` (interrupt-driven)
5. `init_process()` → `ensure_idle_process()` → `init_syscalls()` → `init_vfs()` → `init_load_modules()`
6. `init_ext2()` → `init_net()` → `tcp_init()` → `init_background_tasks()`
7. `mouse_init()` → `speaker_init()` → register IRQ handlers → `sti` → `launch_shell()`

### Critical constraints
- Paging identity-maps only the first 4 MB. Any static data, BSS, or heap beyond 4 MB causes triple-fault.
- Heap is 16 MB (bumped from 1 MB for DOOM + framebuffer). Physical allocator bitmap supports up to 512 MB RAM.
- `_kernel_end` symbol in `linker.ld` marks kernel BSS boundary for memory manager.
- Serial (`init_serial()`) is a stub — only used via direct `outb(0x3F8, ...)`. VGA text mode (0xB8000) is primary console.
- Interrupts are ENABLED (`sti`). Timer (IRQ0), keyboard (IRQ1), and mouse (IRQ12) are interrupt-driven with proper PIC unmasking and EOI.
- Cooperative multitasking via `switch_context`/`create_task_stack` (assembly) + background task callbacks + IRQ scheduler tick.

## Kernel structure
```
kernel/
  boot.asm        — Multiboot header, GDT, start
  kernel.c        — Main kernel, shell (launch_shell), 40+ command handlers
  kernel.h        — All typedefs, extern declarations, constants
  linker.ld       — Linker script with _kernel_end
  memory.c        — Bitmap physical page allocator
  heap.c          — 16 MB kernel heap (kmalloc/kfree)
  paging.c        — Identity-mapped page tables (4 MB)
  process.c       — Process table, background tasks, schedule
  switch.asm      — Context switch (switch_context, create_task_stack)
  vfs.c           — Ramdisk VFS (directories, files, pipe support)
  ext2.c          — EXT2 filesystem stub (not functional)
  timer.c         — PIT timer (1000 Hz, interrupt-driven)
  keyboard.c      — PS/2 keyboard (US/ES layouts, AltGr)
  screen.c        — VGA text mode (80x25)
  serial.c        — Serial stub
  syscall.c       — Syscall handler table
  net.c           — Network stack init
  ethernet.c      — Ethernet frame handler
  arp.c           — ARP cache + requests
  ip.c            — IPv4 send/receive with checksum
  icmp.c          — ICMP echo (ping)
  udp.c           — UDP sockets
  rtl8139.c       — RTL8139 NIC driver (PCI, I/O, MMIO, TX/RX rings)
  dhcp.c          — DHCP client (DISCOVER → OFFER → REQUEST → ACK)
  tcp.c/tcp.h     — Full TCP connection state machine (8 connections)
  vbe.c           — Bochs VBE framebuffer (up to 1024x768x32)
  fb.c            — Framebuffer abstraction (put_pixel, fill_rect, blit)
  mouse.c         — PS/2 mouse driver (IRQ12, 3-byte packets)
  gui.c           — GUI paint demo (mouse drawing)
  font.c/font.h   — VGA 8x16 bitmap font (256 glyphs)
  compositor.c/h  — Window compositor (16 windows, z-order, drag, close)
  speaker.c/h     — PC speaker driver (PIT channel 2, beep, melody)
  vga_graphics.c  — VGA mode 13h (DOOM)
  doom_nyxos.c    — DOOM generic NyxOS port
  gdt.c/idt.c/isr.c/irq.c — x86 descriptor tables
```

## Shell commands (40+)
| Command       | Description |
|---------------|-------------|
| help          | Show all commands |
| clear         | Clear screen |
| nyxfetch      | System info with ASCII logo |
| fastfetch     | Alias for nyxfetch |
| echo          | Print text or `echo text > file` |
| ls/cd/pwd     | Filesystem navigation |
| cat/touch/mkdir/rm/cp/mv | File operations |
| which/head/tail/grep/sort/wc/find/tree | File utilities |
| write/diff    | Write file / compare two files |
| env/export    | Environment variables |
| history       | Command history |
| ps/kill/mem   | Process management |
| ifconfig/dhcp/ping/setip | Network commands |
| tcptest       | TCP HTTP GET test |
| date/uname/version | System info |
| layout        | Switch keyboard layout (us/es) |
| hexdump       | Dump memory |
| crash         | Trigger kernel panic |
| reboot        | Reboot via 0x64/0xFE |
| doom          | Run DOOM game (requires doom1.wad) |
| mode          | Set VBE video mode |
| gui           | GUI paint demo with mouse |
| fonttest      | Test bitmap font rendering |
| desktop       | Window compositor desktop |
| beep/play     | PC speaker tones and melodies |

## Shell features
- Tab completion for command names
- Environment variable expansion: `echo $VARNAME`
- Command history (last 10, duplicates filtered)
- `echo > file` redirection support
- Pipe | support (`cmd1 | cmd2` with temp file)

## Network stack
- RTL8139: PCI I/O BAR, MMIO, TX/RX rings, link detection, CONFIG1 fix
- ARP: Cache with request/reply, static entries, periodic cleanup
- IP: Send/receive with header checksum, local delivery
- UDP: Raw datagram send, port-based listener registration
- ICMP: Echo request/reply for ping
- **DHCP**: Full client (DISCOVER → OFFER → REQUEST → ACK), auto-config
- **TCP**: Full connection state machine (SYN, ESTABLISHED, FIN, RST handling)
- Single static IP: 10.0.2.15/24 or DHCP-assigned

## GUI subsystem
- Bochs VBE framebuffer (1024x768x32, LFB at 0xE0000000)
- PS/2 mouse (IRQ12, absolute positioning)
- VGA 8x16 bitmap font from Linux kernel (256 glyphs)
- Window compositor: z-ordering, title bars, close buttons, drag-to-move
- GUI paint demo: 6 colors, Bresenham line drawing

## Next features to add
- Real EXT2 read support
- Sound Blaster 16 DMA/IRQ audio driver
- ELF loader + initramfs for userspace binaries
- Compositor polish (resize, minimize, keyboard input routing)
- ATA/IDE disk driver
- Real-time clock (RTC) driver
