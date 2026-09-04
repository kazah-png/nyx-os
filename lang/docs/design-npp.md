# N++ — Design Document

**Status:** P1–P5 staged into `ncc` (complete at bootstrap scale); **the front-end era is open — §6 is the plan** · **Base language:** [N](spec-n.md) · **Compiler:** `nppc`

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
global discipline. (Shipped in the bootstrap: `#[drop(fn)]` wires an
ordinary consuming function as the destructor — spec §4.6.)

### 2.5 OS integration: GUI and tasks

The compositor and scheduler get language-level bindings. This section is
the **binding-surface draft**: concrete enough that the kernel side can be
built toward it, honest about what runs today.

#### What the kernel has, and why it does not cross the boundary

NyxOS windows today are **kernel-side constructs**: `window_create()` takes
a *draw callback*, and every app (terminal, editor, games) lives inside the
kernel, painting through `window_draw_fn` and receiving input through
`on_key`/`on_click`/`on_tick` function pointers (`kernel/gui/core/
compositor.h`). Function pointers cannot cross a syscall boundary, so a
user-space window needs the model **inverted**:

- the *process* owns its pixels — a plain buffer in user memory;
- the *kernel* composites them — a present call blits the buffer into the
  window's client area;
- input arrives as **data, not callbacks** — the process polls a
  per-window event queue whenever it likes.

The precedent already exists in-tree: `SYS_FBINFO`/`SYS_FBPRESENT`/
`SYS_GETKEYEVENT` (54–56) do exactly this for fullscreen programs (DOOM).
Windowed is the same idea with an id and a queue.

#### Proposed syscall surface (57–60)

| # | Call | Signature (kernel view) | Notes |
|---|---|---|---|
| 57 | `win_create` | `(w, h, title_ptr, title_len) -> id` (or −1) | client-area size; title copied at the crossing |
| 58 | `win_destroy` | `(id) -> 0/-1` | idempotent close |
| 59 | `win_present` | `(id, buf, w, h) -> 0/-1` | full client-area blit of a `u32` XRGB buffer; kernel validates the user range (`user_ptr_ok`) and clips |
| 60 | `win_poll_event` | `(id, ev_ptr) -> 1/0/-1` | copies one event out, or 0 if the queue is empty |

The event record is four `i64` words — `{ kind, a, b, c }` — with kinds:
`1` key (a = keycode), `2` click (a = x, b = y, c = button), `3` move
(a = x, b = y, c = buttons), `4` close-requested, `5` resize (a = w,
b = h). A fixed small queue per window (say 32 events, oldest dropped)
keeps the kernel side allocation-free.

Deliberate non-goals for v1: no damage rectangles (present is whole-area),
no shared memory mapping (present copies — correctness first), no
callbacks, no vsync contract beyond "present blits when called".

#### The N-side binding — the P5 pieces meet

The handle model is the `own` story arriving at its destination: a window
is an `own struct` and its destructor is `win_destroy` — leaking a window
becomes a compile error, dropping one closes it.

```n
#[caps(syscall)]
extern syscall {
    fn win_create(w: i64, h: i64, t: #[user] *u8, tl: isize) -> i64 = 57
    fn win_destroy(id: i64) -> i64                                  = 58
    fn win_present(id: i64, buf: #[user] *u32, w: i64, h: i64) -> i64 = 59
    fn win_poll_event(id: i64, ev: #[user] *i64) -> i64             = 60
}

#[drop(close_window)]
own struct Window { id: i64 }

#[caps(syscall)]
fn close_window(win: Window) {
    win_destroy(win.id);
}
```

An event loop in **current N syntax** (this compiles today against the
extern block above — `while`, indexing, `#[user]` crossings and `own`
all exist):

