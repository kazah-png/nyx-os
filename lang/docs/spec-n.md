# The N Language — Specification

**Version:** v0.18 (bootstrap) · **Implementation:** [`lang/ncc/ncc.c`](../ncc/ncc.c) · **Target:** NyxOS x86_64

This document specifies N exactly as implemented by the bootstrap compiler
`ncc`. It is a *descriptive* spec: everything here compiles today. Planned
features live in the [N++ design document](design-npp.md), not here — if you
read it in this file, you can use it.

---

## 1. Overview

N is a small systems language that transpiles to freestanding C. Its design
goals, in priority order:

1. **Direct kernel access.** NyxOS syscalls are first-class: declared once with
   their number, called like functions, compiled to the raw x86_64 `syscall`
   instruction with zero overhead and zero libc.
2. **Readable surface.** Type inference with `:=`, string interpolation,
   blocks-as-values — while keeping C-level predictability of what the
   generated code does.
3. **No hidden runtime.** The entire runtime is ~60 lines of freestanding C
   (`user/nyxrt.c`): a syscall primitive and three string-format helpers.
   There is no allocator, no GC, no startup machinery beyond the standard
   NyxOS `crt0`.

An N source file is a sequence of top-level *items*: `extern syscall` blocks
and function definitions. Execution starts at `fn main`.

## 2. Lexical structure

### 2.1 Source encoding

Source files are byte-oriented ASCII (UTF-8 passes through string literals
uninterpreted). The canonical file extension is `.n`.

### 2.2 Comments

```n
// line comment, runs to end of line
/* block comment /* nests */ properly */
```

### 2.3 Identifiers and keywords

Identifiers match `[A-Za-z_][A-Za-z0-9_]*`. The following are reserved:

```
as break continue defer else enum extern false fn for if impl in match
mut own raw return struct syscall true while
```

Additionally, **C keywords that are not N keywords** (`double`, `int`,
`static`, `typedef`, `goto`, …) are rejected as names at every declaration
site: the generated C uses N names verbatim (readable output is a design
goal), so such a name would break the C build downstream with a confusing
error. The compiler says so up front instead.

(`self` is not reserved: it is the conventional name of a method's receiver
and an ordinary binding elsewhere.)

### 2.4 Integer literals

| Form | Example | Notes |
|---|---|---|
| Decimal | `1024`, `1_000_000` | `_` separators allowed, ignored |
| Hexadecimal | `0xFFFF_F000` | prefix `0x` |

Integer literals are 64-bit values; their concrete C type is currently chosen
by C's usual rules at the use site (see §9, Limitations).

### 2.5 String literals and interpolation

String literals are double-quoted with these escapes:
`\n \t \r \0 \\ \" \' \{ \}`.

A `{expression}` inside a string literal is **interpolation**: the expression
is evaluated and formatted into the string at that position.

```n
msg := "pid={getpid()} answer={40 + 2}\n";
```

Semantics: each interpolated expression is evaluated once, in order of
appearance, and formatted **by its inferred type** (§6.4): a `str` value is
inserted verbatim as text; every other type is converted as a signed 64-bit
integer and formatted in decimal. The result is a `str` built in a 256-byte
function-scope buffer (§7.3); text beyond the buffer capacity is truncated,
never overflowed. Use `\{` and `\}` for literal braces.

### 2.6 Operators and punctuation

```
( ) { } [ ] , ; : . .. ->
:= = += -=
#[user]           (attribute, v0.12 — see 3.2)
#[caps(syscall)]  (attribute, v0.14 — see 4.7)
+ - * / %
== != < <= > >=
! && ||
& | ^ << >>
as ?     (? = error propagation on result enums, §5.9)
```

## 3. Types

### 3.1 Primitives

| N type | Meaning | C lowering |
|---|---|---|
| `i8 i16 i32 i64` | signed integers | `nyx_i8` … `nyx_i64` |
| `u8 u16 u32 u64` | unsigned integers | `nyx_u8` … `nyx_u64` |
| `isize usize` | pointer-width (64-bit) integers | `nyx_isize` / `nyx_usize` |
| `addr` | a virtual address (64-bit) | `nyx_addr` |
| `bool` | `true` / `false` | `nyx_bool` |
| `str` | string view: pointer + length, non-owning | `nyx_str` `{ const char* ptr; u64 len; }` |
| `void` | no value (also written by omitting `-> T`) | `void` |
| `never` | function does not return (e.g. `exit`) | `void` + unreachable loop |

`str` values expose two fields: `.ptr` (`*u8`-compatible pointer to the bytes)
and `.len` (byte count). Strings are not NUL-dependent; the length is
authoritative (the runtime does keep buffers NUL-terminated as a convenience).

### 3.2 Pointers

```n
*T              // pointer to T
raw *T          // same representation; documents intent to do unchecked arithmetic
#[user] *T      // user-pointer flavor (v0.12): implicit conversions refused
```

All lower to `T*` in C. `raw` exists so N++ can attach checking to `*T`
while leaving `raw *T` unchecked (see the N++ design). Any type name not in
§3.1 passes through to C unchanged, which is the current FFI escape hatch.

