# Changelog

NyxOS keeps a **detailed, per-version changelog in [`AGENTS.md`](AGENTS.md)** —
every version, down to each patch release, has an entry describing exactly what
changed, why, and how it was verified. That file is the authoritative history.

For downloadable, tagged builds see the
[GitHub Releases](https://github.com/kazah-png/nyx-os/releases) page (each release
attaches a bootable `NyxOS-<version>.iso`).

## Eras

- **v6.x — current.** In-OS toolchain (a ported TinyCC that self-hosts), package
  manager (`xbm`), a cleaner desktop, a userland approaching daily-driver quality,
  and performance/visual sandboxes.
- **v5.9.x — previous.** The mature GUI-OS era (Selene browser with TLS, desktop
  apps, games, audio). **v5.9.0 is the LTS release.**
- **v5.8.x and earlier.** Kernel foundations: fork/exec/pipes, the scheduler, the
  network and TLS stack, filesystem persistence, and the image decoders.

See [`AGENTS.md`](AGENTS.md) for the full, version-by-version detail.
