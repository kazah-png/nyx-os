# Contributing to NyxOS

Thanks for your interest! NyxOS is a from-scratch x86_64 hobby kernel, and
every contribution helps move it forward.

## Before you start

Open an issue or start a discussion to propose your change — especially for
new features, refactors, or anything that touches core subsystems (paging,
scheduling, syscalls, the VFS layer). Small bug fixes are always welcome
without prior discussion.

## Getting started

1. Fork and clone the repo.
2. Make sure you have a working x86_64 cross-compiler (`x86_64-elf-gcc`)
   or a host GCC with `-m64` support.
3. Run `.\build.ps1` (Windows) or `make -C kernel` (Linux/WSL) to verify
   the kernel builds with **zero warnings**.
4. Run `.\run.ps1` to boot in QEMU and confirm the desktop loads.

## Coding standards

### C

- **K&R braces** — opening brace on the same line as the statement:
  ```c
  void func(int x) {
      if (x) {
          do_stuff();
      }
  }
  ```
- **Indent:** 4 spaces, no tabs.
- **Typedefs:** struct and enum typedefs use the `_t` suffix.
- **Naming:** `snake_case` for functions and variables, `UPPER_SNAKE` for
  macros and constants.
- **Static functions** are preferred over globals; mark file-internal symbols
  `static`.
- **No comments** unless the code does something non-obvious (bit twiddling,
  hardware quirks, workarounds).
- **No magic numbers** — define named constants.
- **`memset_asm`/`memcpy`** instead of libc equivalents (no libc in the
  kernel). `snprintf` from `kernel.h` is the safe formatter.
- **No dynamic allocation** in interrupt context unless you've audited every
  caller and it's behind `preempt_disable`.

### Assembly (NASM)

- **Intel syntax**, `.asm` extension.
- Labels in `snake_case`, local labels prefixed with `.`:
  ```asm
  global load_page_directory
  load_page_directory:
      mov cr3, rdi
      ret
  ```
- Kernighan–Ritchie-style comment blocks (`;` comments).

### Commit messages

Follow the existing style:

```
v5.x.y: Short description (72-char max)

Longer explanation of what changed and why, wrapped at 72 characters.
Include relevant bug numbers or context.
```

### Build expectations

The build **must** finish with zero errors and zero warnings — a regression is a blocker.

## Testing

- The project uses no test framework. Most verification is done manually in
  QEMU via the shell (`exec /init.elf`, `cowtest`, `tcploop`, `ping`, etc.).
- If your change touches the scheduler, timer, or process life cycle, run
  `mtdemo` and verify the GUI stays stable.
- If your change touches networking, run `ping 127.0.0.1`, `tcploop`, and
  `dhcp` (if a NIC is available).
- If your change touches the GUI compositor, open a Terminal window, type
  several commands, drag windows, and verify no crashes.

## Pull request process

1. Keep PRs focused — one feature or fix per PR.
2. Rebase onto the latest `master` before opening.
3. Ensure the build produces zero warnings.
4. Mention what you tested and how.
5. Sign off every commit (`git commit -s`) — see the DCO below.

## Developer Certificate of Origin

NyxOS uses the [Developer Certificate of Origin](DCO.txt) (DCO) — the same
lightweight process the Linux kernel uses. It is **not** a copyright assignment:
you keep the rights to your work. By signing off, you certify that you wrote the
patch (or otherwise have the right to submit it) under the project's GPL-2.0+
license.

Add a sign-off line to every commit:

```
Signed-off-by: Your Name <your.email@example.com>
```

`git commit -s` adds it automatically from your `user.name` / `user.email`. The
full text is in [DCO.txt](DCO.txt).

## AI-assisted contributions

NyxOS embraces AI tools for writing and debugging code — they are a great way
to accelerate implementation. However:

- **AI is a tool for building, not for planning.** Infrastructure decisions,
  architecture design and action plans should be made by people who understand
  the full context of the kernel. Let AI write the code, but own the design
  yourself.
- **All code is reviewed by hand before it lands.** Every line an AI produces
  is read, understood and often tweaked by a human maintainer before it is
  committed. Treat AI output as a draft, not as final.
- **You are responsible for what AI generates.** If a bug or security issue
  originates from AI-written code, the human who submitted it owns the fix.

Follow these guidelines and you are welcome to use whatever tools help you
write better NyxOS code.

## What needs help

Check the open [issues](https://github.com/kazah-png/nyx-os/issues) — they
list the highest-priority work along with context and constraints.

## Questions

Open a discussion, ping `uselessalter` on Discord, or email nyxos@inbox.lv. No question is too small.