#### `#[user]` checked pointers (since v0.12 — opening N++ P4)

`#[user] *T` is a **distinct pointer flavor** in the type system:

- It never converts implicitly to or from a plain `*T` — not even through
  the `*u8`/`*void` byte-pointer wildcards (§6.5), which are checked
  *after* the flavor comparison and so cannot smuggle a pointer across.
- `expr as #[user] *T` (and the reverse cast) is the **one audited
  crossing point** — every user-pointer handoff is explicit and greppable.
- Marking a syscall parameter `#[user]` documents in the signature which
  arguments the kernel range-checks with `user_ptr_ok()`; callers are
  forced to acknowledge the boundary at the call site:

  ```n
  extern syscall {
      fn write(fd: i32, buf: #[user] *u8, len: isize) -> i64 = 1
  }
  fn put(s: str) {
      write(1, s.ptr as #[user] *u8, s.len as isize);
  }
  ```

- `#[user]` applies only to pointer types, and is mutually exclusive with
  `raw` (raw is the explicit opt-out of checking) — both are compile
  errors.
- The flavor is **erased at codegen**: same C type, zero runtime cost.

This is the bootstrap slice of the N++ design's §2.3; the compile-time
*range proof* (that the pointer lies in the canonical user half) arrives
with the `n++` front-end. The attribute space is reserved: `#[` followed
by anything other than a known attribute (`#[user]`, `#[caps(syscall)]`
— §4.7) is a lex error.

### 3.3 `pageflags` — W^X page permissions (since v0.13)

```n
rw := PROT_READ | PROT_WRITE;      // data page
rx := PROT_READ | PROT_EXEC;       // code page
wx := PROT_WRITE | PROT_EXEC;      // ← compile error: W^X violation
sys_mmap(0, 4096, rw as i64, 34, -1, 0);
```

`pageflags` is a builtin bitset type for page permissions. Its defining
property: **values can only be built by `|`-composing the predeclared
constants** `PROT_NONE` / `PROT_READ` / `PROT_WRITE` / `PROT_EXEC`, so
the compiler knows every value's exact bit set — which turns the W^X
discipline (a mapping is never writable *and* executable) into a **total
compile-time proof**, not a lint or a runtime check. In detail:

- The constants carry the kernel's own `mmap`/`mprotect` numbers
  (`kernel/core/kernel.h`, identical to POSIX: R=1 W=2 X=4), so one N
  source runs against both the NyxOS kernel and the Linux host shim.
- `pageflags` composes **only** with `|`, and only with other
  `pageflags` values; every other operator is a compile error.
- `flags as i64` (any integer type) extracts the bits for a syscall
  argument. The reverse — casting an integer *into* `pageflags` — is
  refused: arbitrary bits would break the proof. There is no escape
  hatch by design; pass raw integers as plain `i64` if you mean that.
- A `pageflags` **parameter** is opaque: it can be passed on or cast
  out, but not extended with `|` — its composition was already checked
  at every call site, and re-composition against unknown bits would
  reopen the hole.
- Bindings (including `mut`, whose tracked set follows reassignment)
  work normally. Lowered as `nyx_i64` with the literal bit values —
  zero runtime representation cost.

This is N++ P4's PageFlags (design doc §2.3) in bootstrap form; mapping
the same discipline onto kernel-side PTE bits (NX inversion included)
lands with the ring-0 capability work.

## 4. Items

### 4.1 `extern syscall` — kernel bindings

The signature feature of N. Binds NyxOS syscalls by number:

```n
extern syscall {
    fn write(fd: i32, buf: *u8, len: isize) -> i64 = 1
    fn getpid() -> i64                             = 6
    fn exit(status: i32) -> never                  = 0
}
```

Each entry declares a typed function whose body *is* the syscall. The compiler
generates a `static inline` C wrapper that places the number in RAX and the
arguments in RDI, RSI, RDX, R10, R8, R9 (the NyxOS syscall ABI, identical in
register convention to the standard x86_64 one), executes `syscall`, and
returns RAX cast to the declared return type. Up to 6 arguments; missing
arguments are passed as 0.

- A `-> never` syscall is emitted as `void` followed by an unreachable loop —
  the kernel does not return from it.
- The trailing `= N` number is **required**: it is the single source of truth
  connecting the name to the kernel's dispatch table (`kernel/syscall.c`).
- Trailing `;` after an entry is optional.

The authoritative number table lives in [`user/syscall.h`](../../user/syscall.h);
`extern syscall` blocks must agree with it.

### 4.2 Function definitions

```n
fn put(s: str) {                 // no return type → void
    write(1, s.ptr as *u8, s.len as isize);
}

fn add(a: i64, b: i64) -> i64 {
    a + b                        // block tail value (§5.6)
}
```

Parameters are `name: type`, comma-separated. The return type follows `->` and
may be omitted for `void`. `fn main() -> i64` is the program entry point; its
return value becomes the process exit status (via `crt0` → `SYS_EXIT`).

### 4.3 `struct` declarations (since v0.5)

```n
struct Rect {
    w: i64,
    h: i64,
}

fn area(r: Rect) -> i64 {
    r.w * r.h
}

mut r := Rect{ w: 6, h: 7 };    // literal: every field, named, exactly once
r.h = r.h + 1;                  // field write — requires a `mut` binding
```

