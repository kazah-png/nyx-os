# NyxOS Boot & GRUB

How NyxOS goes from firmware power-on to `[LOGIN] Ready.` — the Multiboot2 hand-off,
the 32-bit → long-mode trampoline, and how the bootable ISO is built. Source of truth:
[`kernel/core/boot.asm`](../kernel/core/boot.asm), [`kernel/linker.ld`](../kernel/linker.ld),
`iso/boot/grub/grub.cfg`, [`build.ps1`](../build.ps1), and `kernel_main()` in
[`kernel/core/kernel.c`](../kernel/core/kernel.c).

## The chain at a glance

```
firmware (BIOS or UEFI)
  └─ GRUB 2 (grub-mkrescue image on the ISO)
       └─ reads /boot/grub/grub.cfg  →  multiboot2 /boot/nyx-kernel.bin
            └─ parses the kernel's Multiboot2 header, sets a graphics mode,
               loads the ELF at 1 MB, jumps to _start with EAX=0x36D76289
                 └─ boot.asm _start (32-bit): PAE → page tables → long mode
                      └─ kernel_main(magic, mboot_ptr)  (64-bit C)
                           └─ init GDT/IDT/ISR/IRQ/serial/SSE, parse MB2 tags,
                              bring up the desktop → login
```

NyxOS keeps GRUB as its bootloader (a deliberate, locked decision): GRUB already
implements Multiboot2, ELF loading, and — crucially for real hardware — UEFI GOP
framebuffer setup, so NyxOS does not ship its own bootloader or EFI stub.

## Multiboot2 header (`boot.asm`)

