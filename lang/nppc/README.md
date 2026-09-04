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

Inference never guesses. A call whose argument `nppc` cannot type, or a
bare construction in a function that does not return that enum, is
refused with the explicit form spelled out in the diagnostic. Still
pending: generic uses inside another generic's body — each
instantiation is emitted from the template's own source span, which
cannot carry a nested rewrite yet.

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
  inlines it once and both see it. "Reference" means a name-shaped use:
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
program the suite's stages [10i]–[10k] hold — lowered, agreed on by
`ncc` and `ngen`, run on the host, with a missing file, a cycle, a
private call, and an unreached export refused. Still pending: re-exports
(`pub use "file.npp";`).
