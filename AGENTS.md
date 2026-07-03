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
| v5.0.0 | Full GUI application suite: Text Editor (file open/save, cursor nav), Image Viewer (test pattern, zoom/pan), Sound Test (PC speaker + SB16 sine/square/sweep). Brighter wallpaper gradient (sky-blue + grass). 8 desktop icons. **All placeholders replaced with real apps.** No crashes, zero warnings in build. |
| v5.1.0 | Stability and bugfix release: TSS struct alignment fix (IST pointers shifted 4 bytes early → triple fault on double fault/NMI), GDT limit correction, APIC/PIC IRQ masking fix (PIC left fully masked when APIC active), switch_to_user_process inline asm operand reversal fix, VGA 8x16 font data corruption fix (marker bytes inserted in each glyph → garbage text in GUI). QEMU display changed to sdl for Windows compatibility. |
| v5.3.0 | Login system: boot animation (~5s, NyxOS-themed, 23-step progress bar), credential storage on EXT2 (/etc/passwd), framebuffer login screen with keyboard input, default user nyx/nyx, fallback when no EXT2 disk, login failure → reboot, success → launch desktop. |
| v5.4.0 | Audit/hardening pass: fixed clean-build break (`sha256.h` pulled host `<stdint.h>`, conflicting with kernel int types); build is warning-free again (0 warnings from ~160 — pointer-truncation casts routed through `uintptr_t`, keyboard keymap over-initializers trimmed, dead code removed). **Security:** ring-3 syscall boundary hardened — user pointers validated against the canonical user half, and userspace now gets small integer fds via a translation table instead of raw (forgeable) kernel VFS handles (closed an info-leak + arbitrary-kernel-r/w reachable via `exec`). Panic handler now prints faulting RIP/CS/ring/error. |
| v5.5.0 | **Ring-3 userspace now actually runs.** `exec <elf>` loads an ELF64, enters ring 3, services syscalls, and returns to the shell on exit. Fixed the full chain: (1) EFER.SCE was never enabled → `syscall` #UD'd; (2) LSTAR pointed at the low link address, unmapped under the user CR3 → set it to the PML4[511] higher-half alias; (3) `syscall_entry` clobbered RAX/RBX (and truncated user RSP) *before* SAVE_REGS — rewrote it to save all user GPRs first using RIP-relative ops (no scratch reg), with `kernel_rsp` stored as a higher-half alias; (4) added `copy_from_user`/`copy_to_user` that walk the user page tables to physical (masking bits 51:12 — `~0xFFF` alone kept the NX bit and produced non-canonical → #GP); (5) return via `iretq` (NASM's bare `sysret` is the 32-bit form and truncates RSP); (6) `switch_to_user_process` uses a small setjmp/longjmp (`ku_setjmp`/`ku_longjmp`) so exit() unwinds to the caller instead of halting. Verified: init.elf prints, getpid/malloc(sbrk)/snprintf/write/exit all work, control returns to boot. |
| v5.6.0 | **Userspace cleanup + heap bug.** (1) `exec` now reaps the process on exit: `free_page_directory` (paging.c) walks the user half (PML4[0..510], skips the 511 kernel mirror) freeing every leaf frame + table; `reap_user_process` (process.c) also frees the kernel/context stack (`kernel_stack-4096` is the kmalloc base — `proc->stack` is a *middle* pointer, so the old `destroy_process` `kfree(proc->stack)` corrupted the heap; fixed). Verified clean over repeated execs. (2) **Heap bug:** `kmalloc` sent every request ≤ `SLAB_MAX_OBJ` (1024) to the slab, but the slab caches only went up to 512, so `slab_alloc` returned NULL for 505..1016 B and kmalloc returned NULL *instead of falling back to the heap* — every mid-size allocation failed (e.g. VFS file writes: `/home/user/welcome.txt` was silently empty). Fixed: kmalloc falls back to heap on slab miss; two header magics (`_SLAB`/`_HEAP`) so kfree routes correctly regardless of size; added the missing 1024 slab class. (3) `SYS_WRITE` now routes file fds (≥3) through the ufd table to `vfs_write`, so userspace can create/write files (not just stdout/stderr). init.elf exercises the full file-I/O path (open/create/write/read/close) as a regression test. |
| v5.7.0 | **Per-fd offsets / streaming file I/O.** Added `vfs_pread`/`vfs_pwrite` (offset-aware; pwrite grows + zero-fills without discarding existing content). The userspace fd table (`ufd_*` in syscall.c) now tracks a byte offset per open fd, advanced by each `SYS_READ`/`SYS_WRITE`, so ring-3 reads stream across calls and writes append instead of overwriting. Kernel-internal `vfs_read`/`vfs_write` (offset 0) are unchanged — no blast radius on the shell's one-shot open→read→close callers. Verified: init.elf writes "Hello, " + "userspace file I/O!" in two calls and streams it back in two reads. |
| v5.7.1 | **Fd-based I/O routes to EXT2 mounts (persistent files).** Previously only the path-based helpers (`vfs_write_file`/`vfs_cat_file`) reached a mounted FS; the fd-based path (`vfs_open`/`read`/`write`/`close` — used by `echo >`, head/tail/grep/sort/wc/diff, the editor, and userspace) hit the ramdisk and never persisted, and `/mnt` isn't even a ramdisk node. Now `vfs_open` detects a mount path (`vfs_find_mount`) and returns a transient **mount-backed node** preloaded from the FS (`read_file`); `vfs_write`/`vfs_pwrite` flush the whole node back via `write_file`; `vfs_close` frees it. Added a node free-list so these per-open nodes don't exhaust the 128-node pool. Verified across reboot: a file written to `/mnt` via `vfs_open`+`vfs_write` is read back on the next boot with the same disk image. (Ext2 write engine + persistence were already solid; this wires the fd layer to it.) |
| v5.7.2 | **Fd-based `readdir` on EXT2 mounts (file-manager can browse disk).** `vfs_open` on a mount path now probes `readdir` (ext2_readdir returns −1 for non-dirs) — if it's a directory it loads all entries once into the mount-backed node; `vfs_readdir` serves them by index and `vfs_close` frees them. Completes the mount FS surface: fd-based file read/write **and** directory listing on `/mnt` (path-based `ls`/`mkdir`/`rm` already worked and persist). Verified: `vfs_open("/mnt")` + `vfs_readdir` loop lists `lost+found/`, files, and created subdirs with correct dir/file typing. |
| v5.7.5 | **GUI stability fixed — the `KERNEL_BASE+3` crash is dead** (and with it the timer/scheduler blocker). Root cause, found with QEMU `-d int` (which, unlike serial `printf`, doesn't perturb the Heisenbug): `irq_common` did the CR3 switch using **rax/rbx as scratch BEFORE `SAVE_REGS`**, so on return the interrupted code resumed with a corrupted RBX (= `KERNEL_BASE`); if it then computed a call/jump from rbx it landed at `KERNEL_BASE+3` → `#UD`. Intermittent because it only bit when the IRQ interrupted an instruction about to use rax/rbx — so it showed up under IRQ load (typing in the GUI terminal, or the timer firing). Fix: move `SAVE_REGS` to the very top of `irq_common` (and `isr_common`), same as the earlier `syscall_entry` fix. Also: boot stack 16 KB→128 KB and moved *above* the page tables (a deep compositor call chain could have overflowed into them); removed the `mpos=`/`.` serial debug spam. **Verified visually** (QEMU SDL + monitor `sendkey`/`screendump`): login → desktop, 12+ terminal commands incl. a full DHCP lease, zero crashes. The timer/scheduler can now be revisited (they crashed via this exact bug). |
| v5.7.4 | **Full network stack: DNS + TCP + HTTP work.** Building on v5.7.3's DHCP, fixed the rest of the host↔network byte-order bugs (all masked until the IP checksum was fixed): **DNS** — `qdcount`/`flags` stored LE (query had 256 questions → dropped), returned/parsed IPs and the timeout loop (used dead `tick_count` → infinite hang); **ARP** — sender/target IPs written high-byte-first (asked "who has 3.2.0.10"), `sleep()` hang, host-order static gateway entry with the wrong MAC (removed); **TCP** — `offset_flags`/`window`/checksum stored LE, checksum pseudo-header + RX `offset_flags` in the wrong order, and `payload_len` counted Ethernet padding (60-byte min frame added 2 phantom bytes → wrong ack → RST). **Routing** — `ip_send` now sends off-subnet traffic via the gateway (was ARP-ing internet IPs directly) and skips loopback when auto-selecting (was using lo's IP for the TCP pseudo-header). **http_get** waits for the handshake before sending. `parse_ip` + all IP prints normalized to network order (`ifconfig`/DHCP show 10.0.2.15, not reversed). Verified end-to-end via `-object filter-dump` pcap: DHCP→lease, DNS resolves, and `http_get(1.1.1.1:80)` returns a parsed **HTTP 301** with body. (A real HTTP server works; a captive-portal/proxy in some envs returns a non-HTTP page.) |
| v5.7.3 | **Networking works — DHCP completes** (`dhcp` command gets a lease from QEMU slirp). The stack was 100% broken; fixed 7 bugs, chief among them a byte-order bug that killed *every* packet: (1) **IP header checksum stored little-endian** — `ip->checksum = ip_checksum()` wrote the network-order value LE-reversed, so slirp silently dropped all our IP frames (found via `-object filter-dump` pcap: checksum verified to 0x649b not 0xffff). (2) **`ip_send` ARP-resolved broadcast** (255.255.255.255) → hung; now uses the broadcast MAC directly. (3) **RTL8139 RX ring** re-derived the read offset from `CAPR+16` but wrote it back without `−16` (asymmetric, misaligned after packet 1) and never checked BUFE — rewrote to track the offset itself, check the empty bit, and init CAPR=0xFFF0. (4) **RCR** missing MXDMA/RXFTH fields. (5) **DHCP magic cookie** stored byte-reversed and (6) at BOOTP offset 240 instead of 236. (7) **DHCP poll loop** had no inter-poll delay (finished before replies arrived; timer is dead so it busy-waits on port 0x80). Verified end-to-end: DISCOVER→OFFER→REQUEST→ACK, lease 10.0.2.15. **Still TODO** (same byte-order class of bug, now unmasked): DNS (`hdr->flags`/return IP host-order), TCP/HTTP (seq/ack/ports/checksum), and the `ifconfig` IP *display* order (shows 10.0.2.15 reversed). |

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
10. `bootsplash_update(23, …)` → `bootsplash_clear()` → `auth_setup()` → `login_screen()`
11. `compositor_init()` + `compositor_run()` (GUI desktop) OR `launch_shell()` (fallback)

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
  editor_win.c/h  — Text Editor window (file open/save, cursor nav, scroll, click)
  imageview_win.c/h — Image Viewer window (test pattern, zoom/pan)
  soundtest_win.c/h — Sound Test window (PC Speaker + SB16 buttons)
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
- Desktop icons (Files, Terminal, Editor, Viewer, DOOM, Settings, Paint, Sounds)
- Terminal emulator window (scrollback, Tab completion, command execution)
- File Manager window (VFS browsing, directory navigation, file preview)
- Text Editor window (file open/save, cursor navigation, scroll, click-to-position)
- Image Viewer window (color-bar test pattern with zoom/pan, grid overlay)
- Sound Test window (PC Speaker beep/melody/alarm + SB16 sine/square/sweep) 