A `struct` is a named field record with C layout (it lowers to a C `typedef
struct`, so N structs are directly ABI-compatible with kernel and libc
structures). Rules:

- **Construction is total**: a literal `Name{ field: value, ... }` must
  initialize every declared field, each exactly once, in any order.
- **Field access** uses `.`; reading needs nothing special, writing any field
  requires the *root binding* to be `mut` (mutating a field mutates the
  whole value).
- Structs are **values**: they pass to and return from functions by copy,
  and assignment copies. Field types may be any N type, including `str` and
  other structs.
- In an `if`/`while` condition (or a `match` subject), `{` always opens the
  body — parenthesize the literal (`if (Rect{ … }.w > 0) { … }`, since
  v0.9) or bind it on its own line first if you need one there (the same
  disambiguation rule and escape hatch Rust uses).
- The struct name space is checked: using an undeclared struct, an unknown
  field, or an incomplete literal is a compile error (§6.5).

## 5. Statements

Statements are separated by `;`. Blocks are `{ ... }` and may end with a tail
expression (§5.6).

### 5.1 Variable binding — `:=`

```n
x := 42;              // immutable binding, type inferred
mut counter := 0;     // mutable binding
```

`:=` declares a new variable in the current scope, typed by its initializer
(§6.4). Bindings without `mut` must not be reassigned — the compiler rejects
the assignment with an error (enforced since v0.2). Function parameters are
immutable bindings. Binding an expression with no value (a `void` or `never`
call) is an error.

### 5.2 Assignment

```n
counter = counter + 1;
counter += 1;
counter -= 1;
```

Targets must be assignable places (a variable or field). Valid on `mut`
bindings.

### 5.3 `while`

```n
while n > 0 {
    n = n - 1;
}
```

### 5.4 `if` / `else`

```n
if x > 10 {
    put("big\n");
} else if x > 0 {
    put("small\n");
} else {
    put("non-positive\n");
}
```

`if` at statement position; braces are mandatory, parentheses around the
condition are not.

### 5.5 `return`, `break`, `continue`

```n
return;          // from a void function
return x + 1;    // with a value
break;           // exit innermost while
continue;        // next iteration
```

### 4.4 `enum` declarations (since v0.7)

```n
enum Shape {
    Circle(r: i64),
    Rect(w: i64, h: i64),
    Empty,
}

s := Shape.Circle{ r: 3 };      // payload variant: named-field literal
e := Shape.Empty;               // payload-less variant: bare reference
```

An `enum` is a **tagged union** — a value that is exactly one of its
variants, each optionally carrying a named-field payload. The C lowering is
`{ int tag; union { ... } u; }`, so enums are inspectable from C when
needed. Construction rules mirror struct literals: a payload variant's
literal must initialize every payload field exactly once; a payload-less
variant is written bare (`Shape.Empty`) and referencing a payload variant
without its literal is a compile error. Enum values participate in no
operators — `match` (§5.6) is how you look inside one. This is the
foundation `Result<T, E>` will be built on.

### 4.5 `impl` methods (since v0.8)

```n
impl Rect {
    fn area(self) -> i64 {
        self.w * self.h
    }
    fn scale(self, k: i64) -> Rect {
        Rect{ w: self.w * k, h: self.h * k }
    }
}

r := Rect{ w: 3, h: 4 };
n := r.scale(2).area();     // static dispatch; calls chain naturally
```

An `impl Type { ... }` block attaches functions to a declared struct or
enum. Rules:

- The first parameter is always `self`, written bare — it is the receiver,
  typed as the impl'd type, passed **by value**, and immutable (methods
  return new values rather than mutating; `mut self` is future work).
- Dispatch is **static**: the receiver's compile-time type selects the
  method. The lowering is a plain C function `Type_method(Type self, ...)`
  — no vtables, no indirection, inspectable from C.
- Calls are checked like functions: unknown method, wrong arity, and
  argument type mismatches are compile errors naming `Type.method`.
- Methods and fields share the `.` syntax; parentheses select the method
  (`r.area` is the field lookup — an error if no such field — and
  `r.area()` is the call).

### 4.6 `own struct` — must-consume types (since v0.17, opening N++ P5)

```n
own struct File { fd: i64 }

fn open_log() -> File { File{ fd: 3 } }   // birth; discharged by return
fn close_log(f: File) { put("closing {f.fd}
"); }   // the consuming sink

f := open_log();      // f is born LIVE: someone must consume it
put("{f.fd}
");      // field reads peek — no move
g := f;               // ownership MOVES to g; f is dead
close_log(g);         // obligation discharged
```

An `own struct` is **move-not-copy** and **must-consume** — the two
classic handle bugs become compile errors:

- **Leaking** — an own binding still live when its scope ends (function
  end, an early `return`, a nested block), or an own call result that is
  discarded instead of bound, is an *unconsumed own value* error.
- **Double use** — once a binding is moved (bound to another name,
  passed as an argument, returned), any further use of the old name —
  including field reads — is a *use after move* error.

