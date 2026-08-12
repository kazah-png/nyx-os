# ncc — the N bootstrap compiler

Single-file, dependency-free C99 program that compiles N v0.1 (see the
[language spec](../docs/spec-n.md)) to freestanding C targeting the NyxOS
runtime. "Bootstrap" means: it exists to get N off the ground and to be simple
enough to port *into* NyxOS (milestone M2) — clarity beats cleverness
throughout.

## Build

Any C99 compiler works; no libraries beyond the C standard library:

```bash
gcc -O2 -Wall -Wextra -o ncc ncc.c
```

(That command is warning-clean; keep it that way.)

## Usage

```bash
ncc input.n -o output.c    # transpile
ncc input.n                # ... or write the C to stdout
```

On success `ncc` prints a one-line summary to stderr and exits 0. On the first
error it prints `file:line: message` and exits 1.

## Compiling the output for NyxOS

The generated C includes `"nyxrt.h"` (the runtime header in `user/`) and is
compiled with the standard NyxOS user-space flags, then linked with the
standard `crt0` and the runtime:

```bash
gcc -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone \
    -I ../../user -c output.c ../../user/nyxrt.c
nasm -f elf64 ../../user/crt0.asm -o crt0.o
ld -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 \
   -o program.elf crt0.o nyxrt.o output.o
```

`-mno-red-zone` is mandatory (NyxOS interrupt handlers may run on the user
stack region). Since v0.2 the emitted C is **strict C99 with no GNU
extensions** (verified with `-std=c99 -pedantic-errors` in the test
pipeline) — this matters because TinyCC is the in-OS compiler that will
build N programs inside NyxOS at milestone M2/M3, and tcc does not support
extensions like `__auto_type`.

## Testing without booting the OS

The x86_64 `syscall` *instruction* ABI is identical on Linux and NyxOS — only
the syscall numbers differ. [`host/nyxrt.h`](host/nyxrt.h) exploits that: it
is a drop-in replacement runtime header whose `__nyx_syscall6` maps NyxOS
numbers to Linux ones (write→write, getpid→39, exit→60). Compile the generated
C hosted, against the shim, and run it natively:

```bash
mkdir -p t && cp host/nyxrt.h ../../user/nyxrt.c output.c t/ && cd t
gcc -O2 -Wall -o prog output.c nyxrt.c && ./prog
```

This executes the *real* generated code — real syscall instruction, real
formatting runtime — and is the standard way to behaviorally test compiler
changes quickly. Full in-OS verification (boot QEMU, `exec` the ELF) remains
the release gate.

## Source tour

`ncc.c` is organized top-to-bottom in four sections:

| Section | What it does | Key entry points |
|---|---|---|
| Lexer | Tokens, comments, literals; string interpolation via a brace-depth mode stack that emits `HEAD/MID/TAIL` runs | `next_token`, `scan_string_body` |
| Parser | Recursive descent, one-token lookahead; precedence ladder for expressions | `parse_program`, `parse_expr`, `parse_block` |
| Inference | Minimal per-function symbol table (block-scoped via save/restore); types `:=` bindings, dispatches interpolation, enforces `mut` | `infer_type`, `vars_find` |
| Codegen | Emits strict-C99 per the spec's §7 contract; interpolations lowered to hoisted, bounds-checked buffers | `gen_program`, `gen_preludes` |
| Driver | CLI, file I/O | `main` |

Two invariants worth knowing before editing:

1. **Interpolation buffers are hoisted** into the enclosing block, never
   inside a statement-expression — the `nyx_str` must outlive the statement
   using it (spec §7.3; a dangling-pointer bug taught us this).
2. **The `}` disambiguation** between "close a block" and "close an
   interpolation" is the lexer's brace-depth stack (`istack`): an
   interpolation closes only when depth returns to its opening level, so
   nested braces inside interpolated expressions stay correct.

## Porting to NyxOS (milestone M2)

The file deliberately uses only: `stdio` (fopen/fread/fprintf/stderr),
`stdlib` (calloc/exit/strtod-free), `string`, `stdint`, `stdarg`. The port
checklist is: verify those exist in NyxOS's shared libc, compile `ncc.c` with
the in-OS `cc` (tcc), and wire an `xbm` recipe. No design changes expected —
that constraint is why the compiler has no dependencies.
