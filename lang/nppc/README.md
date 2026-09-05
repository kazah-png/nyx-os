# nppc — the N++ front-end compiler

`nppc` compiles N++ (`.npp`) by lowering it to **N source**; the verified N
pipeline — `ncc`, or the self-hosted toolbox (`nlex`/`nparse`/`ngen`) —
carries the lowered program to C and into NyxOS. The shape decision and the
staged plan live in [`../docs/design-npp.md`](../docs/design-npp.md) §6: the
lowered `.n` is checked by ncc's own type/ownership/capability checker, so
the front-end always works above a soundness net that is itself held
byte-faithful by the selfhost differentials.

```
file.npp ──nppc──► file.n ──ncc / toolbox──► C ──cc/tcc──► ELF
```

## Build and use

```
gcc -O2 -Wall -Wextra -o nppc lang/nppc/nppc.c
./nppc program.npp -o program.n
```

One file, C99, no dependencies — the `ncc` discipline.

## What this rung is (M6.2), plainly

The accepted dialect is **exactly the N subset**, and lowering is the
**identity**: the input is written out byte-for-byte. What makes the
skeleton real is the read side — the whole file is lexed with a faithful
copy of ncc's lexer (same tokens, escapes, interpolation mode stack,
attributes, and diagnostics), so everything `nppc` accepts is something
the N pipeline lexes identically, and everything it refuses would have
been refused there too. This holds the founding contract mechanically:

> Every valid N program is a valid N++ program, with identical behavior.

[`../examples/hello.npp`](../examples/hello.npp) is that contract as a
file — `hello.n`'s content under the `.npp` extension — and the suite's
stage [10] is the day-one differential from design §6.3: nppc lowers it,
the output must equal the `.n` byte-for-byte, and then **both** `ncc` and
the self-hosted `ngen` compile the lowered program and their C must agree.

Each M6.3+ rung (monomorphized generics, closures, modules) replaces a
slice of the identity with a real transform — new syntax lands in this
lexer and lowering first, never in `ncc`.

## Generics (M6.3), plainly

`nppc` monomorphizes. A generic `struct`, `fn`, or `enum` is a template;
each distinct use becomes one plain N item with a mangled name
(`Box<i64>` → `__g_Box_i64`, `Result<str, i64>` → `__g_Result_str_i64`),
every use is rewritten to that name, and everything else — comments,
spacing, the bodies themselves — is spliced through verbatim. It is a
token-span rewrite over the lexed file, no parse tree yet, and the
lowered `.n` is then held to the N checker like any hand-written N. A
concrete item is emitted where its template is declared, so a type a
template will be instantiated with is declared before the template, as
any struct used by value would be.

| Rung | Example | What lowers |
|---|---|---|
| M6.3a | [`box.npp`](../examples/box.npp) | generic structs, explicit type arguments |
| M6.3b | [`genfn.npp`](../examples/genfn.npp) | generic functions: signature type slots substituted, body verbatim |
| M6.3c | [`gcast.npp`](../examples/gcast.npp) | `x as T` inside a generic body — the one type slot N bodies have |
| M6.3d | [`ginfer.npp`](../examples/ginfer.npp) | call-site inference: `id(41)` is `id<i64>(41)` (literal arguments) |
| M6.3e | [`genum.npp`](../examples/genum.npp), [`result.npp`](../examples/result.npp) | generic enums; `Result<T, E>` composing with N's structural `?` |
| M6.3f | [`rinfer.npp`](../examples/rinfer.npp) | construction-site inference: `Result.Ok{ ... }` from the enclosing return type |
| M6.3g | [`gnest.npp`](../examples/gnest.npp) | generics inside generics: `wrap<T> -> Box<T>` instantiates `Box` per `wrap` instantiation |
| M6.3h | [`gfield.npp`](../examples/gfield.npp) | generic struct fields of generic type: `Pair<T> { a: Box<T> }` instantiates `Box` per `Pair` instantiation |

Inference never guesses. A call whose argument `nppc` cannot type, or a
bare construction in a function that does not return that enum, is
refused with the explicit form spelled out in the diagnostic. A template
may use other templates with its own parameters (`wrap<T> -> Box<T>`):
the use is recorded on the template and instantiated, to a fixpoint,
once per concrete instantiation of it, with the arguments substituted —
in a function's signature or body, an enum's payload, or a struct's
field (`Pair<T> { a: Box<T> }`) alike. Generics compose; nothing is
pending on that front.

## Modules (M6.5), plainly

A module is a file. A top-level `use "lib.npp";` inlines that file's
text in its place — the path is relative to the using file — and the
program the generic pass sees is the flat result, so a template declared
in one file and instantiated in another monomorphizes and dedups exactly
as it would in one file. The rules are the ones design §6.2 names:

- **once per program** — a second `use` of the same file, from anywhere,
  leaves only a marker comment;
- **cycles refused** — a file that names one still being resolved is an
  error at the offending `use`;
