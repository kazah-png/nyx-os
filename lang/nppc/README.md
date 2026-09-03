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
lowered `.n` is then held to the N checker like any hand-written N.

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
