# lang/selfhost — ncc, rewritten in N

The module ladder toward M5 (`ncc` self-hosted in N), named in
[docs/selfhost.md](../docs/selfhost.md) under "The mountain after the
toy": **`lex.n` → `parse.n` → `check.n` → `gen.n`** — each of ncc's
passes rewritten as a real N program, compiled by ncc, and held to
ncc's actual behavior **differentially**.

## lex.n — the lexer (rung 1: the stream)

`lex.n` is ncc's `next_token`, in N: the whitespace/comment skip
(nested block comments, depth-counted), the 22-keyword table, integers
(hex, `_` separators), strings with escapes and the **interpolation
brace stack** (16 deep, `T_INTERP_R` on a hole's close), the full
operator switch, and the three attributes — every kind number ncc's
own, `T_EOF` included.

**The contract**: `lex.n` reads `/tmp/n_lex_target.n` and prints one
`kind line` pair per token. Its output must be **byte-identical** to
`ncc --tokens` on the same file. Verified over the whole examples
directory *and over lex.n itself*:

```bash
gcc -O2 -o ncc lang/ncc/ncc.c
ncc lang/selfhost/lex.n -o lex.c && gcc -O2 -o nlex lex.c user/nyxrt.c -I lang/ncc/host
cp <source>.n /tmp/n_lex_target.n
diff <(ncc <source>.n --tokens) <(./nlex)     # empty = proven
```

23 sources, all byte-identical — the suite's stage `[8]` runs exactly
this loop and fails the build on any mismatch.

## parse.n — the parser (rung 1: the shape)

`parse.n` is the chain's second link, and the chain ACCRETES: the
file *contains* its lexer — lex.n's functions carried forward, the
way every link of the toy chain carried its predecessors (N has no
modules; ncc itself is one file) — wearing a two-token window (CUR +
NEXT, ncc's own lookahead plus the peek that tells `x := e` from an
expression statement). Its contract is `ncc --ast`: the POSTORDER
tree dump, byte for byte. The trick that made rung 1 land in one
piece: **recursive descent IS a postorder walk**, so the parser
prints as it parses and stores no tree at all — the tree
materializes when `check.n` needs one, not before.

**Coverage after rung 2** (the claimed targets, enforced by the
suite): everything but `impl` — struct and enum DECLARATIONS
(`#[drop]`/`own` included), extern blocks, functions, every
statement form (`match` in all four positions — statement, `:=`,
`=`, `return` — and `for` joined let/assign/return/expr, `while`,
`if`-`else`(-`if`), `defer`, `break`/`continue`), `?` propagation,
block tails, and the whole expression ladder at ncc's exact
precedence. **Verified byte-identical over the ENTIRE corpus — 24 of 24
sources: every example, lex.n, and parse.n PARSING ITSELF** (the
rung-1 circle, closed) **— including the whole toy compiler
nparse.n, 150K of source, 15,082 dump lines, exact.** `impl` joined
last: methods dump as `M` lines after the F section (ncc's METHODS
table, mirrored by a fifth pass; `self` leads, holds no
capabilities), and with it the parser's coverage ledger is EMPTY. Two designs made rung 2 fit:
the S 9 anchor line grew match-assign's lhs child and `aop` field
(both sides in one step, as always), and the dump's category
grouping — all D lines, then V/U, then X, then F — comes from
**four skip-passes over the file** rather than buffered text: each
pass prints one category and brace-counts past the rest, which is
safe because a string's braces never tokenize. Nothing is out: the
anchor grew its `M` lines (both sides in one step, as always) and
the last source fell.

**And parse.n is proven inside NyxOS too** — rung 2 included: the
in-OS run matched matchexpr.n's 84-line tree (declarations, `match`
in its expression positions, the extended `S 9` line) as well as
hello.n's, both dumped by the on-target build over serial. Rung 1's
first proof: the in-OS ncc (compiled
by the in-OS TinyCC) built it on target and its tree dump for
hello.n matched the C parser's, all 22 lines over serial. That run
earned its keep twice over — the first pass MISMATCHED, and the
culprit was the *C anchor*, not the N module: the dump printers used
`%lld`, which NyxOS's printf does not speak (`%l[dux]` only — the
project's own documented portability rule). The N side was right
both times; the differential caught the reference lying. Fixed to
`%ld`, both dumps agree on both worlds. **And the same
differential holds inside NyxOS**: the in-OS `ncc` (compiled by the
in-OS TinyCC) built `lex.n` on target, and both dumps — the C lexer's
and the N lexer's, over hello.n (73 tokens) and countdown.n (124) —
came back identical over serial. The lexer is proven on the machine
it exists for.

**Rung 2 — lexemes (landed)**: the stream carries substance now, on
both sides of the differential at once. `ncc --tokens` and `lex.n`
both print, per token: identifiers and `#[drop]` names **verbatim**
(`7 11 write`), integers as their **parsed value** — hex and `_`
separators normalized, so `0x811C9DC5` prints `2166136261` and the N
side's fold must match ncc's exactly — and string segments as their
**processed byte count** (`3 17 #18`: bodies hold control bytes, so
they are counted, not printed; the count still pins the escape rules,
since one miscounted escape shifts every line after it). Still 23/23
byte-identical — **and proven inside NyxOS with the lexemes on**:
the in-OS run matched hello.n's 73 enriched lines and countdown.n's
124, both dumps over serial. Errors print kind `-1` and stop; a `-1`
against a valid source means the lexer is wrong, and the differential
would already have caught it. Sources are capped at 256K (nparse.n,
the largest, is 150K).