- **an item is visible in its file; `pub` makes it visible to the files
  that `use` its module.** A top-level `fn`, `struct`, or `enum` without
  `pub` is private to the file that declares it — the main file's own
  items included — and a reference to it from any other file is refused
  with the file named: `'is_even' is private to modlib.npp (mark it pub
  to use it here)`. An exported item is visible to the files that `use`
  its module *directly*: a file that reaches the module only through
  another `use`, or a sibling that never uses it, is refused with the fix
  spelled out: `'bee' is declared by bb.npp, which main.npp does not use
  (add use "bb.npp";)`. A diamond — two modules using the same third —
  inlines it once and both see it. `pub use "lib.npp";` re-exports: the
  used module's exports become part of this module's own, so a file that
  uses this module sees them too, along any chain of `pub use`s; a plain
  `use` inside the chain exports nothing onward. "Reference" means a
  name-shaped use:
  a call `x(`, a construction or generic use `x.` / `x{` / `x<`, a type
  slot, a type argument, an `impl` type. A field or method after `.`, or
  a binding or parameter name, is not one.
- **private items never collide.** Each non-`pub` item of a module is
  renamed `__m_<stem>_<name>` (`__m_modlib_is_even`) at its declaration
  and at every reference inside the module, so two modules' private
  `helper`s are two functions in the lowered N. The renaming is a text
  splice done before the generic pass, which then sees the mangled names
  as ordinary ones — a private generic, or a private type used as a type
  argument, needs nothing special. The main file's items keep their
  names, and `pub` itself is dropped from the lowering (N has no
  visibility). Each module may declare its own `extern syscall` block.

The lowered `.n` keeps `// use "lib.npp" (inlined by nppc)` and
`// end of "lib.npp"` markers around each inlined file, so the flat unit
stays reviewable. Diagnostics after a `use` count lines of the combined
text. [`../examples/modmain.npp`](../examples/modmain.npp) and
[`../examples/modlib.npp`](../examples/modlib.npp) are the two-file
program the suite's stages [10i]–[10n] hold — lowered, agreed on by
`ncc` and `ngen`, run on the host, with a missing file, a cycle, a
private call, and an unreached export refused;
[`../examples/modreexp.npp`](../examples/modreexp.npp) reaches modlib
through [`../examples/modutil.npp`](../examples/modutil.npp)'s `pub use`.
Nothing is pending on the module front.

## Closures (M6.4), plainly

A lambda is a function literal where a value goes:

```
each(fn(x: i64) -> i64 { x * x }, 1, 2, 3);
op := Op{ name: "inc", run: fn(x: i64) -> i64 { x + 1 } };
```

`fn ( params ) [-> type] { body }` in an expression position — a call
argument, a struct-literal field, the right-hand side of a binding, a
return value. nppc lifts each one to a top-level function named `__c_N`
(numbered in the order they are lifted) placed right after the item
before the one the lambda appears in, and the expression becomes that
name — a function value, which N v0.24's function types then type
exactly like a named function passed by name. The body is spliced
through verbatim; the lowered N of the first line above is
`fn __c_0(x: i64) -> i64 { x * x }` followed by `each(__c_0, 1, 2, 3)`.
A lambda is told from a function type by its body: `fn(i64) -> i64` in
a type slot has none.

The rules at this rung (M6.4a):

- **a lambda in a closure slot captures by value (M6.4c2).** Where an
  `Fn` type is expected (below), a lambda may name the enclosing
  function's locals. Each is copied into an environment struct `__E_N`
  when the closure is born — a generated maker `__mk_E_N` puts the copy
  on the bump heap (`sys_sbrk`, 16 bytes per field, never freed; nppc
  declares the syscall when the program does not) and the closure
  carries its address as `env` — and the lifted function reads them back
  as `__e.name` (a captured closure is called through its `call` field),
  so the closure may outlive its frame: `adder(5)` returns one. A
  captured local's type must be evident to a token scan — a declared
  parameter, a literal (`i64`, `str`, `bool`), a call to a known
  function, a struct literal, or a name so typed; anything else is
  refused: `cannot capture 'n': its type is not evident — bind it with a
  literal, a call or a struct literal, or pass it as a parameter`. An
  `own` value captured is moved, as N's rules say.
- **elsewhere, lambdas capture nothing.** In a plain `fn(...)` slot or a
  `:=` binding there is no environment to keep a capture in: naming a
  local of the enclosing function there is refused with the fix named.
  (A local a token scan cannot see, such as a `match` arm's bind, is
  refused by N instead, as an undeclared variable in the lifted
  function.) Inside a generic template a capture works the same way: the
  environment struct and its maker come out as templates over the type
  parameters the captured types mention (`keep<T>` keeps an `x: T` in
  `__E_N<T>`, made by `__mk_E_N<T>`), instantiated with the function
  around them (M6.4c3).
- **lambdas nest.** The pass runs to a fixpoint, innermost first, so a
  lambda inside a lambda is lifted before the one around it copies its
  text.