The ownership contract at function boundaries: a parameter of own type
arrives **held** — the callee is the new owner of record, free to use,
move, or simply let the value end with its body (that is how a consuming
sink like `close_log` terminates the chain). A caller that receives an
own return value gives it a fresh birth, obligation included.

Since v0.18 the tracking is **branch-aware**: `if`/`else` arms may
consume an own value, and the checker verifies each arm against the same
pre-`if` states, then requires the two exits to **agree** on every own
binding — moved in both arms, or in neither:

```n
if noisy > 0 {
    close_log(f);      // both arms consume f —
} else {
    archive_log(f);    // — so the states agree after the if
}
```

Moving a value in only one arm (or in a lone `if` with no `else`, whose
implicit other path moves nothing) is a *consumed in only one branch*
error. One exemption keeps early exits natural: an arm whose **last
statement is `return`** leaves through the return, never reaching the
code after the `if`, so it is excluded from the agreement check — the
return's own leak scan polices that path instead:

```n
f := open_log();
if hurry > 0 {
    close_log(f);
    return 1;          // this path never reaches the merge point
}
close_log(f);          // only ever entered with f still live
```

The flow tracking stays **honest**, trading power for zero false
confidence — each remaining restriction is a compile error, not a gap:

- Moves are refused wherever a statement may run **zero or many
  times**: loop bodies, **`while` conditions** (the condition
  re-evaluates every iteration — consuming there would use the value
  again on the second test), and `match` arms (consume conditionally
  with `if`/`else`). `for` range **bounds** are exempt: they are
  hoisted and evaluated exactly once, before the loop.
- Own bindings are **immutable** (`mut own` is refused); ownership
  changes hands by move, not by overwrite.
- Own values cannot **nest** in structs or enum payloads, be pointed to
  (`*File` is refused everywhere), cross into **syscalls**, flow through
  a **match expression**, or appear in a **defer**.
- No destructors yet — the design document reserves them; today the
  consuming sink is an ordinary function.

Everything above is erased at codegen: an own struct lowers to the same
plain C struct as any other — the ownership discipline is free.

### 4.7 Capabilities — `#[caps(syscall)]` (since v0.14)

```n
#[caps(syscall)]
extern syscall {
    fn write(fd: i32, buf: #[user] *u8, len: isize) -> i64 = 1
}

#[caps(syscall)]                 // the audited boundary
fn put(s: str) {
    write(1, s.ptr as #[user] *u8, s.len as isize);
}

fn greet() {
    put("hi\n");                 // capability-free code uses the wrapper
    // write(...) here would be a compile error
}
```

Capabilities make *who may cross into the kernel* a checked property:

- Marking an `extern syscall` block `#[caps(syscall)]` declares its
  bindings to be **gated kernel crossings**.
- A **direct call** to a gated binding requires the *calling function*
  to hold the capability — declared with the same attribute on the `fn`.
- Enforcement is deliberately at direct call sites only: a
  capability-holding wrapper is the audited boundary (the same shape as
  `unsafe fn`, or §3.2's `as #[user]` cast), and everything above it is
  ordinary, capability-free application code. Grep for the attribute
  and you have the complete list of kernel touchpoints.
- **Opt-in per block**: unmarked `extern syscall` blocks behave exactly
  as before, so a codebase adopts the discipline binding by binding.
- `impl` methods hold no capabilities in v0.14 — they call gated
  wrappers like all other code.
- `syscall` is the only capability name in the bootstrap; the design
  document reserves the rest (`mmio`, `ports`, ring heights) for the
  `n++` front-end and kernel-side modules.

This closes N++ P4's bootstrap staging (design doc §2.3): `#[user]`
pointers (v0.12) + `pageflags` W^X (v0.13) + capabilities (v0.14).

### 5.6 `match` (since v0.7)

```n
match s {
    Circle(r) => {
        put("circle r={r}\n");
    },
    Rect(w, h) => {
        put("rect {w}x{h}\n");
    },
    Empty => {
        put("empty\n");
    },
}
```

`match` inspects an enum value and runs exactly one arm. Rules:

- The subject must be an enum value; each arm names a variant, and the
  match must be **exhaustive** — every variant covered exactly once
  (adding a variant later makes every non-updated `match` a compile
  error, which is the point).
- Arm binds are **positional**: `Rect(w, h)` binds `w` and `h` to the
  payload's fields in declared order, typed accordingly, immutable, scoped
  to that arm. The bind count must equal the payload field count;
  payload-less variants take no parentheses.
- The subject is evaluated once. Arms are blocks (braces required).

#### 5.6.1 `match` as an expression (since v0.9)

`match` can also *be* a value. In this form each arm yields a **single
expression** after the `=>` (no braces), and the whole match evaluates to
the selected arm's value:

```n
a := match shape {                 // binding
    Circle(r)  => 3 * r * r,
    Rect(w, h) => w * h,
    Empty      => 0,
};

total = match shape { ... };       // assignment (target must be mut)

fn area(s: Shape) -> i64 {
    return match s { ... };        // return value
}
```

Rules, on top of the statement form's (same subject/exhaustiveness/bind
checks):

