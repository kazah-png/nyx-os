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

**Honest scope**: this rung is the *stream* lexer — kinds and lines.
It advances through every construct exactly as ncc does, but captures
no lexemes: identifier bytes, string bodies and integer values arrive
with the rung that needs them (`parse.n`). Errors print kind `-1` and
stop; a `-1` against a valid source means the lexer is wrong, and the
differential would already have caught it. Sources are capped at 256K
(nparse.n, the largest, is 150K).
