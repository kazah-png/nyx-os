# N & N++ — the native languages of NyxOS

<p align="center">
  <img src="https://img.shields.io/badge/N-v0.6-825AD2?style=flat" />
  &nbsp;
  <img src="https://img.shields.io/badge/N%2B%2B-design-825AD2?style=flat" />
  &nbsp;
  <img src="https://img.shields.io/badge/target-x86__64-825AD2?style=flat" />
</p>

**N** is the native systems language of NyxOS, in the same spirit that HolyC was
the native language of TempleOS: a language designed *for* one operating system,
whose compiler knows that OS from the inside — its syscall table, its ABI, its
memory map — and which ultimately lives *inside* the OS itself, so that programs
are written, compiled, and run without ever leaving NyxOS.

**N++** is its planned superset: N plus the safety and ergonomics layer
(ownership, sum types with `match`, `Result`/`?`, traits, capability checking).
The relationship is deliberately the C/C++ one: every valid N program is a valid
N++ program, and N++ compiles down through the same pipeline.

---

## The two languages at a glance

| | **N** | **N++** |
|---|---|---|
| Role | The metal: kernel modules, drivers, small userland tools | The ergonomics: applications, GUI programs, larger systems |
| File extension | `.n` | `.npp` |
| Compiler | `ncc` (exists — bootstrap) | `n++` (planned, builds on `ncc`) |
| Memory model | Manual, raw pointers | Ownership/borrowing opt-in, `#[user]` checked pointers |
| Error handling | Return codes | `Result<T, E>` + `?` propagation |
| Data types | Primitives, pointers, `str` | + `struct` methods, `enum` sum types, `match`, generics, traits |
| Status | **v0.6 — working** (see below) | **P1 complete** · P2 underway (`struct`, `defer` shipped) |

Both share the same DNA:

- **Syscalls are part of the language.** An `extern syscall` block binds kernel
  entry points by number, and the compiler emits the raw x86_64 `syscall`
  instruction inline — no libc in between:

  ```n
  extern syscall {
      fn write(fd: i32, buf: *u8, len: isize) -> i64 = 1
      fn getpid() -> i64                             = 6
  }

  fn main() -> i64 {
      pid := getpid();
      msg := "hello from N! pid={pid}\n";
      write(1, msg.ptr as *u8, msg.len as isize);
      0
  }
  ```

- **The compiler knows NyxOS.** Types like `addr`, constants like the canonical
  user-space boundary, the register ABI, the W^X page-flag rules — these are
  compiler knowledge, not header files (fully realized in N++).

- **Readable surface, systems semantics.** `:=` inference, string
  interpolation, expression blocks — but everything lowers to straightforward C
  today (and to native code eventually) with zero hidden runtime.

## Status — what works today

The bootstrap compiler `ncc` ([ncc/ncc.c](ncc/ncc.c), single-file C, no
dependencies) implements N v0.6 — type inference (typed `:=` bindings with an
`i64` default, interpolation that inserts `str` values as text, enforced
`mut`), a complete expression-level checker (undeclared names, unknown
callees, arity, argument/operand/return/assignment types — all compile errors
with `file:line` diagnostics), `struct` records with checked literals and
field access, Go-style function-scoped `defer`, strict-C99 output — and is
verified three ways:

1. **Real programs run on NyxOS.** The first N program booted NyxOS and ran as
   a ring-3 process (`exec /hello_nyx.elf` → `pid=5`, clean exit).
2. **Generated C is clean.** Output compiles warning-free with the OS
   freestanding flags and links with the standard NyxOS `crt0` + `nyxrt`.
3. **Behavioral tests run on the dev machine.** A host shim maps NyxOS syscall
   numbers to Linux ones (the x86_64 `syscall` ABI is identical, only numbers
   differ), so N programs can be executed and checked without booting the OS.

On NyxOS itself, the compiler is one command away — `xbm install ncc` builds
it from source with the in-OS toolchain and installs it to `/mnt/bin`.

See [ncc/README.md](ncc/README.md) for build and test instructions, and
[docs/spec-n.md](docs/spec-n.md) for the complete specification.

## Repository layout

```
lang/
├── README.md            ← you are here: language home
├── docs/
│   ├── spec-n.md        ← N language specification (v0.1, complete)
│   └── design-npp.md    ← N++ design document (the superset plan)
├── ncc/
│   ├── ncc.c            ← bootstrap compiler (hosted, single-file C)
│   ├── README.md        ← build, usage, testing guide
│   └── host/nyxrt.h     ← Linux syscall shim for host-run tests
└── examples/
    ├── hello.n          ← canonical first program
    ├── countdown.n      ← loops, functions, interpolation
    ├── inference.n      ← v0.2 type inference: typed bindings, typed interp, mut
    ├── structs.n        ← v0.5 structs: literals, field access, by-value passing
    └── defer.n          ← v0.6 defer: LIFO cleanup on every exit path
```

The runtime N programs link against lives with the rest of user space:
[`user/nyxrt.h`](../user/nyxrt.h) / [`user/nyxrt.c`](../user/nyxrt.c)
(freestanding string/format helpers + the syscall primitive).

## The goal: a language that lives inside the OS

NyxOS already self-hosts a C toolchain (TinyCC runs in-OS as `cc`; `xbm`
compiles packages from source on the machine itself). N rides that ladder:

| Milestone | Description | Status |
|---|---|---|
| M0 | Bootstrap `ncc` compiles N v0.1 on the dev machine | ✅ done |
| M1 | Language home in-repo: compiler, spec, examples, docs | ✅ this directory |
| M2 | `ncc` compiles *inside* NyxOS with the in-OS `cc` (tcc) | ✅ done |
| M3 | `ncc hello.n` → running binary, entirely in-OS (the HolyC moment) | ✅ done |
| M4 | N++ front-end: type checker, structs/enums/match, `Result`/`?` | design ready |
| M5 | Self-hosting: `ncc` rewritten in N | horizon |

M2 and M3 were reached with zero changes to the compiler's design: `ncc.c` is
plain C99 in one file, so the in-OS tcc builds it directly, and the same
transpile-to-C pipeline that works on the dev machine works in-OS. The
verified full loop inside a booted NyxOS:

```
cc /mnt/ncc.c -I/usr/src/nyx -o /mnt/bin/ncc     # tcc compiles the N compiler
ncc /mnt/hello.n -o /mnt/hello_gen.c             # ncc transpiles N source
cc /mnt/hello_gen.c /mnt/nyxrt.c -I/mnt -o /mnt/bin/nhello
nhello                                           # → hello from N! pid=8
```

The runtime carries the portability knowledge this took (see `user/nyxrt.h`):
under tcc it spells the fixed-width types directly (no `<stdint.h>` on the
in-OS include path) and loads the syscall ABI's r10/r8/r9 explicitly inside
the asm (tcc cannot satisfy six bound register constraints), and `nyxrt.c`
leaves `environ` to the in-OS `libc.o` to avoid a duplicate symbol.

## Documentation policy

Everything in `lang/` is documented to the same bar: every public construct
specified, every design decision explained with its rationale, every limitation
stated honestly. If a feature is in the compiler, it is in the spec; if it is
planned, it is in the design doc with its staging. Documentation lands in the
same commit as the code it describes.
