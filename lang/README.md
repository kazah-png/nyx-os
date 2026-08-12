# N & N++ — the native languages of NyxOS

<p align="center">
  <img src="https://img.shields.io/badge/N-v0.2-825AD2?style=flat" />
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
| Status | **v0.2 — working** (see below) | Design document (see [docs/design-npp.md](docs/design-npp.md)) |

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
dependencies) implements N v0.2 — including minimal type inference (typed
`:=` bindings with an `i64` default, interpolation that inserts `str` values
as text, enforced `mut`) emitting strict C99 — and is verified three ways:

1. **Real programs run on NyxOS.** The first N program booted NyxOS and ran as
   a ring-3 process (`exec /hello_nyx.elf` → `pid=5`, clean exit).
2. **Generated C is clean.** Output compiles warning-free with the OS
   freestanding flags and links with the standard NyxOS `crt0` + `nyxrt`.
3. **Behavioral tests run on the dev machine.** A host shim maps NyxOS syscall
   numbers to Linux ones (the x86_64 `syscall` ABI is identical, only numbers
   differ), so N programs can be executed and checked without booting the OS.

See [ncc/README.md](ncc/README.md) for build and test instructions, and
[docs/spec-n.md](docs/spec-n.md) for the complete v0.1 specification.

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
    └── inference.n      ← v0.2 type inference: typed bindings, typed interp, mut
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
| M2 | `ncc` compiles *inside* NyxOS with the in-OS `cc` (tcc) | next |
| M3 | `ncc hello.n` → running binary, entirely in-OS (the HolyC moment) | planned |
| M4 | N++ front-end: type checker, structs/enums/match, `Result`/`?` | design ready |
| M5 | Self-hosting: `ncc` rewritten in N | horizon |

M2 is concrete engineering, not research: `ncc.c` is plain C99 in one file with
stdio/stdlib only, and the same transpile-to-C pipeline that works on the dev
machine works in-OS the moment `ncc` itself runs there — tcc compiles the C
that `ncc` emits, exactly as the host gcc does today.

## Documentation policy

Everything in `lang/` is documented to the same bar: every public construct
specified, every design decision explained with its rationale, every limitation
stated honestly. If a feature is in the compiler, it is in the spec; if it is
planned, it is in the design doc with its staging. Documentation lands in the
same commit as the code it describes.
