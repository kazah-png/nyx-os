# Booting NyxOS on real hardware (UEFI)

NyxOS normally boots inside QEMU as a legacy-BIOS ISO. Modern laptops are UEFI-only, so
this page covers building a UEFI image, putting it on a USB stick, and what to expect on a
physical machine. The kernel is identical — only the GRUB bootloader platform changes
(`i386-pc` → `x86_64-efi`), and NyxOS draws into the framebuffer GRUB sets up via UEFI GOP.

## Build the UEFI ISO

```
.\build-uefi.ps1
```

This compiles the kernel and wraps it into `NyxOS-uefi.iso` (a GRUB-EFI rescue image).
It needs WSL with `grub-mkrescue`, `xorriso`, and `mtools`. The `x86_64-efi` GRUB modules
come from a system install (`sudo apt install grub-efi-amd64-bin`) if present, otherwise
they are fetched without root via `apt-get download`. Under the hood it just runs
`tools/mkuefi.sh`, which you can call directly on Linux.

## Flash to USB

Write `NyxOS-uefi.iso` to a USB stick with **Rufus** in **DD / Image mode** (not the
Windows "ISO" install mode). The image is a GRUB El-Torito UEFI rescue ISO; DD mode copies
it byte-for-byte so UEFI firmware finds `/EFI/BOOT/BOOTX64.EFI` and boots it.

## Firmware settings

- **Secure Boot: OFF.** NyxOS's GRUB and kernel are unsigned; Secure Boot will refuse them.
- **CSM / Legacy boot: not needed.** NyxOS boots native UEFI now.
- Boot the USB from the firmware boot menu (usually F12 / F9 / Esc at power-on).

## What works on real hardware today

- **Graphics.** GRUB sets a GOP framebuffer (typically 1024×768×32) and hands it to the
  kernel in the multiboot2 framebuffer tag; NyxOS maps it and renders the desktop. This is
  the make-or-break path, and it is validated under QEMU+OVMF (a real UEFI firmware).
- **Boot to login, RAM-only.** The whole system lives in the initramfs — no disk is touched.

The kernel prints, early in boot, the framebuffer geometry and pixel channel layout, e.g.:

```
[INIT] GRUB framebuffer 1024x768x32 pitch=4096 at 0x80000000
[INIT] GRUB fb pixel format: R@16/8 G@8/8 B@0/8
```

NyxOS renders BGRX (`R@16 G@8 B@0`). If a panel reports a different layout the kernel logs
`[WARN] fb is not BGRX...` — so if reds and blues look swapped on screen, that line is why.

## Known limitations on bare metal

These are driver gaps, not bugs — expected on a first physical boot:

- **Keyboard.** NyxOS speaks PS/2 (i8042) only, with no USB-HID stack. A laptop's built-in
  keyboard works **only if** the firmware keeps legacy-USB i8042 emulation on. If it doesn't,
  the machine still boots and draws the login screen — there's just no input yet.
- **Storage.** ATA-PIO only; NVMe SSDs are not driven. Not a problem — NyxOS is RAM-only.
- **Network.** RTL8139 only; there is no 802.11 / Intel AX200 Wi-Fi stack. No networking.
- **Framebuffer must be 32bpp.** Universal on Intel GOP; other depths fall back to the
  QEMU-only VBE path and won't light up a real panel.

## Reference test machine

The current physical target is an Ice Lake laptop: **Intel Core i5-1035G7 / Iris Plus
Graphics (Gen11) / 16 GB LPDDR4 / NVMe SSD / Intel Wi-Fi 6 AX200**. A clean boot there means
UEFI graphics to the login screen; keyboard depends on that firmware's legacy-USB emulation.

## Developer test under QEMU + OVMF

You don't need real hardware to exercise the UEFI path — OVMF is a real UEFI firmware:

```
qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS.fd \
  -cdrom NyxOS-uefi.iso -m 512
```

(Copy `/usr/share/OVMF/OVMF_VARS_4M.fd` to a writable `OVMF_VARS.fd` first.)