- **Every arm must yield a value**, and all arms must agree on one result
  type — the first arm fixes it, later arms must be compatible with it
  (the integer types inter-convert; everything else is strict). An arm
  whose expression has no value (a call to a `void` function) is a
  compile error.
- **Positions are deliberately limited** in v0.9: the value of a binding
  (`x := match … ;`), of an assignment (`x = match … ;`, compound
  assignments included), or of a `return`. Each position has one obvious,
  readable lowering into strict C99 (an exhaustive `switch` assigning a
  result temporary — see §7.1); general expression nesting
  (`f(match …)`, `1 + match …`) is not accepted.
- **Arm binds may shadow the target.** The lowering assigns the selected
  arm's value to a compiler temporary and consumes the target *after* the
  switch, so `x := match s { A(x) => x, … }` means what it says.
- `return match` computes the value **before** any `defer`s run, exactly
  like a plain `return` (§5.7).
- The subject is parsed like a condition header (§4.3): a struct/enum
  literal used directly as the subject must be parenthesized —
  `match (Shape.Empty) { … }` — or bound first. Parens re-enable literals
  in all condition headers since v0.9 (the same escape hatch Rust uses).

This form is the groundwork for `Result`/`?` (N++ P3): `?` will lower to
exactly this shape — an exhaustive match on `Ok`/`Err` whose value feeds
the surrounding binding or return.

### 5.7 `defer` (since v0.6)

```n
fn copy(src: str, dst: str) -> i64 {
    fd := fs_open(src);
    defer fs_close(fd);      // runs when copy() exits — on EVERY path
    if fd < 0 {
        return -1;           // defer runs here
    }
    do_copy(fd)              // and here (tail)
}
```

