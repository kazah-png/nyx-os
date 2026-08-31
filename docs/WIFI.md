# Wi-Fi (Intel AX200) — scouting notes and plan

NyxOS has **no 802.11 driver yet.** This document scouts what one would take, so the work
can be picked up in honest, staged rungs instead of a single impossible leap. The target is
the radio in the real-hardware machine (see [REAL_HARDWARE.md](REAL_HARDWARE.md)): an **Intel
Wi-Fi 6 AX200** (`8086:2723`, PCIe, class `02:80`). As of this writing NyxOS recognises the
chip in `lspci` (rung M0) and nothing more.

## Why this is hard (and barely verifiable)

Two facts shape the whole plan:

1. **QEMU does not emulate the AX200.** It emulates e1000 / rtl8139 / virtio-net, so none of
   the radio-specific code can be exercised in the normal headless loop. Only rung M0 (device
   identification, pure logic) is KAT-able; everything from M1 on needs the physical card.
2. **The real machine has no serial port.** On-target debugging is blind — the only feedback
   is the on-screen kernel-panic register dump. So each rung must be small and defensive.

Because of this, Wi-Fi is a **long, multi-session arc**, not a one-increment feature. The
loop advances it by scouting + small honest rungs, and never claims a rung works until it is
either KAT-verified (M0) or confirmed on the physical card by the user.

## What a driver needs (iwlwifi bring-up, from the public architecture)

The Intel parts are driven on Linux by `iwlwifi`. The bring-up order for an AX200-class
device is roughly:

1. **PCI detect + MMIO** — find the device, map BAR0 into uncached VA. *NyxOS already has
   this*: `pci_enumerate()` finds it and `map_mmio_range()` maps a high BAR (proven on the
   real controller by the NVMe driver, whose BAR sits the same way above the identity map).
2. **Firmware (ucode) download — the big blocker.** The device runs Intel microcode that is
   NOT in the silicon; the driver must load a binary blob (e.g. `iwlwifi-cc-a0-*.ucode`,
   ~1 MB, a TLV container) into device memory before anything else works. NyxOS has no such
   file and no loader. This needs: shipping the blob in the initramfs, parsing the TLV
   sections, and DMA-ing them in.
3. **NIC access control** — `grab_nic_access()`: poll `CSR_GP_CONTROL` for readiness, take/
   release the hardware, check no command is in flight.
4. **Keep-warm buffer** — a 4 KiB-aligned DRAM buffer whose (physaddr >> 4) is written to
   `KEEP_WARM_ADDR_REG` to keep host DRAM powered for the device's DMA.
5. **DMA rings** — TX / RX / command rings (256-entry, 256-byte-aligned circular buffers) in
   host DRAM, base addresses handed to the device. *NyxOS has the pattern* from the NVMe
   admin/IO queues (ring + doorbell + PRP-style descriptors).
6. **FW alive + host commands** — after the ucode boots it posts an ALIVE notification; from
   there the host configures the device through commands (e.g. `TX_QUEUE_CFG_CMD`), each
   matched to its response by a sequence number on the RX ring.
7. **Scan → associate → data** — active/passive scan for APs, then auth + association, then
   the **WPA2 4-way handshake (EAPOL)** and finally 802.11 data frames bridged to the IP
   stack. This is the mac80211 / wpa_supplicant equivalent — by far the largest part.

## What NyxOS brings vs what's missing

| Piece                        | NyxOS today                                              |
|------------------------------|----------------------------------------------------------|
| PCI enumeration              | ✅ `pci_enumerate()` (finds `8086:2723`)                  |
| High-BAR MMIO mapping        | ✅ `map_mmio_range()` (proven on real NVMe BAR)           |
| DMA rings + doorbells        | ✅ pattern exists in the NVMe driver                      |
| WPA2 crypto (AES-CCMP)       | ✅ have AES-CTR / AES-CMAC / GCM primitives to build CCMP |
| SHA/HMAC/PBKDF2 (PMK/PTK)    | ✅ in the crypto library                                  |
| Firmware blob + TLV loader   | ❌ none — the #1 blocker                                  |
| 802.11 MAC (mgmt/assoc/data) | ❌ none                                                   |
| EAPOL / 4-way handshake      | ❌ none                                                   |
| Regulatory / channel mgmt    | ❌ none                                                   |

So the *low-level plumbing* (PCI, MMIO, DMA) is within reach, but the *radio stack*
(firmware, MAC, supplicant) is a from-scratch build.

## Milestone ladder

- **M0 — identify.** `lspci` names the AX200 (`pci_wifi_name`). ✅ done, KAT-verified.
- **M1 — MMIO + reset.** Map BAR0, read the hardware rev / `CSR_HW_REV`, do the reset+enable
  handshake. *Real HW only* (QEMU has no device); verify by the user reading the on-screen
  print.
- **M2 — firmware.** Ship an AX200 ucode in the initramfs; parse the TLV container; verify the
  section layout with a host-side KAT before ever DMA-ing it.
- **M3 — rings + keep-warm.** Stand up TX/RX/command rings and the keep-warm buffer.
- **M4 — fw alive.** Download the ucode, wait for the ALIVE notification, issue the first host
  command and match its response.
- **M5 — scan.** Passive scan; surface a list of nearby SSIDs.
- **M6 — associate + WPA2.** Auth + assoc + the EAPOL 4-way handshake (reusing NyxOS's AES/
  HMAC), install the pairwise key.
- **M7 — data path.** TX/RX 802.11 data frames, bridge to the existing IP/TCP stack.

The first DOOM-scale sub-goal is **M4 (firmware alive)** — past it, the device is a computer
we can command; before it, it is inert silicon.

## Legal note

`iwlwifi` is GPL-2.0 — its *architecture* informs this plan, but its **code must not be
copied** into NyxOS; the driver is written from scratch. The Intel **firmware** is
proprietary-but-redistributable (`linux-firmware`, with its own license) and would be shipped
as an opaque blob, user-supplied or vendored under its own terms — never rewritten.

## Sources

- [iwlwifi — OSDev Wiki](https://wiki.osdev.org/Iwlwifi)
- [iwlwifi — Linux Wireless documentation](https://wireless.docs.kernel.org/en/latest/en/users/drivers/iwlwifi.html)