- **inside a generic template, a lambda is generic too (M6.4b).** A
  lambda in the body of `fn boxed<T>` that names `T` lifts to a template
  of its own — `fn __c_N<T>(v: T) -> Box<T> { … }`, over exactly the type
  parameters it mentions — and the expression becomes the use `__c_N<T>`:
  a nested generic use like `Box<T>`, instantiated once per concrete
  instantiation of the enclosing function and rewritten to the concrete
  name (`__g___c_N_i64`), which N passes as a function value. A lambda
  that names no type parameter lifts as a plain function. (A type
  parameter inside a function type, `f: fn(T) -> Box<T>`, is a handled
  type slot of the generic pass since this rung.)
- **a `:=`-bound lambda needs a declared function type.** N binds a
  function value only when its type is declared somewhere in the program
  (a parameter, a field, or a return type of that signature) — the rule
  N's spec §3.4 states; the lowering adds nothing to it.

**The closure type `Fn` (M6.4c1).** N's `fn(i64) -> i64` is a bare
function: it has nowhere to keep an environment, so a closure needs a
type of its own. `Fn(i64) -> i64` is that type — it may appear in any
type slot (a parameter, a struct field, a return type) — and nppc lowers
each distinct signature to one N struct:

```
struct __Fn_i64__i64 {
    env: addr,
    call: fn(addr, i64) -> i64,
}
```

declared once, ahead of the program's first struct or function; the
struct's name is the signature's spelling with its punctuation as
underscores (`Fn()` is `__Fn__`, `Fn(*u8, Box<i64>) -> bool` is
`__Fn_pu8_Box_i64__bool`). A call through a closure — a parameter of
`Fn` type, a local bound from a call returning one, or the `Fn` field of
a struct-typed parameter or local — becomes `f.call(f.env, a)`. A lambda
written where a closure is expected takes the environment as its first
parameter and the expression becomes the closure value
`__Fn_i64__i64{ env: 0, call: __c_N }`; a named function passed there is
wrapped in an adapter of the same shape (`fn __c_N(_env: addr, a0: i64)
-> i64 { dbl(a0) }`). The environment is 0 when the closure captures
nothing; a capturing one carries its environment struct's address (the
capture bullet above).
[`../examples/closurety.npp`](../examples/closurety.npp) is the worked
example, held by stage [10q];
[`../examples/capture.npp`](../examples/capture.npp) captures, stage
[10r].

**`Fn` inside generic templates (M6.4c3).** An `Fn` that names a type
parameter of the template around it — `fn apply<T>(f: Fn(T) -> T, x: T)`
— is left as written while the generic pass instantiates the template,
and the closure pass runs once more over the result: `Fn(i64) -> i64`
inside `__g_apply_i64` is concrete then, so it becomes the same
`__Fn_i64__i64` a plain function uses (declared once, whichever pass
meets it first), its calls are rewritten, and a closure made outside
(`inc := adder(1); twice<i64>(inc, 40)`) passes straight in. A lambda in
such a slot lifts as a generic `__c_N<T>` and its closure value is
spelled with the slot's type until that second pass names the struct —
`Fn(T) -> T{ env: 0, call: __c_N<T> }` — where the slot's type is the
callee's parameter type with the call's explicit arguments standing in
for the callee's type parameters (`apply<i64>(fn(v: i64) -> i64 { … },
40)` reads `Fn(i64) -> i64`, and a lambda in `main` stays plain). A
named function passed to a generic call gets its adapter in that second
pass.
[`../examples/gfnclosure.npp`](../examples/gfnclosure.npp) is the worked
example, held by stage [10s].

**Function and closure types as fields of generic structs (M6.4c3b).**
A generic struct's field may be a function type or a closure type that
names the struct's type parameters — `struct Pair<T> { first: T,
second: Fn(T) -> T }`, `struct Op<T> { f: fn(T) -> T }`. The generic
pass keeps such a field as the type's token span and spells it out per
instantiation with the parameters substituted, so `__g_Pair_i64` reads
`second: Fn(i64) -> i64` and the closure pass names it `__Fn_i64__i64`
like any other slot; a lambda in a `Pair<T>{ … }` literal inside a
template lifts as a generic closure, one in a concrete `Pair<i64>{ … }`
literal lifts plain, and a call through the field, `p.second(0)`, is
rewritten like a call through any closure field. A generic use inside
such a field's type (`f: fn(T) -> Box<T>`) is refused for now, with the
fix named.
[`../examples/gstructfn.npp`](../examples/gstructfn.npp) is the worked
example, held by stage [10t].

[`../examples/closure.npp`](../examples/closure.npp) is the worked
example: four lambdas — an argument, a struct field, another argument,
a binding — lifted to `__c_0`…`__c_3`, held by the suite's stage [10o]
(lowered, agreed on by `ncc` and `ngen`, run on the host; nested lambdas
run; a capture refused).
[`../examples/gclosure.npp`](../examples/gclosure.npp) is the generic
case, stage [10p]: a lambda in `boxed<T>` lifts to `__c_0<T>` and comes
out as `__g___c_0_i64` and `__g___c_0_str`; one in `count<T>` that names
no type parameter lifts plain.