`defer expr;` registers `expr` to execute when the **function** exits.
Semantics (deliberately Go's):

- Deferred expressions run in **LIFO** order — last registered, first run.
- They run on **every** exit path: each early `return`, the body's tail
  expression, and (for `void` functions) falling off the end.
- The return value is computed **before** the defers run, so a defer cannot
  change what the function returns.
- Names in the deferred expression resolve at the point of registration.

**v0.6 restriction:** `defer` may appear only in the function's *outermost*
block (not inside `if`/`while` bodies). This keeps the static lowering exact
— defers cannot be conditionally registered, so no runtime defer stack is
needed. Registering conditionally is a compile error, not a silent surprise.

### 5.9 Error propagation — `?` (since v0.10)

```n
enum DivResult { Ok(q: i64), Err(code: i64) }

fn tenfold_quotient(a: i64, b: i64) -> DivResult {
    q := checked_div(a, b)?;     // Ok: q is bound · Err: returned to caller
    DivResult.Ok{ q: q * 10 }
}
```

**Result enums.** N has no generics yet, so `Result<T, E>` is a *structural
convention*: any enum with exactly the two variants `Ok` and `Err`, each
carrying zero or one payload field, is a **result enum**, and `?` operates
on every enum of that shape. (The generic type former arrives with the
`n++` front-end; code written against the convention will migrate as-is.)

**What `?` does.** `expr?` evaluates a result-enum value once. On `Ok` the
statement consumes the payload; on `Err` the enclosing function **returns
immediately**, propagating the error. Three statement positions carry it
(the same restriction discipline as §5.6.1):

| Form | On `Ok(v)` |
|---|---|
| `x := expr?;` | binds `x` to `v` (requires `Ok` to carry a payload) |
| `x = expr?;` / `x += expr?;` | assigns/updates the `mut` target with `v` |
| `expr?;` | discards the payload — check-and-continue |

**Propagation rules.**

- The enclosing function's return type must itself be a result enum —
  using `?` in `main() -> i64` is a compile error naming the actual
  return type.
- If the operand and return types are the **same** enum, the `Err` value
  is returned unchanged.
- If they **differ**, the `Err` payload is rewrapped into the return
  type's `Err` variant: both `Err`s must agree on carrying a payload, and
  the payload types must be compatible (§6.5) — checked at compile time.
- `defer`s run **before** the propagating return, exactly as for any
  other `return` (§5.7).

**Lowering** (§7.1): the operand lands in a `__t` temp; `if (__t.tag ==
Err)` returns (rewrapping into an `__e` temp when needed); the `Ok`
payload is then consumed from `__t.u.Ok`. No hidden control flow beyond
the visible early return.

### 5.10 Block tail value

The final expression of a function body, written **without** a trailing `;`,
is the function's return value:

```n
fn main() -> i64 {
    setup();
    0                // ← returned
}
```

This is exactly equivalent to `return 0;`.

### 5.11 `for` — counted loops (since v0.11)

```n
for i in 1..6 {          // i = 1, 2, 3, 4, 5  (half-open, like Rust)
    sum += i;
}
for i in 0..twice(5) {   // bounds are expressions, evaluated ONCE
    if i % 2 == 0 { continue; }
    if i == 7     { break; }
    put("{i} ");
}
```

`for name in start..end block` counts `name` over the **half-open** integer
range `[start, end)`:

- Both bounds must be integer-typed expressions; each is evaluated
  **exactly once**, before the loop begins (a bound that calls a function
  does not re-run per iteration). `start >= end` gives zero iterations.
- The loop variable is a **fresh immutable `i64`**, scoped to the body.
  Assigning to it is a compile error; shadowing an outer name is fine.
- `break` and `continue` behave as in `while`.
- The range header is parsed like a condition (§4.3): struct/enum literals
  there need parens.
- v0.11 scope, deliberately: no inclusive `..=`, no step clause, no
  iterator protocol — each waits for a real use case rather than
  speculative syntax.

Lowering (§7.1): both bounds land in temps, then a plain C
`for (nyx_i64 i = __fs; i < __fe; i++)`.

## 6. Expressions

### 6.1 Precedence (highest binds tightest)

| Level | Operators | Associativity |
|---|---|---|
| 1 | calls `f(x)`, field `a.b` | left |
| 2 | unary `-` `!` | right |
| 3 | `as` (cast) | left |
| 4 | `*` `/` `%` | left |
| 5 | `+` `-` | left |
| 6 | `<<` `>>` | left |
| 7 | `&` | left |
| 8 | `^` | left |
| 9 | `\|` | left |
| 10 | `==` `!=` `<` `<=` `>` `>=` | left |
| 11 | `&&` | left |
| 12 | `\|\|` | left |

Note the placement of `as` above arithmetic: `x as u32 + 1` casts first, then
adds. Comparison sits *below* the bitwise operators (unlike C), so
`flags & MASK == 0` means `(flags & MASK) == 0` — the common intent.

### 6.2 Casts — `as`

```n
msg.ptr as *u8       // pointer conversion
count as isize       // integer width/signedness conversion
```

`as` performs a C-style explicit conversion to the named type. Narrowing casts
truncate exactly as in C. Chaining is allowed: `x as u32 as u64`.

### 6.4 Type inference

Every expression has an inferred N type, computed by these rules (top match
wins):

| Expression | Inferred type |
|---|---|
| integer literal | `i64` (the language default) |
| `true` / `false` | `bool` |
| string literal, interpolated string | `str` |
| variable / parameter | its binding's type |
| `f(...)` | `f`'s declared return type |
| `s.ptr` / `s.len` (on `str`) | `*u8` / `u64` |
| comparison, `&&`, `\|\|`, `!` | `bool` |
| other unary / binary arithmetic | the (left) operand's type |
| `e as T` | `T` — casts are authoritative |

Inference is *minimal by design*: it types `:=` bindings, drives interpolation
formatting (§2.5), and enforces `mut` (§5.1). It does **not** yet verify
argument or operand compatibility — that is the N++ type checker (P1). Where
inference must guess (an unknown name), it assumes `i64`; use `as` to
override at the use site.

### 6.5 Static checks (v0.3–v0.4 — the N++ P1 checker)

The compiler rejects the following, each with a `file:line` diagnostic:

- **Undeclared variables** — every name used in an expression must be a
  binding or parameter in scope.
- **Unknown functions** — every callee must be a declared `fn` or an
  `extern syscall` entry (only named functions are callable in N).
- **Wrong argument count** — call arity must match the declaration.
- **Argument type mismatches**, under these compatibility rules: any two
  integer types are call-compatible (C conversion semantics then apply);
  `str` matches only `str`; pointers must agree in depth and in `#[user]`
  flavor (§3.2 — the flavor is checked first, so the byte-pointer
  wildcards cannot cross it), and base types
  must match unless either side is `*u8`/`*void` (byte pointers); an `as`
  cast changes the inferred type and is therefore authoritative.
- **Assignment to immutable bindings** (§5.1).
- **Binary-operand type errors** (v0.4): `str` values do not participate in
  any operator (build strings with interpolation); pointers support only
  comparison against a compatible pointer — **there is no implicit pointer
  arithmetic** in N: cast to `addr` (`p as addr + 1`) to compute on an
  address explicitly; everything else requires integer operands on both
  sides.
- **Return values** (v0.4): a `return` (or function-body tail expression)
  must carry a value exactly when the function declares a return type, and
  that value must be compatible with it.
- **Assignment values** (v0.4): the assigned value must be compatible with
  the target's type, and `+=`/`-=` require an integer target.
- **Struct correctness** (v0.5): struct names in literals and signatures must
  be declared; field access must name a real field (`str` exposes only
  `.ptr`/`.len`; integers have no fields); literals must initialize every
  field exactly once with compatible values; interpolation accepts only
  `str` and integer values; unary operators require integer operands.

Remaining outside the bootstrap's scope: *missing*-return flow analysis (a
typed function whose control flow can fall off the end is caught by the C
compiler on the generated file, not by `ncc`).

### 6.6 Indexing — `e[i]` (since v0.15)

```n
b := s[i];              // byte i of a str, as u8
q := p[i];              // element i of a *T, as T
h = (h ^ s[i] as i64) * 16777619;   // composes like any expression
```

Indexing is a postfix expression (same precedence tier as calls and
fields). Rules:

- The base must be a **pointer** (result: the pointed-to type, one level
  down; the `#[user]` flavor does not transfer — the element is a plain
  value) or a **str** (result: `u8`, the byte of the backing text).
