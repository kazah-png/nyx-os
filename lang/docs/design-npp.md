# N++ — Design Document

**Status:** staged into `ncc` (P1–P4 complete, P5 started) · **Base language:** [N](spec-n.md) · **Compiler (planned):** `n++`

N++ is to N what C++ was to C: a superset that keeps the base language intact
and adds the abstraction and safety layer on top. The contract:

> **Every valid N program is a valid N++ program, with identical behavior.**

N stays the language of the metal — kernel modules, drivers, tools where you
want to see every instruction. N++ is the language of applications: same
syscall access, same zero-hidden-runtime philosophy, but with types that make
whole bug classes unrepresentable.

---

## 1. Why a second language instead of growing N?

Because the two audiences pull in opposite directions. A driver author wants
`raw *u8` and no opinions; an application author wants `Result` and `match`
and cannot leak a window handle. Splitting lets N stay *permanently simple*
(the spec fits in one document, the compiler in one file — a feature in
itself, and the path to self-hosting), while N++ absorbs all the complexity
budget. The C/C++ split proved the model; we keep the good part (shared core,
gradual adoption per-file) and drop the bad (N++ will not fork the base
semantics — it compiles *through* N's pipeline, not beside it).

Files choose their language by extension: `.n` is compiled as N by `ncc`;
`.npp` is compiled as N++ by `n++`. The two link freely against each other.

## 2. Feature pillars

### 2.1 Data types: `struct`, `enum`, `match`

```npp
struct Rect { w: u32, h: u32 }

impl Rect {
    fn area(self) -> u32 = self.w * self.h
}

enum Shape {
    Circle(r: f64),
    Rect(w: f64, h: f64),
    Empty,
}

match shape {
    Circle(r)  => area_circle(r),
    Rect(w, h) => w * h,
    Empty      => 0.0,
}
```

Sum types with exhaustive `match` are the foundation everything else builds
on (`Result` is just an enum). Lowering: structs map to C structs; enums to
tagged unions; `match` to switch on the tag with bound locals.

### 2.2 Errors: `Result<T, E>` and `?`

```npp
fn read_config() -> Result<Config, Errno> {
    fd := fs.open("/etc/nyx.conf", .Read)?;   // ? propagates the error
    defer fs.close(fd);
    Config.parse(fd)
}
```

Kernel returns negative on error; the N++ standard bindings convert that
convention to `Result` once, at the boundary, so application code never
checks sign bits. `?` lowers to the early-return pattern; `defer` runs
LIFO at scope exit (including early returns — lowered via cleanup labels).

### 2.3 Safety: checked pointers and capabilities

- **`#[user] *T`** — a pointer that the compiler proves lies in the canonical
  user half `[0x1000, 0x0000_8000_0000_0000)` before it crosses a syscall
  boundary. This turns NyxOS's `user_ptr_ok()` kernel check into a
  compile-time guarantee. `raw *T` remains the explicit opt-out.
- **`PageFlags`** — a typed bitset for page mappings that rejects
  `WRITABLE | EXEC` combinations at compile time: W^X by construction,
  matching the kernel's NX discipline.
- **Capabilities** — a module declares its height (`ring0` / `ring3`) and
  capability set (`syscall`, `mmio`, `ports`, …); the checker rejects calls
  that exceed it. Kernel modules and user programs share one language with
  different licenses.

### 2.4 Ownership (opt-in, not Rust-globally)

Default N++ is value/pointer semantics like N. Types can opt into ownership:

```npp
own struct File { fd: i32 }        // moved, not copied; must be consumed
```

Dropping an unconsumed `own` value is a compile error unless the type
declares a destructor — enough to make handle leaks (files, windows, sockets)
unrepresentable, without imposing borrow-checking on all code. This is
deliberately far short of Rust: ownership is a per-type contract, not a
global discipline.

### 2.5 OS integration: GUI and tasks

The compositor and scheduler get language-level bindings (over the syscall
surface as it grows):

```npp
win := gui.Window.new("Clock", w: 220, h: 120)?;
win.on(Event.Click, fn(e) { put("click at {e.x},{e.y}\n"); });
```

Structured concurrency (`task` blocks whose children cannot outlive them)
maps onto NyxOS's preemptive scheduler — design follows once the kernel's
thread-spawn surface for user space stabilizes.

## 3. Compilation strategy

```
file.npp ──n++ front-end──► typed AST ──lower──► N-level IR ──► C ──cc/tcc──► ELF
```

The `n++` front-end (checker + lowering) reuses `ncc`'s lexer/parser
foundation, extends it with the constructs above, type-checks, then *lowers
to the same C surface `ncc` targets* — same runtime, same crt0, same
Makefile rules. This keeps one backend to maintain and means every N++
feature is debuggable by reading the generated C.

## 4. Staged roadmap

| Stage | Contents | Gate |
|---|---|---|
| P1 | ✅ shipped in `ncc` v0.2–v0.4: inference (`i64` literals, typed interpolation, `mut`) + the expression-level checker (names, arity, argument/operand/return/assignment types) | all `lang/examples` still compile bit-identically ✅ |
| P2 | ✅ **complete**: `struct` (v0.5) · `defer` (v0.6) · `enum` + `match` (v0.7) · `impl` methods (v0.8, static dispatch) | structs.n, defer.n, enums.n, methods.n |
| P3 | ✅ **complete** (bootstrap side): match-as-expression (v0.9) · `?` over structural Ok/Err result enums (v0.10 — same-type pass-through, cross-type Err rewrap, defers honored) · fs bindings sketch (`fsio.n`: the negative-return→Result boundary conversion of §2.2, `?` chains, deferred close). Generic `Result<T, E>` and the idiomatic `.npp` rewrite move to the `n++` front-end (P5 era) | `fsio.n` runs the §2.2 shape end-to-end |
| P4 | ✅ **complete** (bootstrap side): `#[user]` pointer flavor (v0.12 — hard no-implicit-conversion boundary, `as` as the audited crossing) · `pageflags` W^X bitset (v0.13 — total compile-time W^X proof, live-mmap verified) · capabilities (v0.14 — `#[caps(syscall)]` gates extern blocks, direct callers must hold the cap, wrapper = audited boundary). The canonical-half range proof, further capability names (`mmio`, `ports`), and ring heights move to the `n++` front-end and kernel-side modules | a kernel-module example checked at `ring0` |
| P5 | 🔨 **started**: `own` structs shipped (v0.17 — move-not-copy, must-consume; leaks/double-use/discards are compile errors) and the tracking is now **branch-aware** (v0.18 — `if`/`else` arms may consume, both exits must agree, a `return`-ending arm is exempt; moves stay refused in loops, `while` conditions, and match arms). Remaining: destructors, GUI bindings (wait on kernel window syscalls) | a windowed N++ app on the NyxOS desktop |

P1 is the enabling investment: everything later depends on the checker
existing. It also immediately improves plain N (better errors from `ncc`).

## 5. Open questions

Recorded here so decisions are made deliberately, not by accident:

1. **Generics monomorphization vs erasure** — leaning monomorphization
   (C++-style, zero-cost, larger binaries); decide at P2 with real data.
2. **String ownership** — `str` is a view; N++ needs an owning string for
   builders. Candidate: `String` as an `own struct` over the future
   user-space allocator.
3. **Closure representation** — fat pointer (fn ptr + env ptr) vs
   monomorphized call sites; needed by P5 for event handlers.
4. **`n++` binary name on VFS** — `n++` is a valid NyxOS filename today; if
   shell parsing ever conflicts, fallback name is `npc`.
