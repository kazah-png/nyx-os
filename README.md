<div align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:0a0a0a,50:1a1a1a,100:2d2d2d&height=140&section=header&text=NyxOS&fontSize=52&fontColor=00ff9d&animation=fadeIn&fontAlignY=55" />
</div>

<div align="center">
  <strong>Custom 32-bit x86 kernel · C and Assembly · Offensive security research platform</strong>
  <br/><br/>
  <!-- Badges -->
  <a href="https://github.com/kazah-png/nyx-os/releases">
    <img src="https://img.shields.io/github/v/release/kazah-png/nyx-os?style=flat&color=00ff9d&label=release" />
  </a>
  <img src="https://img.shields.io/badge/kernel-80%20KB-00ff9d?style=flat" />
  <img src="https://img.shields.io/badge/arch-i686-00ff9d?style=flat" />
  <img src="https://img.shields.io/badge/status-booting-00ff9d?style=flat" />
  <a href="https://github.com/kazah-png/nyx-os/issues/1">
    <img src="https://img.shields.io/badge/status%20report-View-0d1117?style=flat&logo=github" />
  </a>
</div>

---

## NyxOS — Terminal Preview

```
nyx:root$ nyxfetch

______          \'/
      .-'` .    `'-.    -= * =-
    .'  '    .---.  '.    /|\
   /  '    .'     `'. \
  ;  '    /          \|
 :  '  _ ;            `