- The index must be an integer.
- **Writes (since v0.16)**: `p[i] = x` (and `+=`/`-=`) stores through a
  **pointer** element; the value must be compatible with the element
  type. Two deliberate rules:
  - The pointer **binding** is not mutated by an element store, so it
    does not need `mut` — N's `mut` is a property of bindings; what the
    pointee holds is the program's contract, as in C.
  - Writing through a **str** index stays a compile error: `str` is an
    immutable view of its backing text, which is often a literal in
    read-only storage. (`s.ptr` remains the explicit raw escape — what
    you do through it is on you.)
- **Bounds are the programmer's contract**, as in C. `str` carries
  `len`, so bound your loops with it (`for i in 0..s.len as i64`); a
  checked access variant is n++ territory.
- This is the reason N has indexing but still no pointer arithmetic:
  `p[i]` names an element; `p + i` names an address. The first is what
  a self-hosted `ncc` needs to read its own source bytes (M5); the
  second stays behind the explicit `as addr` cast.

Note on `str.ptr`: the language types it `*u8`, and since v0.15 the
generated C agrees (the read site casts the backing `const char*`), so
binding it without a cast — `p := s.ptr;` — is well-formed.

### 6.7 Calls and fields

Function calls take positional arguments. Field access uses `.` and applies to
`str` values today (`.ptr`, `.len`); it generalizes to user types in N++.
Method-call syntax (`value.method()`) is reserved for N++ and rejected by
`ncc` with a clear error.

## 7. Code generation contract

This section specifies what C the compiler is *required* to emit, because N's
"no hidden runtime" promise is part of the language.

### 7.1 Lowering table

| N construct | Generated C |
|---|---|
| `extern syscall fn f(...) -> T = N` | `static inline T' f(...) { return (T')__nyx_syscall6(N, args…, 0…); }` |
| `fn f(a: A) -> R { … }` | `R' f(A' a) { … }` + forward prototype |
| `x := e;` | `T' x = e';` where `T` is the inferred type (§6.4) |
| `str` literal `"abc"` | `((nyx_str){"abc", 3})` |
| block tail `e` | `return e';` (in a value-returning function) |
| `never` return | `void` fn + `for (;;) {}` after the syscall |
| `x := match s { … }` (§5.6.1) | `T x = 0;` + `{ E __m = s'; T __mres = 0; switch (__m.tag) { … __mres = arm'; … } x = __mres; }` — the zero init is a dead store (the switch is exhaustive) kept so the C is warning-free |
| `return match s { … }` | same switch shape; defers run after `__mres` is computed, then `return __mres;` |
| `x := e?;` (§5.9) | `T x = 0;` + `{ R __t = e'; if (__t.tag == Err) { defers; return __t-or-rewrap; } x = __t.u.Ok.f; }` |
| `for i in a..b { … }` (§5.11) | `{ nyx_i64 __fs = a'; nyx_i64 __fe = b'; for (nyx_i64 i = __fs; i < __fe; i++) { … } }` |

### 7.2 The runtime

Generated code includes exactly one header, `nyxrt.h`, providing: the
primitive typedefs, `nyx_str`, `__nyx_syscall6` (the inline-asm syscall
primitive) and three formatting helpers (`__nyx_fmt_begin/_str/_i64`) that are
each bounds-checked and freestanding. Programs link `crt0.o + nyxrt.o` and
nothing else.

### 7.3 Interpolation lowering

A string with interpolations lowers to a hoisted buffer built *before* the
statement that uses it:

```c
char __b0[256];
nyx_str __s0 = __nyx_fmt_begin(__b0, 256);
__nyx_fmt_str(&__s0, __b0, 256, (nyx_str){"pid=", 4});
__nyx_fmt_i64(&__s0, __b0, 256, (nyx_i64)(getpid()));
put(__s0);
```

The buffer is declared in the enclosing block scope — **never** inside a
statement-expression — so the resulting `nyx_str` cannot dangle. (This rule
exists because the naive `({ ... })` lowering was tried and produced a
dangling pointer; it is a required property of any future backend.) Inside a
loop body the buffer is per-iteration. The `str` produced by an interpolation
is valid until the end of the enclosing block.

## 8. Grammar

Condensed EBNF of the implemented language:

