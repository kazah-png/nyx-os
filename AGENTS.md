# NyxOS — Agent Context

## Goal
Evolve NyxOS into a functional 32-bit x86 kernel with filesystem, networking, shell, and process multitasking.

## Build & Test
- Cross-compiler: `i686-elf-gcc` at `cross/bin/` (WSL)
- Build: `make -C kernel` → `kernel/nyx-kernel.bin` (~70 KB)
- ISO: `tools/build.sh` → `NyxOS.iso`
- QEMU test (serial): `qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -serial stdio`
- QEMU test (interactive): `qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -nographic`
- Known working: boots fully, reaches shell, modules load successfully
- Repo: `https://github.com/kazah-png/nyx-os`

## Releases
- v1.0.0 — Base kernel
- v1.1.0 — Ramdisk VFS + shell commands
- v1.2.0 — Real networking (RTL8139, ARP, IP, UDP, ICMP/ping)
- v2.0.0 — Clean slate: removed all hacking/offensive code, crypto, anonymity, keylogger. Clean general-purpose OS.
- v2.0.1 — DOOM game integration (VGA mode 13h, doomgeneric adaptations, shell command)

## Architecture
### Boot flow
1. `boot.asm` (multiboot header, GDT, paging off) → `kernel_main`
2. `init_gdt()` → `init_idt()` → `init_isr()` → `init_irq()`
3. `init_memory()` (bitmap allocator) → `init_paging()` (identity-map 1:1 up to 4MB) → `init_heap()` (1MB heap)
4. `init_timer(1000)` → `init_keyboard()` (polling)
5. `init_process()` → `init_syscalls()` → `init_vfs()` → `init_ext2()` → `init_net()`
6. `init_background_tasks()` → `launch_shell()` (polling keyboard loop with background tasks)

### Critical constraints
- Paging identity-maps only the first 4 MB. Any static data, BSS, or heap beyond 4 MB causes triple-fault.
- Heap is 1 MB (not 256 MB — BSS used to overflow 4 MB range).
- Physical allocator uses a 16 KB bitmap (supports up to 512 MB RAM), NOT page_stack at 0xD0000000 (unmapped before paging).

- `_kernel_end` symbol in `linker.ld` marks kernel BSS boundary for memory manager.
- Serial (`init_serial()`) is a stub — only used via direct `outb(0x3F8, ...)`. VGA text mode (0xB8000) is primary console.
- Interrupts are ENABLED (`sti`). Timer (IRQ0) and keyboard (IRQ1) are interrupt-driven with proper PIC unmasking and EOI.
- Cooperative multitasking via `switch_context`/`create_task_stack` (assembly) + background task callbacks.

## Kernel structure
```
kernel/
  boot.asm        — Multiboot header, GDT, start
  kernel.c        — Main kernel, shell (launch_shell), command handlers
  kernel.h        — All typedefs, extern declarations, constants
  linker.ld       — Linker script with _kernel_end
  memory.c        — Bitmap physical page allocator
  heap.c          — 1 MB kernel heap (kmalloc/kfree)
  paging.c        — Identity-mapped page tables
  process.c       — Process table, background tasks, schedule
  switch.asm      — Context switch (switch_context, create_task_stack)
  vfs.c           — Ramdisk VFS (directories, files)
  ext2.c          — EXT2 filesystem stub (not functional)
  timer.c         — PIT timer (1000 Hz polling)
  keyboard.c      — PS/2 keyboard (polling, US/ES layouts)
  screen.c        — VGA text mode (80x25)
  serial.c        — Serial stub
  syscall.c       — Syscall handler table
  net.c           — Network stack init
  ethernet.c      — Ethernet frame handler
  arp.c           — ARP cache + requests
  ip.c            — IPv4 send/receive
  icmp.c          — ICMP echo (ping)
  udp.c           — UDP sockets
  rtl8139.c       — RTL8139 NIC driver (PCI, I/O, MMIO)
  tcp.c           — TCP stub
  gdt.c/idt.c/isr.c/irq.c — x86 descriptor tables
```

## Shell commands
| Command    | Description |
|------------|-------------|
| help       | Show all commands |
| clear      | Clear screen |
| nyxfetch   | System info with ASCII logo |
| echo       | Print text or `echo text > file` |
| ls/cd/pwd  | Filesystem navigation |
| cat/touch/mkdir/rm/cp/mv | File operations |
| which      | Find command location |
| head       | Show first N lines of file |
| tail       | Show last N lines of file |
| grep       | Search file contents |
| sort       | Sort lines of a file |
| wc         | Count lines/words/chars |
| tree       | Recursive directory listing |
| find       | Find files by name |
| write      | Write text to file |
| env/export | Environment variables |
| history    | Command history |
| ps/kill    | Process management |
| mem        | Memory usage |
| ifconfig   | Network interface status |
| dhcp       | Request IP via DHCP |
| ping       | ICMP ping |
| layout     | Switch keyboard layout (us/es) |
| hexdump    | Dump memory |
| date/uname | System info |
| doom       | Run DOOM game (requires doom1.wad) |
| reboot     | Reboot via 0x64/0xFE |

## Shell features
- Tab completion for command names
- Environment variable expansion: `echo $VARNAME`
- Command history (last 10, duplicates filtered)
- `echo > file` redirection support

## Network stack
- RTL8139: PCI, I/O BAR, MMIO, TX/RX rings, link detection
- ARP: Cache, request/reply, periodic cleanup
- IP: Send/receive with checksum, local delivery
- UDP: Send raw UDP datagrams
- ICMP: Echo request/reply for ping
- **DHCP**: Full client (DISCOVER -> OFFER -> REQUEST -> ACK), auto-configures IP/netmask/gateway
- Single static IP: 10.0.2.15/24 (QEMU user-mode default) or DHCP-assigned

## Next features to add
- Real EXT2 read support
- TCP (connections, handshake)
- Sound support (PC speaker / Sound Blaster)
