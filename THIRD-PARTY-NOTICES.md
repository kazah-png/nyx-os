# Third-Party Notices

NyxOS as a whole is distributed under the **GNU General Public License, version 2
or later** (see [LICENSE](LICENSE)). It bundles or derives from the third-party
components listed below, each of which remains under its own license and
copyright. This file consolidates that attribution; the per-component license
texts referenced here (`user/tcc/COPYING`, `docs/fonts/OFL.txt`, etc.) are the
authoritative terms.

Nothing in this file weakens the GPL that covers NyxOS's own source — it records
what is *not* original to NyxOS and under what terms it is included.

---

## 1. DOOM engine — `kernel/doom_src/` (+ `user/doomgeneric_nyxos.c`, `user/doom_*.c`)

- **Project:** doomgeneric (<https://github.com/ozkl/doomgeneric>), derived from
  Chocolate Doom, derived from id Software's DOOM.
- **License:** GNU General Public License, version 2 or later.
- **Copyright holders (per-file headers preserved in the tree):**
  - id Software, Inc. — © 1993–1996 (original DOOM)
  - Simon Howard — © 2005–2014 (Chocolate Doom)
  - Raven Software (Heretic/Hexen-derived paths)
  - Andrey Budko (Chocolate Doom contributor)
  - David Flater — © 2008 (`i_allegrosound.c`, `i_sdlsound.c`)
  - Ben Ryves — © 2006 (`mus2mid.c`)
- **Note — no game data:** NyxOS ships the DOOM **engine source only**. It does
  **not** distribute any DOOM `WAD`/game data. DOOM launches only if the user
  supplies their own WAD on the mounted disk at `/mnt/doom1.wad`; the shareware
  and commercial WADs are proprietary to their owners and are intentionally kept
  out of this repository and every release image.

## 2. TinyCC (in-OS C toolchain) — `user/tcc/`

- **Project:** TinyCC / `tcc` by Fabrice Bellard (<https://repo.or.cz/tinycc.git>).
- **License:** GNU Lesser General Public License, version 2.1 — full text in
  [`user/tcc/COPYING`](user/tcc/COPYING). A per-contributor MIT relicensing offer
  is recorded in `user/tcc/RELICENSING`.
- **Copyright:** © 2001–2004 Fabrice Bellard and the TinyCC contributors
  (grischka, Edmund Grimley Evans, Thomas Preud'homme, Shinichiro Hamaji,
  Frédéric Féret, Daniel Glöckner, Timo Lähde, and others; headers preserved).
- **Distinct sub-licenses inside the TinyCC tree:**
  - `user/tcc/lib/armeabi.c` — **MIT License**, © 2013 Thomas Preud'homme.
  - `user/tcc/include/varargs.h` — **Public Domain**.
- NyxOS's porting shims under `user/tcc/nyxshim/` are original NyxOS code
  (GPL-2.0+ by project inheritance).

## 3. Web fonts (documentation site) — `docs/fonts/`

- **Fonts:** Inter, Space Grotesk, JetBrains Mono (latin-subset `.woff2`).
- **License:** SIL Open Font License 1.1 (OFL-1.1) — full text in
  [`docs/fonts/OFL.txt`](docs/fonts/OFL.txt); summary in `docs/fonts/LICENSES.txt`.
- **Copyright:**
  - Inter — © The Inter Project Authors (<https://github.com/rsms/inter>)
  - Space Grotesk — © The Space Grotesk Project Authors
    (<https://github.com/floriankarsten/space-grotesk>)
  - JetBrains Mono — © 2020 The JetBrains Mono Project Authors
    (<https://github.com/JetBrains/JetBrainsMono>)
- Glyph outlines are unmodified; these are the Google Fonts latin subsets,
  self-hosted so the site makes no third-party requests.

## 4. Cryptography reference derivations — `kernel/crypto/`

The bulk of `kernel/crypto/` is original NyxOS code validated against published
RFC/NIST test vectors. The following files follow well-known **public-domain**
reference implementations; they carry no license obligation, but the origin is
recorded here for completeness:

- `curve25519.c`, `ed25519.c` — the **TweetNaCl** reference (Daniel J. Bernstein,
  Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe, Sjaak Smetsers) — public domain.
- `poly1305.c` — **poly1305-donna** (Andrew Moon) — public domain.
- `murmur3.h` — **MurmurHash3** (Austin Appleby) — public domain.
- `siphash.c` / `siphash.h` — **SipHash-2-4** (Jean-Philippe Aumasson, Daniel J.
  Bernstein) — CC0 / public domain.

## 5. Inflate / DEFLATE decoder — `kernel/image/inflate.c`

- Original NyxOS implementation written in the style of Mark Adler's
  public-domain **`puff.c`** (part of zlib). No zlib code is copied; the
  algorithm reference is public domain.

## 6. Console bitmap font — `kernel/drivers/video/font.c`

- The `font_data[256][16]` table is the classic **IBM VGA 8×16 (CP437)** bitmap
  font. Bitmap font *data* of this kind is long-standing prior art and is treated
  as public domain (in the United States, the bitmap rendering of a typeface is
  not itself protected by copyright). No upstream license terms apply.

## 7. `tfetch` bunny fetch — `user/pkg/tfetch/`

- **Inspired by / ported from:** *tfetch* ("tubular fetch") by Parker0312 / "tman.rs"
  (<https://github.com/Parker0312/tfetch>) — the concept (a bunny system-info fetch
  with per-pride-flag colour palettes) and the flag→colour mappings originate there.
- **What NyxOS ships:** an **original C reimplementation** in `user/pkg/tfetch/tfetch.c`.
  The upstream is Rust and is not copied; the bunny is an original ASCII rendition (the
  upstream Unicode-braille art is not reproduced — the console font is CP437), the
  colours are remapped to the terminal's 16-colour ANSI set, and the info fields read
  NyxOS's own `/proc`.
- **Permission:** the upstream repository carries no license file; this port is included
  with the **author's express permission**, granted to the NyxOS maintainer and recorded
  here on 2026-09-04. If you are the author and want the attribution or terms adjusted,
  please open an issue.

---

## License compatibility

NyxOS is licensed **GPL-2.0-or-later**, which is the umbrella for the combined
work. The bundled components are compatible with it:

| Component | License | Compatible with GPL-2.0-or-later |
|-----------|---------|----------------------------------|
| DOOM engine (doomgeneric) | GPL-2.0-or-later | Yes (same license) |
| TinyCC | LGPL-2.1 | Yes (LGPL-2.1 may be used within a GPL work) |
| `armeabi.c` | MIT | Yes (permissive) |
| Fonts | OFL-1.1 | Yes — applies to the font files only, not the code |
| Crypto refs, `puff.c`-style inflate, VGA font | Public domain / CC0 | Yes (no restrictions) |
| `tfetch` port (original C) | GPL-2.0-or-later (concept used with author's permission) | Yes (NyxOS's own code) |

The OFL-1.1 fonts are documentation-site assets, not part of the compiled OS;
they are redistributed unmodified with their license and copyright per OFL §2.

---

*If you believe a component is missing from this file or is misattributed, please
open an issue at <https://github.com/kazah-png/nyx-os/issues>.*