```ebnf
program      = { item } ;
item         = [ "#[caps(syscall)]" ] ( extern_block | fn_decl )
             | [ "own" ] struct_decl | enum_decl | impl_block ;
             (* own: must-consume move semantics, 4.6 *)
impl_block   = "impl" ident "{" { method } "}" ;
method       = "fn" ident "(" "self" { "," param } ")" [ "->" type ] block ;

extern_block = "extern" "syscall" "{" { extern_fn } "}" ;
extern_fn    = "fn" ident params [ "->" type ] "=" int_lit [ ";" ] ;

fn_decl      = "fn" ident params [ "->" type ] block ;
params       = "(" [ param { "," param } ] ")" ;
param        = ident ":" type ;

struct_decl  = "struct" ident "{" field { "," field } [ "," ] "}" ;
field        = ident ":" type ;

type         = [ "#[user]" ] [ "raw" ] { "*" } ident ;
             (* #[user] requires at least one "*", excludes "raw" *)

block        = "{" { stmt } [ expr ] "}" ;          (* trailing expr = tail *)
stmt         = [ "mut" ] ident ":=" ( expr [ "?" ] | match_val ) ";"
             | expr ( "=" | "+=" | "-=" ) ( expr [ "?" ] | match_val ) ";"
             | expr "?" ";"                 (* v0.10: check-and-propagate *)
             | "return" [ expr | match_val ] ";"
             | "break" ";" | "continue" ";"
             | "defer" expr ";"          (* outermost function block only *)
             | "match" expr "{" arm { "," arm } [ "," ] "}"
             | "while" expr block
             | "for" ident "in" expr ".." expr block   (* v0.11 *)
             | "if" expr block [ "else" ( if_stmt | block ) ]
             | expr ";" ;
arm          = ident [ "(" ident { "," ident } ")" ] "=>" block ;
match_val    = "match" expr "{" varm { "," varm } [ "," ] "}" ;  (* v0.9 *)
varm         = ident [ "(" ident { "," ident } ")" ] "=>" expr ;

enum_decl    = "enum" ident "{" variant { "," variant } [ "," ] "}" ;
variant      = ident [ "(" field { "," field } ")" ] ;

expr         = or ;
or           = and  { "||" and } ;
and          = cmp  { "&&" cmp } ;
cmp          = bor  { ("=="|"!="|"<"|"<="|">"|">=") bor } ;
bor          = bxor { "|" bxor } ;
bxor         = band { "^" band } ;
band         = shift{ "&" shift } ;
shift        = add  { ("<<"|">>") add } ;
add          = mul  { ("+"|"-") mul } ;
mul          = cast { ("*"|"/"|"%") cast } ;
cast         = unary { "as" type } ;
unary        = ( "-" | "!" ) unary | postfix ;
postfix      = primary { "." ident
                       | "(" [ expr { "," expr } ] ")"
                       | "[" expr "]" } ;      (* indexing: reads v0.15, writes v0.16 *)
primary      = int_lit | "true" | "false" | string | interp_string
             | struct_lit | ident | "(" expr ")" ;
struct_lit   = ident "{" [ finit { "," finit } [ "," ] ] "}" ;
             (* not permitted directly in an if/while/match condition
                header — parenthesize it there (since v0.9) *)
finit        = ident ":" expr ;
             (* enum-variant literal: postfix `Enum.Variant{ finit, ... }`,
                payload-less reference: `Enum.Variant` *)
```

String interpolation is handled lexically: the lexer emits
`HEAD { expr } [MID { expr }]* TAIL` token runs with a brace-depth stack, so
interpolated expressions are parsed by the ordinary expression grammar.

## 9. Limitations (v0.1 — honest list)

These are known, deliberate gaps in the bootstrap; each is queued for the
N++/type-checker phase:

1. **No flow analysis.** Expression-level checking is complete (§6.5), but
   whether every control path of a typed function actually returns defers to
   the C compiler on the generated file.
2. **Interpolation is text-or-decimal only.** `str` inserts text, everything
   else formats as signed decimal; there are no hex/width format controls yet.
3. **Missing constructs:** closures, modules/`use`, generic
   `Result<T, E>` — all specified in the N++ design document. (`struct`
   landed in v0.5, `defer` in v0.6, `enum` + `match` in v0.7, `impl`
   methods in v0.8 — completing the N++ P2 stage — match-as-expression
   in v0.9, `?` over structural result enums in v0.10, and counted `for`
   loops in v0.11.)
4. **Match-expression and `?` positions are limited** (§5.6.1, §5.9):
   statement value positions only — no general expression nesting
   until the lowering needs it.
5. **No allocator-backed slice type yet** (§6.6): buffers are raw
   `sbrk`/`mmap` pointers with programmer-contract bounds; a
   length-carrying checked slice is n++ territory. (Raw index *writes*
   landed in v0.16.)
6. Fixed implementation caps (per file: 64 functions, 64 syscalls; per call:
   16 arguments; per function: 256 live locals) — generous for the bootstrap,
   diagnosed clearly when exceeded.

Resolved since v0.1: `:=` bindings now get concrete types with `i64` as the
integer default; interpolation dispatches by type; `mut` is enforced; the
generated C is strict C99 with no GNU extensions (TinyCC-compatible — this
removed `__auto_type`, which tcc does not support).

## 10. Toolchain

```bash
# build the compiler (any C99 compiler)
gcc -O2 -Wall -Wextra -o ncc lang/ncc/ncc.c

# transpile
./ncc lang/examples/hello.n -o hello.c

# compile for NyxOS (the user-space flags) and link
gcc -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone \
    -I user -c hello.c user/nyxrt.c
nasm -f elf64 user/crt0.asm -o crt0.o
ld -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 \
   -o hello.elf crt0.o nyxrt.o hello.o
```

See [`lang/ncc/README.md`](../ncc/README.md) for host-run testing (executing N
programs directly on Linux through the syscall-number shim) and the full
development workflow.

## 11. Versioning

The N spec version tracks the language, not the OS: v0.x are bootstrap
revisions (this document), v1.0 is the first checked language (type checker
enforcing §5.1/§9). Each spec change lands in the same commit as the compiler
change that implements it.