## v5.1.0 — Stability and bugfix release
- **TSS struct alignment fix**: `kernel/kernel.h` — three reserved `uint32_t` fields (SS0/SS1/SS2 at offsets 28-39) were incorrectly merged into a single `uint64_t`, shifting all IST pointers 4 bytes early. Any exception using an IST (double fault, NMI) read a garbage stack pointer → triple fault.
- **GDT limit correction**: `kernel/gdt.c` — limit was `(6*8+16)-1=63` (too large), corrected to `(5*8+16)-1=55`.
- **APIC/PIC IRQ masking fix**: `kernel/irq.c` — when APIC is active, the legacy PIC stays fully masked. Prevents duplicate interrupt delivery.
- **switch_to_user_process inline asm fix**: `kernel/switch.asm` — AT&T operand order was reversed (source/destination swapped in `mov`), causing exec'd user processes to crash.
- **QEMU display fix**: `run.ps1` — changed from `-display gtk` to `-display sdl` (gtk is broken on Windows).
- **VGA 8x16 font data corruption fix**: `kernel/font.c` — font_data array had marker bytes (the glyph's own index) inserted at position `(index % 16)`, shifting all pixel data. Replaced with correct IBM VGA 8x16 ROM font. GUI text now renders legibly.
- **Boot loop resolved**: kernel boots to desktop in GUI mode and runs stably for extended periods. No double faults, page faults, or crashes observed after running 60+ seconds.
- Build: zero errors, zero warnings. Kernel confirmed stable in both serial and GUI (SDL) mode.

## v5.3.0 — Login system
- **Boot animation**: Full-screen NyxOS-themed splash (~5s) with dark gradient, ASCII bird logo, twinkling stars (60 points), animated spinner (8 rotating dots), progress bar (23 steps with percentage and status text). Fades to black before desktop.
- **Auth backend** (`auth.c/h`): Credential storage in `/etc/passwd` on EXT2 (`username:salt:hex_hash`). First boot creates default user `nyx`/`nyx`. Fallback to hardcoded default when no EXT2 disk. Hash uses djb2 with per-user salt.
- **Login screen** (`login.c/h`): Framebuffer-based login panel (centered dark panel with title, username/password fields). Keyboard input via direct `kbd_buffer` polling with `cli`/`sti`. Password masked with `*`. Enter switches fields or submits. No cursor or mouse to avoid page faults.
- **Integration**: `bootsplash_clear()` → `auth_setup()` → `login_screen()` → compositor in kernel_main. Login failure → reboot via `outb(0x64, 0xFE)`. Success → launch NyxOS Desktop.
- **Keyboard buffer exposed**: `kbd_buffer`, `kbd_head`, `kbd_tail` made non-static in `keyboard.c` for direct polling from login.c.

## Next features to add
- **Userspace polish** (ring-3 works as of v5.5.0; exec reaps on exit + file
  read/write from ring 3, v5.6.0; per-fd offsets / streaming, v5.7.0):
  - Each read/write is still capped at one ≤4 KB `kmalloc` bounce per syscall
    (loop in userspace for more). `vfs_pwrite` reallocs on every grow — fine for
    small files, revisit for large/append-heavy workloads.
  - `copy_from_user`/`copy_to_user` assume user physical pages fall in the 64 MB
    identity map; fine for small programs, revisit if RAM use grows.
  - Only one user process runs at a time (`switch_to_user_process` is blocking).
    Real concurrency needs the preemptive scheduler (below).
- **Preemptive scheduler** — now UNBLOCKED (the `irq_common` #UD bug is fixed in
  v5.7.5). Remaining work, both now tractable with visual QEMU verification:
  1. **Enable the timer.** The LAPIC timer is masked and the PIT (ISA IRQ0) is
     delivered on I/O APIC **pin 2** (QEMU ACPI interrupt-source override), but
     the boot only unmasks pin 0 — so `tick_count` never advances and `sleep()`/
     uptime are dead. `ioapic_redirect_irq(2, 32, 0)` + `ioapic_unmask_irq(2)`
     delivers ticks. (Previously this surfaced the `irq_common` #UD; that's fixed
     now, so re-test carefully in the GUI.)
  2. **Reinstate `irq_scheduler_tick` round-robin.** It was written and is sound
     (`init_task_stack` already builds an `irq_common`-compatible frame; filter
     to `page_directory==NULL` kernel threads; idle picked last) — it's in git
     history from the v5.5-era attempt. Re-enable behind `sched_enable()` and
     verify the GUI stays stable with two demo kernel threads.
- File Manager: copy/paste, drag-and-drop files
- Network: DHCP/DNS/TCP/HTTP all work (v5.7.3–v5.7.4). Possible follow-ups: TCP
  retransmit/large multi-segment responses, `ping` (ICMP) verification, and DNS
  reliability under high latency (bump the poll budget). Debug the wire with
  `qemu -object filter-dump,id=f0,netdev=n0,file=x.pcap` + a pcap parser.
- SMP (multi-core) bringup via APIC IPI
- Page fault advanced features (COW, demand paging, swapped pages)
- EXT2: mount-backed dirs (fd-based `readdir`/`mkdir`/`unlink` on `/mnt`), and
  flush-on-close instead of flush-on-every-write for large files. (Fd-based
  file read/write on `/mnt` now persists — v5.7.1.)

## Security notes (v5.4.0)
- Ring-3 syscall args are validated: `user_ptr_ok`/`user_str_ok` in `syscall.c`
  reject any pointer outside the canonical user half `[0x1000, 0x800000000000)`,
  so a user process can't name a kernel address as a syscall buffer.
- Userspace fds are opaque small integers (`UFD_BASE`+slot) mapped to internal
  VFS handles in `syscall.c`; kernel VFS pointers are never exposed to or
  accepted from ring 3.
- Still open: passwd salt is derived deterministically from the username
  (`gen_salt_hex` = HMAC(secret, username)) rather than random-per-user; the
  on-disk XOR of `/etc/passwd` is obfuscation, not encryption (the stored
  values are already PBKDF2 hashes). Fine for a hobby OS; note it's not a real
  secret-at-rest scheme.
