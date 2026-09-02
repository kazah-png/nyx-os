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