```n
fn event_loop(win: Window, fb: *u32, w: i64, h: i64) {
    mut running := true;
    while running {
        ev := alloc_words(4);
        while win_poll_event(win.id, ev as #[user] *i64) > 0 {
            if ev[0] == 4 {                 // close requested
                running = false;
            }
            if ev[0] == 2 {                 // click: paint a dot
                fb[ev[2] * w + ev[1]] = 0xFFFFFF;
            }
        }
        win_present(win.id, fb as #[user] *u32, w, h);
    }
    close_window(win);      // a held param is the callee's manual duty (sink rule)
}
```

The auto-close reads best at the birth site. Note how the capability
gate composes with the handle: only the audited `open_window` wrapper
touches the syscall — `main` holds no capability, cannot call
`win_create` directly (the checker refuses), and still cannot leak the
window:

```n
#[caps(syscall)]
fn open_window(w: i64, h: i64, title: str) -> Window {
    Window{ id: win_create(w, h, title.ptr as #[user] *u8, title.len as isize) }
}

fn main() -> i64 {
    win := open_window(220, 120, "hello");
    put("window {win.id} open\n");          // field reads peek, no move
    0
}                                           // ← win was LIVE: #[drop] closes it
```

The `gui.Window.new(...)? / win.on(Event.Click, fn(e) …)` sugar stays
`n++`-front-end territory (closures, `Result`); the binding above is the
floor it lowers to.

#### Staging — honest

| Piece | Status |
|---|---|
| N language surface (extern block, `own` + `#[drop]` handle, event loop) | **compiles today** — every construct shipped v0.12–v0.19 |
| Kernel syscalls 57–60 | **do not exist** — the ask above |
| Compositor support | needs a *user-buffer window* variant: a `window_t` whose draw callback blits from the owning process's presented buffer, plus a per-window event queue filled where `on_key`/`on_click` fire today |
| P5 gate ("a windowed N++ app on the NyxOS desktop") | **unblocked — 57–60 landed (v6.4.354), and nwin.n runs** |

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

*(This sketch predates self-hosting. §6 revises the target: the
front-end now lowers to **N source**, and the verified N pipeline —
ncc or the selfhost toolbox — carries it the rest of the way.)*

## 4. Staged roadmap

