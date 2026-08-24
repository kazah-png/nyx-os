# N++ — Design Document

**Status:** staged into `ncc` (P1–P5 complete at bootstrap scale) · **Base language:** [N](spec-n.md) · **Compiler (planned):** `n++`

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

1. **Generics monomorphization vs erasure** — leaning monomorphization
   (C++-style, zero-cost, larger binaries); decide at P2 with real data.
2. **String ownership** — `str` is a view; N++ needs an owning string for
   builders. Candidate: `String` as an `own struct` over the future
   user-space allocator.
3. **Closure representation** — fat pointer (fn ptr + env ptr) vs
   monomorphized call sites; needed by P5 for event handlers.
4. **`n++` binary name on VFS** — `n++` is a valid NyxOS filename today; if
   shell parsing ever conflicts, fallback name is `npc`.
