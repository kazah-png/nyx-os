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
this loop and fails the build on any mismatch. **And the same
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
byte-identical. Errors print kind `-1` and stop; a `-1` against a
valid source means the lexer is wrong, and the differential would
already have caught it. Sources are capped at 256K (nparse.n, the
largest, is 150K).