;  :  /(\ \
|  .       '.
|  ' /     --'
|  .   '.__\
;  :       /
 ;  .     |            ,
  ;  .    \           /|
   \  .    '.       .'/
    '.  '  . `'---'`. `'
      `'-..._____.-'
    N Y X O S
    N I G H T F A L L
  -------------------------------------
  Kernel:     NyxOS 1.2.0 (Nightfall)
  Arch:       x86 (32-bit)
  Memory:     256 MB total, 252 MB free
  Heap:       1024 KB
  Paging:     Enabled
  Uptime:     42 ticks
  -------------------------------------
```

---

## About

**NyxOS** is a from-scratch 32-bit x86 kernel built as a research platform for low-level systems programming and offensive security. It boots via Multiboot (GRUB-compatible), runs in protected mode with paging, and provides a modular framework for loading kernel-level components.

The project implements core kernel primitives, a custom network stack (RTL8139 NIC + ARP/IP/UDP/ICMP + DHCP), a ramdisk filesystem, and 11 offensive security modules — all written in C and x86 Assembly with no external libraries.

---

## Tech stack

**Languages**

![C](https://img.shields.io/badge/C-00599C?style=flat&logo=c&logoColor=white)
![Assembly](https://img.shields.io/badge/x86%20Assembly-6E4C13?style=flat&logo=assemblyscript&logoColor=white)
![Make](https://img.shields.io/badge/Make-003545?style=flat&logo=gnu&logoColor=white)

**Toolchain**

![GCC](https://img.shields.io/badge/GCC-FFD700?style=flat&logo=gcc&logoColor=black)
![NASM](https://img.shields.io/badge/NASM-009A9E?style=flat)
![LD](https://img.shields.io/badge/LD-00599C?style=flat)
![QEMU](https://img.shields.io/badge/QEMU-FF6600?style=flat&logo=qemu&logoColor=white)

**Kernel primitives**

![GDT/IDT](https://img.shields.io/badge/GDT%2FIDT-4B8BBE?style=flat)
![Paging](https://img.shields.io/badge/Paging-007ACC?style=flat)
![Syscalls](https://img.shields.io/badge/Syscalls-3CB371?style=flat)
![VFS](https://img.shields.io/badge/VFS-FF6347?style=flat)

**Network stack**

![Ethernet](https://img.shields.io/badge/Ethernet-2496ED?style=flat)
![ARP](https://img.shields.io/badge/ARP-00ADD8?style=flat)
![IP](https://img.shields.io/badge/IP-00599C?style=flat)
![UDP](https://img.shields.io/badge/UDP-512BD4?style=flat)
![ICMP](https://img.shields.io/badge/ICMP-FF6600?style=flat)
![DHCP](https://img.shields.io/badge/DHCP-00ff9d?style=flat)

**Crypto**

![MD5](https://img.shields.io/badge/MD5-3776AB?style=flat)
![SHA256](https://img.shields.io/badge/SHA256-CE422B?style=flat)

---

## Shell session preview

```
nyx:root$ help
Available commands:
  help           - Show this help
  version        - Show kernel version
  clear          - Clear the screen
  nyxfetch       - Show system info with ASCII logo
  echo           - Print a line of text
  ls             - List directory contents
  cd             - Change directory
  pwd            - Print working directory
  cat            - Display file contents
  touch          - Create empty file
  mkdir          - Create directory
  rm             - Remove file or directory
  cp             - Copy file
  mv             - Move/rename file
  which          - Show path of a command
  head           - Show first lines of a file
  tail           - Show last lines of a file
  grep           - Search file contents
  sort           - Sort lines of a file
  wc             - Count lines/words/chars
  tree           - Show filesystem tree
  find           - Find files by name
  write          - Write text to file
  history        - Show command history
  ps             - List processes
  mem            - Show memory usage
  modules        - List loaded modules
  ifconfig       - Show network interfaces
  dhcp           - Request IP via DHCP
  ping           - Ping a host
  ...

nyx:root$ echo Hello, NyxOS!
Hello, NyxOS!

nyx:root$ echo test > /readme.txt

nyx:root$ cat /readme.txt
test

nyx:root$ ls /
bin/   dev/   etc/   home/  mnt/   root/  tmp/   usr/   var/

nyx:root$ uname
NyxOS 1.2.0 (Nightfall) i686

nyx:root$ mem
Physical memory: 256 MB total, 252 MB free
Heap size: 1024 KB

nyx:root$ ps
PID  PPID STATE NAME
1    0    1     init

nyx:root$ history
  1  help
  2  echo Hello, NyxOS!
  3  echo test > /readme.txt
  4  cat /readme.txt
  5  ls /
  6  uname
  7  mem
  8  ps
  9  history

nyx:root$ export NAME=NyxOS

nyx:root$ echo $NAME
NyxOS

nyx:root$ touch /hello.txt && write /hello.txt Hello World

nyx:root$ sort /hello.txt
Hello World

nyx:root$ ifconfig
lo:   IP 127.0.0.1    MAC 00:00:00:00:00:00   MTU 65536
eth0: IP 10.0.2.15    MAC 52:54:00:12:34:56   MTU 1500
```

---

## Features

### Boot & initialization
- Multiboot-compliant (GRUB-ready)
- Protected mode 32-bit with GDT setup (code/data segments)
- Paging with identity mapping (first 4 MB)
- Full IDT with exception handlers (0-31) and IRQ remapping (32-47)
- PIT timer at 1000 Hz (polling)
- PS/2 keyboard driver (US and ES layouts + AltGr)

### Memory management
- Bitmap-based physical page allocator (supports up to 512 MB)
- Kernel heap (`kmalloc`/`kfree`) with first-fit + block splitting + coalescing
- Identity-mapped page tables

### Process management
- Static process table (up to 512 processes)
- PID/PPID tracking, process states, stealth levels
- Cooperative multitasking via background task callbacks

### Shell & commands
Built-in command interpreter with **30+ commands**:

| Category | Commands |
|----------|----------|
| **System** | `help`, `clear`, `nyxfetch`, `uname`, `date`, `version`, `reboot`, `crash` |
| **Files** | `ls`, `cd`, `pwd`, `cat`, `touch`, `mkdir`, `rm`, `cp`, `mv`, `head`, `tail`, `grep`, `sort`, `wc`, `find`, `tree`, `write`, `which` |
| **Process** | `ps`, `kill`, `mem`, `modules` |
| **Network** | `ifconfig`, `dhcp`, `ping` |
| **Misc** | `echo`, `env`, `export`, `history`, `hexdump`, `layout` |
| **Offensive** | `scan`, `hash`, `exploit`, `brute`, `memscan`, `shellcode`, `keylog`, `backdoor`, `bdshell` |

**Shell features:**
- Tab completion for command names
- Environment variable expansion (`$VARNAME`)
- Command history (last 10, duplicates filtered)
- `echo text > file` redirection support

### Network stack (real)
- **RTL8139 NIC driver** — PCI detection, I/O BAR, MMIO, TX/RX ring buffers, link detection
- **ARP** — Cache with request/reply, periodic cleanup
- **IPv4** — Send/receive with header checksum, local delivery
- **UDP** — Raw datagram send, port-based listener registration
- **ICMP** — Echo request/reply (ping)
- **DHCP** — Full client (DISCOVER → OFFER → REQUEST → ACK), auto-configures IP/netmask/gateway
- **Interface** — `ifconfig` for status, static IP 10.0.2.15/24 or DHCP-assigned

### Cryptographic subsystem
- SHA-256 (FIPS 180-4 compliant)
- MD5 (RFC 1321 compliant)

### Offensive security modules
Each module is a kernel-loadable component:

| Module | Description |
|--------|-------------|
| **Rootkit** | Hides processes and files matching keywords |
| **Backdoor** | Persistent reverse shell with password auth |
| **Keylogger** | Captures keystrokes to kernel buffer |
| **Injector** | Shellcode injection into processes |
| **Scanner** | Port scanner and vulnerability detector |
| **Exploit loader** | Simulated CVE-based exploit engine |
| **Hydra brute** | Dictionary attack simulation |
| **C2 server** | Command-and-control endpoint |
| **Ransomware** | File encryption demonstration |
| **Cryptominer** | CPU-based miner (simulated) |
| **Reaver** | WPS PIN attack simulation |

---

## Project structure

```
nyx-os/
├── kernel/
│   ├── boot.asm          # Multiboot header, entry point
│   ├── kernel.c          # Main kernel, shell, command dispatcher
│   ├── kernel.h          # Core header (types, structs, inline funcs)
│   ├── gdt.c / gdt_flush.asm
│   ├── idt.c / idt_load.asm
│   ├── isr.c / isr_stubs.asm
│   ├── irq.c
│   ├── memory.c          # Physical memory manager (bitmap allocator)
│   ├── heap.c            # Kernel heap allocator
│   ├── paging.c          # Page tables, virtual memory
│   ├── process.c         # Process management + background tasks
│   ├── switch.asm        # Context switch assembly
│   ├── syscall.c         # System calls
│   ├── vfs.c             # Ramdisk virtual file system
│   ├── dhcp.c            # DHCP client
│   ├── net.c / tcp.c / udp.c / ip.c / ethernet.c
│   ├── arp.c / icmp.c / rtl8139.c
│   ├── crypto.c          # SHA-256, MD5
│   ├── anon.c            # Anonymity subsystem
│   ├── timer.c           # PIT timer
│   ├── keyboard.c        # PS/2 driver (US/ES layouts)
│   ├── screen.c          # VGA text mode
│   └── serial.c          # COM1 debug stub
├── modules/              # 11 offensive security modules
│   ├── modules.h
│   ├── rootkit.c / backdoor.c / keylogger.c / injector.c
│   ├── scanner.c / reaver.c / exploit_loader.c / hydra_brute.c
│   ├── c2_server.c / ransomware.c / cryptominer.c
│   └── Makefile
├── tools/
│   ├── build.sh          # ISO builder (grub-mkrescue)
│   ├── qemu_launch.sh    # QEMU launcher
│   └── qemu_launch.ps1   # Windows QEMU launcher
├── Makefile              # Top-level build
└── README.md
```

---

## Build & run

### Prerequisites

```
i686-elf-gcc / i686-elf-ld (cross-compiler)
nasm  (>= 2.14)
GNU make
QEMU  (for emulation)
```

### Build

```bash
git clone https://github.com/kazah-png/nyx-os.git
cd nyx-os

# Build the kernel
make -C kernel
```

### Run in QEMU

```bash
# Quick test with serial output
qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -serial stdio

# Interactive mode with keyboard
qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -no-reboot -nographic

# With networking (QEMU user-mode)
qemu-system-i386 -kernel kernel/nyx-kernel.bin -m 256M -nic user,model=rtl8139
```

---

## Status

See the full **[NyxOS Status Report](https://github.com/kazah-png/nyx-os/issues/1)** for a detailed feature checklist.

### What works
- ✅ Full boot sequence to shell
- ✅ 30+ shell commands
- ✅ Ramdisk VFS (files, directories)
- ✅ Real networking (RTL8139 + ARP/IP/UDP/ICMP/DHCP)
- ✅ 11 offensive modules
- ✅ Tab completion, env vars, command history
- ✅ MD5/SHA-256 crypto

### What's being built
- 🔄 TCP handshake and data transfer
- 🔄 Interrupt-driven I/O and preemptive multitasking
- 🔄 Pipe | support in shell parser
- 🔄 EXT2 filesystem read
- 🔄 VGA graphics mode

---

## License

This project is provided for educational and research purposes only.

---

<div align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:2d2d2d,50:1a1a1a,100:0a0a0a&height=80&section=footer" />
</div>