| Stage | Contents | Gate |
|---|---|---|
| P1 | ✅ shipped in `ncc` v0.2–v0.4: inference (`i64` literals, typed interpolation, `mut`) + the expression-level checker (names, arity, argument/operand/return/assignment types) | all `lang/examples` still compile bit-identically ✅ |
| P2 | ✅ **complete**: `struct` (v0.5) · `defer` (v0.6) · `enum` + `match` (v0.7) · `impl` methods (v0.8, static dispatch) | structs.n, defer.n, enums.n, methods.n |
| P3 | ✅ **complete** (bootstrap side): match-as-expression (v0.9) · `?` over structural Ok/Err result enums (v0.10 — same-type pass-through, cross-type Err rewrap, defers honored) · fs bindings sketch (`fsio.n`: the negative-return→Result boundary conversion of §2.2, `?` chains, deferred close). Generic `Result<T, E>` and the idiomatic `.npp` rewrite move to the `n++` front-end (P5 era) | `fsio.n` runs the §2.2 shape end-to-end |
| P4 | ✅ **complete** (bootstrap side): `#[user]` pointer flavor (v0.12 — hard no-implicit-conversion boundary, `as` as the audited crossing) · `pageflags` W^X bitset (v0.13 — total compile-time W^X proof, live-mmap verified) · capabilities (v0.14 — `#[caps(syscall)]` gates extern blocks, direct callers must hold the cap, wrapper = audited boundary). The canonical-half range proof, further capability names (`mmio`, `ports`), and ring heights move to the `n++` front-end and kernel-side modules | a kernel-module example checked at `ring0` |
| P5 | 🔨 **started**: `own` structs shipped (v0.17 — move-not-copy, must-consume; leaks/double-use/discards are compile errors), the tracking is **branch-aware** (v0.18 — `if`/`else` arms may consume, both exits must agree, a `return`-ending arm is exempt; moves stay refused in loops, `while` conditions, and match arms), and **destructors shipped** (v0.19 — `#[drop(fn)]` wires an ordinary consuming fn; live values auto-drop at scope end, defers first then drops LIFO, held params never auto-drop, no drop flags). The own-types contract of §2.4 is complete. And the GUI bindings are REAL: the kernel implemented the drafted surface verbatim in v6.4.354 (syscalls 57–60, the 4×i64 event encoding — [#77](https://github.com/kazah-png/nyx-os/issues/77) closed), and [nwin.n](../examples/nwin.n) is the running program — an `own`+`#[drop]` Window handle over a real kernel resource, the gradient-and-square frame presented from a process-owned buffer, events polled, gracefully skipping where no desktop composites it. **P5 complete at bootstrap scale** | a windowed N++ app on the NyxOS desktop — the N bootstrap ran first |

P1 is the enabling investment: everything later depends on the checker
existing. It also immediately improves plain N (better errors from `ncc`).

## 5. Open questions

Recorded here so decisions are made deliberately, not by accident:

1. **Generics monomorphization vs erasure** — **decided (§6.2): monomorphization**
   (C++-style, zero-cost, larger binaries; N has no runtime to erase into).
2. **String ownership** — `str` is a view; N++ needs an owning string for
   builders. Candidate: `String` as an `own struct` over the future
   user-space allocator.
3. **Closure representation** — **decided (§6.2): explicit capture struct +
   top-level function**, the classic lambda-lifting lowering; a fat-pointer
   calling convention only if dynamic dispatch is ever needed.
4. **`n++` binary name on VFS** — `n++` is a valid NyxOS filename today; if
   shell parsing ever conflicts, fallback name is `npc`.

## 6. The n++ front-end era — the staged plan

The bootstrap staging (§4) is finished: every P-row shipped inside `ncc`,
and N is self-hosted with a byte-verified, installable toolchain
(`docs/selfhost.md`). What remains of the N++ vision is exactly what a
*separate front-end* exists for — generics, closures, modules — and this
section is its plan, written the way `selfhost.md` was written for M5:
shape first, then rungs, then the verification each rung must survive.

### 6.1 The compiler shape — lower to N source

Three shapes were on the table:

1. **`nppc` lowers `.npp` to `.n` source** — the front-end handles the
   new constructs (instantiate generics, lift closures, resolve modules)
   and emits plain N; the existing pipeline (`ncc`, or the self-hosted
   toolbox) carries it to C and the OS. **Chosen.**
2. Lowering straight to C — one less hop, but it forks the backend: two
   emitters to keep honest, and the lowered program bypasses every
   verified stage the last months built.
3. Growing `ncc` further — no new dialect, but generics and modules are
   front-end-sized complexity; the single-file bootstrap stays simple
   precisely because they live elsewhere (§1's whole argument).

Shape 1 wins on the strength the project already paid for: the lowered
`.n` is **checked by ncc's own type/ownership/capability checker** (a
free soundness net under the new front-end — a lowering bug that
produces ill-typed N is caught, loudly, by a compiler that is itself
held byte-faithful by the selfhost differentials), it is readable the
way §3 wanted the C to be readable, and every `.npp` program
automatically exercises the verified ladder end to end. The cost — one
more textual hop — buys the entire trust chain.

### 6.2 The feature ladder

**Monomorphized generics + `Result<T, E>`** (the flagship). Generic
`fn` and `struct`/`enum` declarations are templates the front-end
instantiates on use; each instantiation becomes a plain N item with a
mangled name (`__g_<name>_<type>…`, stable and readable), and
`Result<T, E>` becomes the declared-pair result enums v0.10 already
compiles — the structural bridge is shipped, the front-end only
automates the declaring. Duplicate instantiations dedupe by mangled
name. No erasure: N has no runtime to erase into. Shipped as M6.3a–f
(§6.4); [`result.npp`](../examples/result.npp) is the flagship end to
end.

**Closures** — lambda lifting. A closure literal becomes an explicit
capture `struct` plus a top-level function taking it as its first
parameter; the checker computes the capture set (by value; an `own`
capture moves and the struct inherits must-consume). What today is
written as the nwin event loop's manual dispatch becomes
`win.on(Event.Click, fn(e) …)` sugar over exactly that lowering.

**Modules / `use`** — file-level units. A module is a file; `use`
imports its public items (`pub` marks them); resolution is a topological
walk with cyclic imports refused; the front-end concatenates the
instantiated, resolved program into the single `.n` unit the pipeline
already consumes. No link-time machinery — the flat model the OS
toolchain already trusts.

### 6.3 The verification story — differentials from day one

Every rung lands the way the N rungs landed: worked examples + negative
tests in the same commit, and a differential fence in the suite from the
first day — `nppc` lowers an `.npp` example to `.n`, then **both** `ncc`
and the self-hosted toolbox compile it and their C must agree
byte-for-byte (the [8d]-class guarantee extending up one layer). The
lowered `.n` is committed beside its source where it aids review, and
regenerated-and-compared by the suite where it does not (the [1c]
pattern). `nppc` itself starts as hosted C99 beside `ncc.c` — one file,
same toolchain discipline — and the self-hosting question is deferred
until the dialect stabilizes (the N ladder showed the way; it can be
climbed again when it is worth climbing).

### 6.4 Milestones

| Stage | Contents | Gate |
|---|---|---|
| M6.1 | This plan | ✅ this section |
| M6.2 | ✅ `nppc` skeleton shipped ([`../nppc/nppc.c`](../nppc/nppc.c)): the read side is ncc’s lexer whole (same tokens, escapes, interpolation stack, attributes, diagnostics — accept and refuse exactly as the N pipeline would), lowering is the identity on the N subset, [`hello.npp`](../examples/hello.npp) is the founding contract as a file, and the suite’s stage [10] is the day-one fence. The parse tree arrives with M6.3’s first real transform | ✅ hello.npp lowers byte-identically and `ncc` + the self-hosted `ngen` agree on its C |
| M6.3a | ✅ **generic structs** monomorphize ([`box.npp`](../examples/box.npp)): `struct Box<T>` + explicit uses `Box<i64>` / `Box<u8>` lower to concrete `__g_Box_<T>` structs (type parameter substituted, uses rewritten, everything else spliced through verbatim) via a token-span rewrite — no full parser needed, no inference. Suite stage [10b] | ✅ box.npp monomorphizes and the lowered N compiles identically under `ncc` and `ngen` |
| M6.3b | ✅ **generic functions** ([`genfn.npp`](../examples/genfn.npp)): `fn id<T>(x: T) -> T` + explicit calls `id<i64>(...)` lower to concrete `__g_id_<T>` functions (signature type slots substituted, body spliced through verbatim, calls rewritten). Type parameters must sit in a handled type slot (`:` / `->` / `*`); a use elsewhere is refused for now. Suite stage [10c] | ✅ genfn.npp monomorphizes and the lowered N compiles identically under `ncc` and `ngen` |
| M6.3c | ✅ **body type slots** ([`gcast.npp`](../examples/gcast.npp)): a cast to a type parameter (`x as T`) inside a generic function body substitutes like the signature does — the one type slot N bodies have, since locals are always inferred (`x := e`). Suite stage [10d] | ✅ gcast.npp monomorphizes and the lowered N compiles identically under `ncc` and `ngen` |
| M6.3d | ✅ **call-site inference** ([`ginfer.npp`](../examples/ginfer.npp)): `id(41)` infers `T = i64` from the literal argument and lowers to the same `__g_id_i64` as the explicit `id<i64>(41)` (dedup to one instantiation). Literal-driven for now (int / string / bool); a value nppc cannot read asks for the explicit form. Suite stage [10e] | ✅ ginfer.npp: inferred and explicit calls share one instantiation; the lowered N agrees under `ncc` and `ngen` |
| M6.3e | ✅ **generic enums** ([`genum.npp`](../examples/genum.npp)): `enum Maybe<T>` + explicit uses `Maybe<i64>` (type positions and construction `Maybe<i64>.Some{...}` / `Maybe<i64>.None`) lower to a concrete `__g_Maybe_i64` (payload type slots substituted); `match` arms need no rewrite. Reuses the fn span-splice. Suite stage [10f]. **The flagship end to end** ([`result.npp`](../examples/result.npp)): one `enum Result<T, E>`, two instantiations (`__g_Result_i64_i64`, `__g_Result_str_i64`), and N's structural `?` composing with them unchanged — same-type pass-through and the cross-instantiation Err rewrap. Stage [10g] runs it on the host and checks the output | ✅ genum.npp monomorphizes and the lowered N compiles identically under `ncc` and `ngen`; result.npp lowers, agrees, and runs correctly |
| M6.3f | ✅ **construction-site inference** ([`rinfer.npp`](../examples/rinfer.npp)): `Result.Ok{ v: n }` / `Maybe.None` with no `<...>` take their type arguments from the enclosing function's declared return type (`-> Result<i64, i64>`) and lower to the instantiation that return type already names — one definition each. Inference never guesses: a bare construction in a function returning something else is refused with the explicit form spelled out (inside a generic function it is a nested use — M6.3g). Suite stage [10h] | ✅ rinfer.npp: inferred constructions share the return type's instantiation; the lowered N agrees under `ncc` and `ngen` and runs correctly; 3 negatives refused |
| M6.3g | ✅ **generics inside generics** ([`gnest.npp`](../examples/gnest.npp)): a template may use another template with its own type parameters — `fn wrap<T>(x: T) -> Box<T> { Box<T>{ val: x } }`, `fn some<T>(x: T) -> Maybe<T> { Maybe.Some{ v: x } }`. A use inside a template is recorded on the template instead of instantiated at once; each concrete instantiation of the template then instantiates, to a fixpoint, the inner generics it needs with its arguments substituted, and the template's emit rewrites the uses (explicit, bare construction, or inferred call) to the concrete names. A type parameter may sit in a generic's argument list; one anywhere the lowering does not rewrite is still refused. Suite stage [10l] | ✅ gnest.npp: `wrap<i64>` / `wrap<str>` / `some<i64>` bring `__g_Box_i64` / `__g_Box_str` / `__g_Maybe_i64` into being; `ncc` and `ngen` agree; host output correct; a nested use with a concrete argument and an inferred call inside a template run; 1 negative refused |
| M6.3h | ✅ **generic struct fields of generic type** ([`gfield.npp`](../examples/gfield.npp)): `struct Pair<T> { a: Box<T>, b: T }`, `struct Two<A, B> { x: Box<A>, y: Box<B> }` — a field's generic type is a nested use of the struct, so each concrete `Pair<i64>` or `Two<i64, str>` instantiates the boxes it needs (nothing else has to mention `Box<i64>`) and names them concretely. The generic a field names is resolved once every generic is known (an enum declared later is fine); a non-generic type with arguments, or a wrong arity, is refused. Generics now compose across structs, functions, and enums alike — nothing pending. (A struct field of *enum* type by value is an N limitation today: ncc emits every struct before every enum.) Suite stage [10m] | ✅ gfield.npp lowers to `struct __g_Pair_i64 { a: __g_Box_i64, b: i64 }` and `__g_Two_i64_str`; `ncc` and `ngen` agree; host output correct; `Pair<i64>` alone brings `Box<i64>` into being once; 2 negatives refused |
| M6.4 | Closures (lambda lifting, `own` captures). Scouted: N has no function types (a type is `#[user]? raw? *… IDENT`), so a lifted closure has nothing to be passed *as* — an N rung adding `fn(T) -> U` types (ncc + the four self-hosted modules, in lockstep) precedes the first closure rung | an event-handler example over the nwin surface; escape/negative tests |
| M6.5a | ✅ **modules** ([`modmain.npp`](../examples/modmain.npp) + [`modlib.npp`](../examples/modlib.npp)): a module is a file; a top-level `use "file.npp";` (path relative to the using file) inlines that file's resolved text in its place — once per program (a repeated `use` reduces to a marker comment), cyclic uses refused, a missing file refused — and the generic pass then runs over the whole program, so a template declared in one file instantiates and dedups from another. `pub` marks exports and is dropped from the lowering. The lowered `.n` keeps `// use "…" (inlined by nppc)` / `// end of "…"` markers, so the flat unit stays reviewable; diagnostics after a `use` count lines of the combined text. Suite stage [10i] | ✅ modmain.npp lowers to one flat unit; `ncc` and `ngen` agree on it; host output correct; missing file and cycle refused; a repeated use inlines once |
| M6.5b | ✅ **visibility**: an item — a top-level `fn`, `struct`, or `enum` — is visible in the file that declares it, and `pub` makes it visible program-wide; a name-shaped reference (a call, a construction or generic use, a type slot, a type argument, an `impl` type) from another file to a non-`pub` item is refused with the file named (`'is_even' is private to modlib.npp (mark it pub to use it here)`), the main file's own items included. A module's private items are renamed `__m_<stem>_<name>` at their declaration and at every reference inside the module — a text splice before the generic pass, so a private generic, or a private type as a type argument, works unchanged — and two modules' private `helper`s stay two functions. Each module may declare its own `extern syscall` block (ncc accepts the repeat). [`modlib.npp`](../examples/modlib.npp) keeps `is_even` private. Suite stage [10j] | ✅ modmain.npp lowers with `__m_modlib_is_even`; `ncc` and `ngen` agree; host output correct; a private call from the main file and a module's reference to a main-file item are refused; two modules with a private `helper` each (plus the main file's own) compile and run; a private generic over a private type instantiates |
| M6.5c | ✅ **`pub` scoped to importers**: an exported item is visible to its own file and to the files that `use` its module directly. A use-graph — one edge per `use`, files by id — is recorded during resolution, and a name-shaped reference from a file that does not use the item's module is refused with the fix spelled out (`'bee' is declared by bb.npp, which main.npp does not use (add use "bb.npp";)`): a transitive reach (main uses A, A uses B, main names B's item) and a sibling reach (A names B's item, B used only by main) are both refused. A diamond (A and B both use C) inlines C once and both see it; a file using a module directly as well as through another still sees it. Suite stage [10k] | ✅ transitive and sibling references refused; the diamond program compiles identically under `ncc` and `ngen` and runs; direct-and-through use runs |
| M6.5d | ✅ **re-exports** ([`modutil.npp`](../examples/modutil.npp) + [`modreexp.npp`](../examples/modreexp.npp)): `pub use "file.npp";` inlines the file exactly as `use` does and makes its exports part of the using module's own — a file that uses that module sees them without naming the re-exported one, along any chain of `pub use`s; a plain `use` inside the chain exports nothing onward and the reach is refused with the fix named. The use-graph carries the flag per edge. Modules: nothing pending. Suite stage [10n] | ✅ modreexp.npp reaches put / Maybe<T> / first_even through modutil's `pub use`; `ncc` and `ngen` agree; host output correct; a plain use inside a pub-use chain is refused; a two-level pub-use chain reaches |
| M6.6 | `nppc` self-describes | an `.npp` program written in the dialect's own idioms exercises every rung at once |

The rungs are sized like M5's were: one honest increment each, docs in
the same commit, nothing claimed that a fence does not hold.