The kernel advertises **two** Multiboot headers in the `.multiboot` section (the linker
`KEEP()`s it first in `.text` so it lands within GRUB's 32 KB search window):

| Header | Magic | Purpose |
|--------|-------|---------|
| Multiboot **v1** | `0x1BADB002` | compatibility only (flags `0x3` = align modules + memory map) |
| Multiboot **v2** | `0xE85250D6` | **primary** — architecture 0 (i386), checksummed |

The v2 header carries a **framebuffer request tag** (type 5, `1024×768×32`). This asks the
bootloader to set a linear graphics mode and report the framebuffer address/pitch back in a
type-8 tag. It is what makes NyxOS render on **real UEFI hardware via GOP** instead of poking
the QEMU-only Bochs VBE registers — see [REAL_HARDWARE.md](REAL_HARDWARE.md). `1024×768×32`
is chosen for its clean 4096-byte pitch (`width × 4`) on virtually all hardware. A required
end tag (type 0) closes the header.

At entry GRUB has placed its magic in `EAX` (`0x36D76289` for v2, `0x2BADB002` for v1) and a
pointer to the boot-information structure in `EBX`; `_start` saves both immediately.

## 32-bit → long mode trampoline (`boot.asm`)

`_start` runs in 32-bit protected mode (GRUB leaves us there) and climbs to 64-bit long mode:

1. **`cli`**, save `EAX`/`EBX` (magic + boot-info pointer).
2. **Enable PAE** (`CR4.PAE`, bit 5) — required for long mode.
3. **Build page tables** in `.bss` (zeroed first):
   - `PML4[0] → PDPT[0] → PD`, and the **PD identity-maps 0–128 MB using 2 MB pages**
     (`0x83` = present | writable | page-size).
   - `PML4[0]` is also mirrored into **`PML4[511]`** to provide a higher-half kernel window
     at `0xFFFF_FF80_0000_0000+` (the low identity map is what actually executes; the kernel
     ELF is linked at 1 MB, below).
4. **Load `CR3`**, set **`EFER.LME`** (MSR `0xC0000080`, bit 8), enable **`CR0.PG`** (bit 31).
5. **`lgdt`** the 64-bit GDT and far-jump `0x08:.long_mode` into 64-bit code.
6. In 64-bit: load the data segments (`0x10`), set `RSP = stack_top`, zero all GPRs for a
   clean state, put magic in `EDI` and the boot-info pointer in `ESI`, and `call kernel_main`.

### Boot-time memory layout (`.bss`)

```
stack_bottom ── 128 KB kernel stack ── stack_top
pml4_table   (4 KB)
pdpt_table   (4 KB)
pd_table     (8 KB, 2 PDs → 128 MB of 2 MB pages)
```

The stack is 128 KB and sits **before** the page tables on purpose: the GUI compositor's deep
redraw call chains plus interrupt frames once overflowed a smaller stack into the page tables
and silently corrupted them. Keeping the tables after the stack turns an overflow into a
harmless fault instead of table corruption.

### The 64-bit GDT

Five flat descriptors, used for the rest of the system's life:

| Selector | Descriptor |
|----------|------------|
| `0x00` | null |
| `0x08` | kernel code (64-bit, ring 0) |
| `0x10` | kernel data |
| `0x18` | user code (64-bit, ring 3) |
| `0x20` | user data |

(A per-CPU TSS is installed later, in C — see [PROCESS.md](PROCESS.md).)

## Kernel image & load address (`linker.ld`)

`nyx-kernel.bin` is an **ELF64** (`OUTPUT_FORMAT(elf64-x86-64)`, `ENTRY(_start)`), which is
exactly what Multiboot2 loads. Layout:

- Loaded at **`. = 0x100000` (1 MB)** — the conventional load address above the legacy
  low-memory/BIOS region.
- `.text` leads with `KEEP(*(.multiboot))` so the header is found early; `_text_start`/
  `_text_end` bracket **all** code (per-function `.text.*` sections included) so the panic
  backtrace can range-check a return address.
- `.rodata` / `.data` / `.bss` follow. Because the kernel is built `-mcmodel=large`, the script
  also gathers the large-model `.lrodata` / `.ldata` / `.lbss` sections; capturing `.lbss` is
  essential so the allocator's own metadata (`page_refcount[]`, `page_pinned[]`) falls **before**
  `_kernel_end` and gets reserved rather than handed back out as free RAM.

## GRUB configuration & building the ISO

`iso/boot/grub/grub.cfg` is minimal:

```
set timeout=5
set default=0
insmod all_video
menuentry 'NyxOS' {
    multiboot2 /boot/nyx-kernel.bin
    module2 /boot/nyx-kernel.bin nyxkernel.bin
    boot
}
```

`insmod all_video` lets GRUB honour the framebuffer request tag. `multiboot2` loads the kernel;
the `module2` line ships the same binary as a named module (available via the MB2 modules tag).

[`build.ps1`](../build.ps1) produces the bootable image:

1. `make` (under WSL) compiles and links the kernel → `kernel/nyx-kernel.bin`, printing
   `[OK] 0 warnings` (the build **surfaces** warnings — a non-zero count is a regression).
2. It regenerates `grub.cfg`, copies `nyx-kernel.bin` into `iso/boot/`, and runs
   **`grub-mkrescue -o NyxOS.iso iso/`** to emit the El-Torito–bootable `NyxOS.iso`.

> **Trap:** plain `make` never rebuilds `NyxOS.iso` — only `build.ps1` does. Booting QEMU after
> a bare `make` silently runs a **stale** kernel. Always `.\build.ps1`. (See the ISO-build note
> in the project memory.)

Boot it with `qemu-system-x86_64 -cdrom NyxOS.iso -m 512` (BIOS/SeaBIOS), or under OVMF for the
UEFI path (`-drive if=pflash,...` with `OVMF_CODE`/`OVMF_VARS`). Real-hardware USB imaging (Rufus)
and the UEFI/GOP specifics live in [REAL_HARDWARE.md](REAL_HARDWARE.md).

## `kernel_main()` — early bring-up (`kernel.c`)

`kernel_main(uint64_t magic, void* mboot_ptr)` is the C entry. In order:

1. Save `magic`/`mboot_ptr`; `init_screen()`; **`uptime_mark_boot()`** to stamp the boot
   wall-clock from the RTC (the single honest uptime source — see [PROCESS.md](PROCESS.md)).
2. Bring up the CPU tables and I/O: **GDT → IDT → ISR → IRQ → serial → SSE/FPU**.
3. **Parse the Multiboot information** by `magic`:
   - `0x2BADB002` (v1): read basic `mem_lower`/`mem_upper`.
   - `0x36D76289` (v2): walk the tag list at `mboot_ptr + 8` until the type-0 terminator:
     - **type 1** — boot command line. A **`selftest`** token runs the offline KAT battery
       before login (used by CI — see [TESTING.md](TESTING.md)); **`rotate=90|180|270`** rotates
       the framebuffer for portrait-mounted panels. Normal boots pass no command line.
     - **type 4 / type 6** — memory info / memory map (total RAM, reserved regions).
     - **type 8** — the framebuffer GRUB actually set (address, pitch, width/height/bpp), the
       reply to the header's request tag.
4. Hand off to memory init, the scheduler, drivers, auth, and the compositor, ending at the
   login screen (`[LOGIN] Ready.`).

## Command-line cheatsheet

| Cmdline token | Effect |
|---------------|--------|
| *(none)* | normal interactive boot to login |
| `selftest` | run the self-test battery, print `SELFTEST-SUMMARY passed=N failed=0`, halt (CI) |
| `rotate=90` \| `180` \| `270` | rotate the framebuffer for a portrait panel |

## See also

- [ARCHITECTURE.md](ARCHITECTURE.md) — kernel subsystem overview
- [REAL_HARDWARE.md](REAL_HARDWARE.md) — UEFI/GOP boot on physical hardware
- [PROCESS.md](PROCESS.md) — GDT/TSS, scheduling, uptime
- [MEMORY.md](MEMORY.md) — paging and the physical/virtual layout after boot
- [TESTING.md](TESTING.md) — the `selftest` KAT battery
