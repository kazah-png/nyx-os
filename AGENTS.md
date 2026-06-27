# NyxOS — Agent Context

## Goal
Evolve NyxOS into a functional x86_64 kernel with filesystem, networking, shell, process multitasking, GUI, multimedia, and audio.

## Build & Test
- Cross-compiler: host GCC with `-m64` (or `x86_64-elf-gcc` for cross)
- Build (PowerShell): `.\build.ps1` → `kernel/nyx-kernel.bin`
- Build (make): `make -C kernel` → `kernel/nyx-kernel.bin`
- Run (GUI): `.\run.ps1`
- Run (serial debug): `.\run.ps1 -Mode serial`
- Run (with net): `.\run.ps1 -Mode net`
- Run (with disk): append `-hda ext2-test.img` to QEMU args
- ISO: `tools/build.sh` → `NyxOS.iso`
- Repo: `https://github.com/kazah-png/nyx-os`

## Releases
| Version | Description |
|---------|-------------|
| v1.0.0 | Base kernel |
| v1.1.0 | Ramdisk VFS + shell commands |
| v1.2.0 | Real networking (RTL8139, ARP, IP, UDP, ICMP/ping) |
| v2.0.0 | Clean slate: removed all hacking/offensive code |
| v2.0.1 | DOOM game integration (VGA mode 13h, doomgeneric, shell command) |
| v2.1.0 | Preemptive multitasking, interrupt-driven I/O, pipe support, diff command |
| v2.1.1 | Full TCP stack, RTL8139 fixes, GUI/window compositor, PC speaker, bitmap font |
| v2.2.0 | GUI auto-boot: NyxOS Desktop launches at startup instead of text shell |
| v2.3.0 | Real EXT2 read support: VFS mount layer, auto-mount at /mnt, ls/cd/cat on ext2 |
| v2.4.0 | Sound Blaster 16 DMA/IRQ audio driver (sb16play command, DMA buffer fix, auto-init) |
| v3.0.0 | ELF userspace loader, initramfs, per-process paging, ring 3 execution, int 0x80 syscalls |
| v3.1.0 | RTC driver, more syscalls (open/read/close/getpid/sbrk/fsize/exec), libc (printf/malloc/snprintf), initramfs auto-boot, desktop polish (right-click, settings, wallpaper) |
| v4.0.0 | Full x86_64 native (long mode, 4-level paging, syscall/sysret, ELF64, ring 3), clean 32-bit dead code removal, docs corrected |
| v4.1.0 | Higher-half kernel mapping (PML4[256] mirror), user/kernel page-table isolation (user has no identity mapping), CR3 switching in ISR/IRQ/syscall entries, scheduler uses next_cr3 |
| v4.2.0 | NX bit (No-Execute) + SMEP (Supervisor Mode Execution Prevention), PAGE_NX added to user stack/heap/data, Local APIC + I/O APIC init, CPUID detection, IST (Interrupt Stack Table) for double fault (#8) and NMI (#2), repo cleanup (README x86_64, .gitignore, AGENTS.md) |

## Architecture
### Boot flow
1. `boot.asm` (multiboot header, GDT, paging off) → `kernel_main`
2. `init_gdt()` → `init_idt()` → `init_isr()` → `init_irq()`
3. `init_memory()` (bitmap allocator) → `init_paging()` (identity-map 1:1 up to 4MB) → `init_heap()` (16MB heap)
4. `vbe_init()` → `vbe_set_mode(1024, 768, 32)` → `fb_init()` → `init_apic()` → `init_timer(1000)` → `init_keyboard()`
5. `init_process()` → `ensure_idle_process()` → `init_syscalls()` → `init_vfs()` → `init_load_modules()`
6. `init_ext2()` → `init_net()` → `tcp_init()` → `init_background_tasks()`
7. `mouse_init()` → `speaker_init()` → `sb16_init()` → register IRQ handlers → `sti`
8. `initramfs_load()` → `initramfs_boot()` (create initramfs files in VFS)
9. Auto-detect EXT2 on ATA disk → mount at `/mnt`
10. `compositor_init()` + `compositor_run()` (GUI desktop) OR `launch_shell()` (fallback)

### Critical constraints
- Paging identity-maps 64 MB (16 page tables). Any static data, BSS, or heap beyond 64 MB causes triple-fault.
- Heap is 16 MB (bumped from 1 MB for DOOM + framebuffer). Physical allocator bitmap supports up to 512 MB RAM.
- `_kernel_end` symbol in `linker.ld` marks kernel BSS boundary for memory manager.
- Serial (`init_serial()`) is a stub — only used via direct `outb(0x3F8, ...)`. VGA text mode (0xB8000) is primary console.
- Interrupts are ENABLED (`sti`). Timer (IRQ0), keyboard (IRQ1), mouse (IRQ12), and SB16 (IRQ5) are interrupt-driven with proper PIC unmasking and EOI.
- Cooperative multitasking via `switch_context`/`create_task_stack` (assembly) + background task callbacks + IRQ scheduler tick.
- Per-process paging: `alloc_page_directory` clones kernel identity-mapped page tables with supervisor-only PTEs; user pages mapped via `map_page_dir` with PTE `U/S=1` and PDE `U/S=1` for ring-3 access.
- ELF loader maps code at ELH entry point (e.g. 0x10000), user stack at 0xD0000000 (one page, 4096 bytes).
- Syscalls via `syscall`/`sysret`: rax=number, rdi/rsi/rdx/rcx/r8/r9=args; handlers in `syscall_handler_c` dispatch table (exit=0, write=1, print=2, open=3, read=4, close=5, getpid=6, sbrk=7, fsize=8, exec=9).
- TSS I/O map base set to full TSS size → all I/O ports denied from ring 3.
- build.ps1: WSL cross-compilation; QEMU 11.x with `-audiodev dsound,id=audio0 -device sb16,audiodev=audio0`

## Kernel structure
```
kernel/
  boot.asm        — Multiboot header, GDT, start
  kernel.c        — Main kernel, shell (40+ command handlers), desktop launch
  kernel.h        — All typedefs, extern declarations, constants
  linker.ld       — Linker script with _kernel_end
  memory.c        — Bitmap physical page allocator
  heap.c          — 16 MB kernel heap (kmalloc/kfree)
  paging.c        — Identity-mapped page tables (4 MB)
  process.c       — Process table, background tasks, schedule
  switch.asm      — Context switch (switch_context, create_task_stack)
  vfs.c           — Ramdisk VFS + mount table (EXT2 passthrough)
  ext2.c/h        — EXT2 filesystem driver (read-only, mountable via VFS)
  ata.c/h         — ATA/IDE PIO disk driver
  timer.c         — PIT timer (1000 Hz, interrupt-driven)
  keyboard.c      — PS/2 keyboard (US/ES layouts, AltGr)
  screen.c        — VGA text mode (80x25) + putchar hook for terminal capture
  serial.c        — Serial stub
  syscall.c       — Syscall handler table
  elf.c/h         — ELF64 loader (validate, parse PT_LOAD, map pages, create user process)
  initramfs.c/h   — Initramfs cpio parser (new-style '070701'), creates files in VFS
  initramfs_data.h — Generated C byte array with embedded cpio archive
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
  compositor.c/h  — Window compositor (32 windows, z-order, drag, resize, close, workspaces)
  terminal_win.c/h — Terminal emulator window (scrollback, Tab completion, command execution)
  fileman_win.c/h — File Manager window (VFS browsing, directory navigation, file preview)
  speaker.c/h     — PC speaker driver (PIT channel 2, beep, melody)
  sb16.c/h        — Sound Blaster 16 DSP driver (DMA, IRQ, mixer, PCM playback)
  doom_nyxos.c    — DOOM generic NyxOS port
  doom_nyxos_sound.c — DOOM sound module (DMX lump loading, channel mixing stubs)
  vga_graphics.c  — VGA mode 13h (DOOM)
  gdt.c/idt.c/isr.c/irq.c — x86 descriptor tables
```

## Shell commands (40+)
| Command       | Description |
|---------------|-------------|
| help          | Show all commands |
| clear         | Clear screen |
| nyxfetch      | System info with ASCII logo |
| echo          | Print text or `echo text > file` |
| ls/cd/pwd     | Filesystem navigation (also on EXT2 via /mnt) |
| cat/touch/mkdir/rm/cp/mv | File operations |
| which/head/tail/grep/sort/wc/find/tree | File utilities |
| write/diff    | Write file / compare two files |
| env/export    | Environment variables |
| history       | Command history |
| ps/kill/mem   | Process management |
| ifconfig/dhcp/ping/setip | Network commands |
| tcptest       | TCP HTTP GET test |
| mount         | Mount EXT2: mount [drive] [part_lba] |
| date/uname/version | System info |
| exec          | Execute ELF binary: exec <file> |
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
| sb16play      | SB16 DMA audio playback: sb16play [freq] [ms] |

## Shell features
- Tab completion for command names
- Environment variable expansion: `echo $VARNAME`
- Command history (last 10, duplicates filtered)
- `echo > file` redirection support
- Pipe `|` support (`cmd1 | cmd2` with temp file)

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
- Window compositor: z-ordering, title bars (min/max/close), drag-to-move, resize, 4 workspaces
- Taskbar with running app buttons, Start menu (12 items), clock
- Desktop icons (Files, Terminal, DOOM, Settings, About, Paint)
- Terminal emulator window (scrollback, Tab completion, command execution)
- File Manager window (VFS browsing, directory navigation, file preview)

## What's new in this session
- Higher-half kernel mapping (x86_64): kernel identity-mapped at both 0x00000000 (PML4[0]) and 0xFFFFFFFF80000000 (PML4[256]); user processes only get higher-half mapping, NO identity mapping; CR3 switching in ISR/IRQ/syscall entries/exits; IDT/GDT/TSS bases use higher-half addresses; scheduler sets next_cr3 instead of direct switch_page_directory
- Desktop polish (Fase 1): fast wallpaper, right-click context menu (New Folder, New File, Refresh, Settings), File Manager toolbar + inline name input, Settings window with Info/Display/Keyboard tabs (resolution buttons, US/ES layout toggle)
- Extended syscalls: open(3), read(4), close(5), getpid(6), sbrk(7), fsize(8), exec(9)
- User-space libc: crt0.asm, syscall.h (static inline wrappers), libc.h/libc.c (mem, string, stdio, stdlib)
- SYS_EXEC: replaces calling process in-place with new ELF via kernel stack frame (popa + iret to ring 3)
- ELF segment copy fix: non-page-aligned PT_LOAD copies data at correct page offset (dst_off = vaddr & 0xFFF)
- Syscall popa bugfix: isr_stubs.asm saves EAX to [esp+28] (saved EAX slot) instead of [esp] (saved EDI slot)
- SYS_EXIT fix: for(;;) hlt instead of sti;hlt to prevent GPF from returning to dead user process
- hello.elf build fix: linked with ld -e _start -Ttext 0x10000 for proper ET_EXEC with valid program headers
- init.elf rewritten to use full libc (printf, malloc, snprintf, free) — boots, prints system info, exits cleanly
- DOOM sound: wired DMX sound lump loading → SB16 playback (single-cycle DMA, 64KB buffer, auto-stop on IRQ)
- Scrollbar in File Manager: vertical scrollbar with proportional thumb, click-to-scroll, arrow key navigation (Up/Down/PgUp/PgDn/Home/End), Enter to open selected file/dir
- Slab allocator (`slab.c`/`slab.h`) for kmalloc objects ≤512 bytes (caches for 32/64/128/256/512 bytes)
- `proc->stack = sp + 112` → `proc->stack = sp` in `init_task_stack`/`init_user_task_stack`

## Session fixes (Jun 2026)
- Fixed triple fault on `sti`: `ioapic_redirect_irq()` in `apic.c` wrote I/O APIC redirection entries **without** the mask bit, implicitly unmasking ALL hardware IRQs 0-15. The PIT timer fired immediately after STI, and the unregistered IRQ handler chain caused a crash → triple fault. Fix: added `ioapic_mask_irq(i)` after routing each IRQ in `init_apic()`.
- Fixed kernel crash in `switch_to_user_process()`: `map_pml4()` at `paging.c:91` stripped the `PAGE_USER` bit from page-table entries (`flags & ~0xFFF` clears lower 12 bits), making all user pages supervisor-only → #PF on first ring-3 instruction. Fix: propagate `PAGE_USER` and `PAGE_NX` from flags explicitly: `(flags & PAGE_USER ? PAGE_USER : 0) | (flags & PAGE_NX)`.
- Restored `switch_to_user_process()` with higher-half trampoline (`switch_to_user_trampoline` in `isr_stubs.asm`): indirect `call rax` through a higher-half address, switches RSP→CR3→RESTORE_REGS→iretq. Inline assembly fixed to AT&T syntax for GCC default dialect. Verified working (reaches ring 3, no crash). Used in `cmd_exec` for direct user-process launch from shell.

## Next features to add
- Paint app with variable brush size + color picker
- Calculator window (basic arithmetic)
- Task Manager window (process list, CPU/memory stats)
- Notepad with file save/open
- EXT2 write support (currently read-only)
