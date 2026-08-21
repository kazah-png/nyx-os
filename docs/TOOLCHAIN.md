# NyxOS In-OS Toolchain

How NyxOS compiles and installs C programs **from inside the running OS**, with no
host tools. The compiler front-end is the `cc` command in
[`kernel/core/kernel.c`](../kernel/core/kernel.c), backed by the vendored TinyCC port
under [`user/tcc/`](../user/tcc/); the package manager is `xbm`, also in
[`kernel/core/kernel.c`](../kernel/core/kernel.c). This complements
[ARCHITECTURE.md](ARCHITECTURE.md) (whole-system view), [PROCESS.md](PROCESS.md)
(how the resulting ELF is spawned) and [FILESYSTEM.md](FILESYSTEM.md) (where sources
and binaries live).

---

## 1. Why this matters

Most hobby operating systems are cross-compiled: you build the programs on Linux and
copy the binaries in. NyxOS instead carries its own **self-hosting C toolchain** — a
real C compiler runs as a NyxOS process, reads `.c` source from the filesystem, and
writes a runnable ELF back to it. You can write a program, compile it, and run it
without ever leaving the OS. The compiler can even compile *its own* source, which is
the definition of self-hosting.

```
edit hello.c   →   cc hello.c -o /mnt/bin/hello   →   spawn /mnt/bin/hello
   (editor)          (in-OS TinyCC)                     (a ring-3 process)
```

---

## 2. `cc` — the compiler

```
cc [-c] [--self-libc] <in.c | in.o ...> [-o out]
```

`cc` drives the vendored **TinyCC** (`user/tcc/`) to turn C into a ring-3 ELF
executable:

- **Compile + link** (default): each `.c` is compiled and the results linked with the
  NyxOS C library into a position-dependent ELF loaded at the userland base. `-o`
  names the output (default `a.out`).
- **`-c` (compile only)**: stop after producing a `.o` object file, for separate
  compilation — pass several `.o` files back to `cc` to link them.
- **`--self-libc`**: link against the toolchain's own bundled libc source
  ([`user/tcc/libc.c`](../user/tcc/libc.c)) instead of the shared `libc.so`, used when
  building the compiler itself and other freestanding pieces.

The C library the produced programs call is [`user/tcc/libc.c`](../user/tcc/libc.c) /
[`libc.h`](../user/tcc/libc.h): a compact freestanding libc (strings, `malloc`,
`printf`/`snprintf`, file and process syscalls) that talks to the kernel through the
`SYS_*` syscall numbers listed in [`kernel/core/kernel.h`](../kernel/core/kernel.h).
Everything is integer/`-mno-sse` clean, matching the rest of the OS.

### Self-hosting

The port reached self-hosting incrementally: TinyCC, compiled in-OS by `cc`, can
compile the TinyCC source again and produce a working `tcc` — the compiler builds the
compiler. `libctest` (`user/tcc/libctest.c`) exercises the libc the same way. Because
the whole pipeline is on-disk, a normal `cc` run is bounded only by the VFS node pool
and heap, not by any host.

---

## 3. `xbm` — the package manager

`xbm` is NyxOS's answer to `apt`/`pacman`: it builds packages **from source with the
in-OS `cc`** and installs the result. Each package is a recipe directory under
`/usr/pkg/<name>/` describing how to build it.

```
xbm install <name>      resolve deps, build each from source, install, record a manifest
xbm remove  <name>      uninstall
xbm verify  <name>      re-hash the installed binary vs its manifest (OK / MODIFIED)
xbm deps    <name>      print the install order (dependencies first), detecting cycles
xbm search  <str>       find available packages
xbm list  [--installed] browse available or installed packages
```

Key behaviours:

- **Dependency resolution.** `install` reads the recipe's `deps:` field and installs
  the whole tree in topological order (dependencies before dependents), the same
  "pull the tree" model apt and pacman use. Cycles and missing packages are reported
  rather than looping.
- **Build from source.** Every package is compiled in-OS with `cc` at install time —
  there are no prebuilt binaries to trust.
- **Integrity manifest.** After installing, `xbm` records a **SHA-256** hash of the
  binary; `xbm verify` re-hashes the on-disk file and reports `OK` or `MODIFIED`, the
  same tamper check a distro package manager gives you.
- **Remote sources.** A recipe may carry a `url:` line pointing at an `http://`
  source; `xbm install` downloads it into the package first, then compiles it — the
  "fetch source, then build" flow, over the NyxOS TCP stack.

Installed programs land in `/mnt/bin` (on the persistent ext2 disk), so they survive a
reboot and are on the shell's search path.

---

## 4. A worked example

```sh
# write a program with the built-in editor (or any means of creating a file)
edit /mnt/hello.c
    int main(void){ printf("hello from NyxOS cc\n"); return 0; }

# compile + link it in-OS
cc /mnt/hello.c -o /mnt/bin/hello

# run it — it is a normal ring-3 process
hello

# or package it: drop a recipe in /usr/pkg/hello/ and let xbm build+install+hash it
xbm install hello
xbm verify  hello        # -> OK
```

---

## 5. Where things live

| Piece | Location |
|-------|----------|
| `cc` / `xbm` command front-ends | [`kernel/core/kernel.c`](../kernel/core/kernel.c) |
| Vendored TinyCC port | [`user/tcc/`](../user/tcc/) (`tcc.elf`, `libc.c/.h/.so`) |
| Syscall ABI the built code uses | `SYS_*` in [`kernel/core/kernel.h`](../kernel/core/kernel.h) |
| Package recipes | `/usr/pkg/<name>/` (on the running system) |
| Installed binaries | `/mnt/bin` (persistent ext2 disk) |

The toolchain is what turns NyxOS from an OS you *run* programs on into one you can
*build* programs on — the groundwork for growing the userland from within.
