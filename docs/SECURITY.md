# NyxOS Security Model

NyxOS is an experimental, from-scratch operating system, not audited software — but
security is a design axis, not an afterthought. This document describes the mechanisms
the kernel actually implements (each claim maps to code in the tree), and is honest
about the limits. Cryptography has its own document, [CRYPTO.md](CRYPTO.md); the network
stack, [NETWORK.md](NETWORK.md).

## Privilege separation

- **Ring 0 / ring 3.** The kernel runs in ring 0; every userspace program runs in ring 3
  and reaches the kernel only through the `syscall`/`sysret` gate (57 syscalls).
- **Per-process address spaces.** Each process has its own 4-level page directory
  (`alloc_page_directory`). A process cannot name, let alone touch, another process's or
  the kernel's private memory — those mappings simply do not exist in its tables.
- **User / kernel page-table isolation.** Kernel data is not mapped into the user half of
  any address space. Interrupt, IRQ, and syscall entry switch `CR3` to the kernel page
  tables on the way in and restore the process's on the way out (`isr_stubs.asm`,
  `syscall.c`).

## Memory protection

- **NX (no-execute).** Non-executable pages carry the NX bit (`PAGE_NX`, bit 63 —
  `mm/paging.h`), so data pages cannot be run as code. The shared libc image is mapped
  read-only into every process.
- **SMEP / SMAP.** When the CPU advertises them (`CPUID.7:EBX`), the kernel sets
  `CR4.SMEP` (bit 20) and `CR4.SMAP` (bit 21) on every core (`cpu_apply_smep_smap`). SMEP
  faults if ring 0 ever tries to *execute* a user page; SMAP faults if it *reads/writes*
  one without an explicit `stac`. The kernel never dereferences a user virtual address —
  it walks the user tables to a physical address and uses the identity map — so these are
  a **hardware-enforced audit** of that isolation invariant: a future stray user-VA access
  from ring 0 traps instead of silently succeeding. (QEMU's default `qemu64` CPU does not
  advertise SMEP/SMAP; use `-cpu qemu64,+smep,+smap` or a Haswell+/`max` model to activate
  them.)

## Fault isolation

A fault taken in ring 3 — a page fault, general-protection fault, invalid opcode, a
divide error — **terminates only the offending process** and returns the CPU to the
scheduler (`isr.c`); the kernel and every other process keep running. A genuine kernel
fault instead draws a graphical panic screen with the register state, rather than a silent
hang, so a real bug is diagnosable.

## Safe program loading

Loading an executable is a classic attack surface: the loader parses an attacker-supplied
file and maps it into a fresh address space. Every ELF — from disk, the initramfs, or an
`execve` — passes `elf_validate()` first (`proc/elf.c`), which rejects anything that is not
a little-endian x86-64 `ET_EXEC`, and checks the program-header table is well-formed and
inside the image (`e_phentsize` exactly the struct size; `e_phoff` and
`e_phnum * phentsize` bounded with 64-bit, subtraction-first math so hostile 16-/64-bit
fields cannot wrap past the check). `elf_load_image()` then bounds every `PT_LOAD` segment:

- `p_offset + p_filesz` is checked overflow-safe against the image size before any copy.
- A segment must land in user space; a higher-half `p_vaddr` is rejected, which stops a
  crafted file from grafting mappings into the kernel's own top-level page-table entry.
- A per-segment size ceiling bounds the allocation loop, so one segment cannot exhaust
  every free page.
- Non-executable segments are mapped NX; the entry point must be a canonical user address.

These checks are locked against regression by a rejection self-test (`elf_selftest`) that
mutates each hostile field and confirms the loader says no.

## Parsing untrusted input

The kernel parses a great deal of attacker-influenced data: network packets
(ARP/IPv4/IPv6/ICMP/UDP/TCP, DNS and DHCP replies), image files (BMP/GIF/JPEG/PNG), on-disk
filesystems (ext2, ustar), and text formats (JSON, CSV, INI, URLs, …). The rules that keep
this safe:

- **Overflow-safe bounds.** Length and offset math is done so it cannot wrap; a record
  length or name that runs past its buffer is rejected, not trusted.
- **Iterative and bounded.** The kernel task stack is 4 KB, so parsers avoid deep recursion
  and unbounded stack buffers — they iterate with off-stack state.
- **Verified against hostile input.** Parsers are checked by the self-test battery below,
  including deliberately corrupt inputs (e.g. the ext2 directory scanner is fed a
  zero-length record, a length past the block end, and a 255-byte name at the block edge),
  and several are differentially fuzzed against a reference implementation.

## Authentication

Login passwords are stored as **PBKDF2-HMAC-SHA256** hashes with a per-user random salt and
iteration count (`auth/auth.c`) — never in the clear, and identical passwords across users
yield different hashes. Repeated failures trip a **brute-force lockout** (rate-limiting)
that blocks further attempts for a cooldown window.

## Cryptography

NyxOS ships its own crypto (no external libraries). The Selene browser speaks **TLS 1.2**
with a real trust model; primitives include AES-GCM/CTR/CBC, ChaCha20-Poly1305, the SHA-2
and SHA-3 families, Ed25519, X25519, and NIST P-256/P-384. Secret comparisons are
constant-time, and randomness comes from a CSPRNG. Full detail — including the exact
handshake and certificate handling — is in [CRYPTO.md](CRYPTO.md).

## Correctness as a security property

A bug in a parser or a crypto primitive *is* a security bug, so NyxOS treats correctness as
testable. The kernel carries a **known-answer self-test battery (102 tests)** — run on
every CI build via the `selftest` boot cmdline — covering the crypto primitives, the
encoders/checksums, every hardened parser, the ELF loader's rejection path, and the
filesystem/format code. A build is only green when it prints `SELFTEST-SUMMARY passed=N
failed=0`. CI additionally runs **CodeQL** static analysis; several of the loader's
overflow-safe checks above exist because CodeQL flagged the naive version.

## Non-goals and honest limits

- **Experimental, unaudited, "as is."** NyxOS is hobby software under GPL-2.0 with no
  warranty. Don't run it on hardware or data you can't afford to lose.
- **No verified boot.** The UEFI loader is unsigned, so **Secure Boot must be turned off**
  to boot it. There is no measured/verified boot chain.
- **FPU/SSE state across context switches** is not yet saved (`fxsave`/`fxrstor` —
  tracked as issue #40). It is latent today (the kernel is `-mno-sse` and DOOM is
  fixed-point), but float-heavy *preemptible* userland could observe corruption.
- **Not a multi-tenant or hardened-server target.** The threat model is a single user
  exploring the system, plus robustness against hostile *files and packets* — not a
  hostile local user escalating against a trusted admin.

## Reporting

Found a memory-safety or crypto issue? Please
[open an issue](https://github.com/kazah-png/nyx-os/issues) with a reproduction (a serial
log, a crafted input, or the panic register dump). It's a hobby project — there is no
embargo process, but fixes to real safety bugs are prioritized.
