# The Road from Toy to Self-Host (M5)

**Status:** planning note, updated as rungs land · **The chain:**
[ntokens.n](../examples/ntokens.n) · [ncalc.n](../examples/ncalc.n) ·
[nemit.n](../examples/nemit.n) · [nstack.n](../examples/nstack.n) ·
[nparse.n](../examples/nparse.n)

M5 is the milestone where `ncc` is rewritten in N. The toy compiler chain
has been climbing toward it since v0.15 — this note is the honest parity
audit: what the toy already proves, what still separates it from `ncc`'s
real front-end, and the next three rungs, ranked.

## What the toy chain already proves

The five examples together form a complete compiler at toy scale, and
every architectural pattern in them is the real one:

| Pattern | Where | Why it matters for self-host |
|---|---|---|
| Token buffer between lexer and parser | nparse `lex()` | the real pipeline shape — no character rescanning |
| Interned symbols (names buffer + spans) | nparse | ncc's `vars_find`/`vars_add` in miniature |
| Postfix emission falling out of the descent | nparse, nemit | exactly `gen_expr`'s discipline |
| Branch patching (JZ placeholder → patch) | nparse if/while | the emitter skill every jump needs |
| Function addresses recorded before bodies | nparse fndef | forward references and self-recursion |
| Caller-save call convention, multi-param | nparse | calling conventions are *designed*, not given |
| A VM executing the emitted code | nstack, nparse | closes the loop: output is *runnable* |

All of it runs inside NyxOS, compiled by the in-OS toolchain — the
pipeline is proven on target, end to end.

## Parity audit — lexer

`ntokens.n` + `nparse.n`'s `lex()` vs `ncc.c`'s `next_token()`:

| Feature | Toy | ncc |
|---|---|---|
| Line comments, string literals w/ escapes, two-char operators | ✓ | ✓ |
| Keywords + multi-letter interned identifiers | ✓ | ✓ |
| Decimal integers | ✓ | + hex `0x`, `_` separators |
| Nested block comments | ✗ | ✓ |
| Attributes `#[...]` | ✗ | ✓ |
| **Interpolation mode stack** (`T_STR_HEAD/MID/TAIL`) | ✗ | ✓ — the hardest lexer feature |
| Line/column tracking for diagnostics | ✗ | ✓ |
| **Input from a file** | ✓ — rung 1 landed | ✓ |

The last row is the cheapest and the most symbolic: the toy has never
lexed anything it did not carry as a literal. `fsio.n` already proved
kernel `open`/`read` from N — wiring the two together makes the first
*file-driven* self-host artifact.

## Parity audit — parser, checker, emitter

| Feature | Toy | ncc |
|---|---|---|
| Precedence expression grammar + unary | ✓ (3 tiers) | ✓ (full table) |
| Statements, `if`/`while`, functions, recursion | ✓ | ✓ |
| `else` | ✓ — rung 2 landed | ✓ |
| **An AST** | ✗ — emits during the descent | ✓ — parse → check → gen |
| Types (everything is `i64` in the toy) | ✗ | full checker |
| structs / enums / match / defer / own / caps | ✗ | ✓ |
| `str` values and string literals as data | ✗ | ✓ |
| Error diagnostics (`line N: message`) | ✓ at the parser's expect points — rung 2 | ✓ everywhere |

The structural gap is the AST row. On-the-fly emission works because the
toy checks nothing; a checker needs to *look at* the program before code
exists, which is why `ncc` builds `Expr`/`Stmt` trees first. The toy can
grow one incrementally: parse expressions into a postorder node array
(`kind/lhs/rhs/val` in parallel i64 arrays — every technique already in
hand), then emit from the tree instead of the descent.

## Does N itself have enough?

Mostly yes — the language grew its self-host muscles deliberately:

- **buffers**: `sbrk` + index writes (v0.16) give growable working memory;
- **file input**: `open`/`read`/`close` bindings proven in-OS (fsio);
- **output**: emitting C is sequential text — `write()` directly, no
  string-builder needed;
- **bytes and spans**: `s[i]` reads, pointer indexing, interning — proven;
- **missing**: nothing *blocking* at subset scale. Conveniences that would
  help: an owning string / dynamic array type (§9, n++ territory), and
  struct arrays (today: parallel i64 arrays, which work but read raw).

## The next three rungs, ranked

1. **File-driven lexing** — ✅ **landed**: ntokens.n now opens a real
   `.n` file (hello.n), reads it into sbrk memory through an audited
   `#[caps]` wrapper, and lexes the buffer — 77 tokens counted by kind.
   The design finding held: N cannot wrap a pointer + length back into
   a `str`, so the file scanner walks `p[i]` pointer reads — exactly
   ncc's own `SRC` walk. First artifact that lexes N it did not embed.
2. **`else` + diagnostics in the toy** — ✅ **landed**: `if cmp block
   else block` compiles as the classic diamond (JZ patched to the else
   entry, the then-arm's JMP patched past it all), every token records
   its source line, and the parser's expect points (`)`, `;`, braces,
   trailing input) refuse bad input with `line N: error: ...` — first
   error wins. A parser became real: wrong input gets a message.
3. **A postorder AST in nparse** — parse expressions into node arrays,
   emit from the tree. The architecture leap that makes a toy *checker*
   possible, and the last structural difference from `ncc`'s pipeline.

Beyond those, the ladder is: a type column on the AST (i64/str to start),
then the subset grows until the toy parses the examples directory — at
which point it stops being a toy.

## Definition of done for M5

`ncc` rewritten in N means: an N program that reads `.n` source, produces
the same C the C-hosted `ncc` produces for the supported subset, compiles
with the in-OS toolchain, and rebuilds itself inside NyxOS. The toy chain
is the training ground; every rung above retires one difference between
the training ground and the real thing.
