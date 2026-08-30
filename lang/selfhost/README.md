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

## check.n — the checker (rung 1 landed)

The third module's contract lives in
[tests/bad/](tests/bad/): sixty-six wrong programs and a manifest of
ncc's exact first-error lines. `check.n` must reproduce them —
location and wording — for every file it claims; the corpus doubles
as ncc's own regression battery, so the reference and its rewrite
keep each other honest.

**Rung 1 — the tree + the first errors (landed)**: the parser's code
accretes one more time, transformed — every parse function now
returns a NODE INDEX instead of printing, and the tree the toy
proved in miniature materializes at real scale: one parallel-array
arena (kind / children / line / span / next-link, eight words a
node), child lists in a flat side arena, and the five category
passes collapsed back to ncc's own single pass (errors have no dump
order to buy; declarations land in tables). On top of the tree: a
merged extern+fn table (ncc's fn_lookup order), a scoped name table
with ncc's VARS discipline — params seeded immutable, innermost
shadows, blocks truncate on exit — and a checker walk that mirrors
ncc's own sequence (assignment checks lhs, then rhs, then `mut`;
calls resolve, then arity, then arguments). The first error prints
as `line: message` and stops, exactly as ncc dies on its first.

Claimed and byte-exact — the manifest rows with the filename column
stripped (the normalization rule lives in
[tests/bad/README.md](tests/bad/README.md)):

   bad_undecl       2: undeclared variable 'y'
   bad_mut          3: cannot assign to immutable 'x' (declare it with 'mut')
   bad_unknown_fn   2: unknown function 'launch'
   bad_arity        3: 'write' takes 3 argument(s), got 1
   bad_for_mut      3: cannot assign to immutable 'i' (declare it with 'mut')
   bad_struct_mut   4: cannot assign to immutable 'r' (declare it with 'mut')
   bad_caps         7: 'write' requires the syscall capability — ...
   bad_cname        1: function name 'double' is a C keyword — ...
   bad_defer_nested 4: defer is only allowed in the function's outermost block
   bad_argty        6: argument 1 to 'put': expected str, got i64
   bad_userptr      6: argument 2 to 'write': expected #[user] *u8, got *u8
   bad_assign_ty    3: cannot assign i64 to a str target
   bad_index_valty  4: cannot assign str to a u8 target
   bad_index_write  3: cannot write through a str index — ...
   bad_ret_void     2: 'return' with a value in a function with no return type

plus two POSITIVE targets — hello.n and countdown.n check clean, and
their required output is silence. Coverage, stated honestly: extern
blocks and functions parse for real (full bodies, the whole
expression ladder, for-loop variables seeded in their body scope);
struct/enum/impl items brace-skip and `match` refuses — their
manifest rows arrive with the decl-table and match rungs. The host
shim's sbrk arena grew 1M → 8M (ncc/host/nyxrt.h): the checker's
node arena was the first thing to outgrow it, and the differential
reported the overflow as six segfaults before it could pass for an N
bug. Suite stage [8c] holds all eight targets.

**Rung 2 — the structural family (landed)**: three more rows, no
types needed, each a small judgment over machinery rung 1 already
built. The **capability gate** reads the caps bit the fn table
already carries — and mirrors ncc's shape exactly: `xfn_caps` is
XFNS-only, so fn-to-fn calls never gate, and the check sits between
existence and arity (a call that is wrong twice reports the
capability first, ncc's order — probed differentially). The
**C-keyword name refusal** is parse-time, at the function's name,
because ncc's `pexp` fires it before the program is even whole; the
24-entry keyword table lands in kwlook's length-gated shape. The
**defer depth judgment** is the first thing the defer case does —
before its expression resolves (ncc's S_DEFER order) — with
BLOCK_DEPTH tracked exactly as ncc tracks it: incremented per block,
1 meaning the function's own. Nine rows claimed, all byte-exact;
positive silences unchanged.

**Rung 3 opens — the type family**: `infer_type` arrives in N.
`Ty` was hiding in plain sight — the parser's `struct T` (ptrs,
is_user, name-span) is ncc's `Ty` exactly, with an empty span as
ncc's NULL name (void). Synthesized names (an int literal is `i64`
with no source text to point at) use the INTERN TRICK: the six fixed
names are appended after the source in the same buffer, so spans
stay uniform and the lexer never sees them. The name table carries
each binding's inferred type, the fn table carries full parameter
records and return types, and `cinfer` transliterates ncc's
`infer_type` case for case — literals, paths, calls, str's `.ptr`
and `.len`, unary and binary (comparisons to bool, arithmetic by the
left operand), casts as the authoritative source, indexing down one
pointer level. `tcompat` is ncc's `ty_compat` in order: the
`#[user]` boundary first (so the byte-pointer wildcards cannot
smuggle across), exact name+depth, integer inter-conversion, then
the `*u8`/`*void` escapes. Five rows landed with it — argument
types (including the `#[user]` mismatch, rendered `#[user] *u8`
exactly), assignment targets, and the str-index write refusal —
and ncc's own function table had to grow from 64 to 128 on the way:
the checker became the biggest N program yet written. Struct
fields, enum semantics and method returns still fall back softly
(their tables are the next rungs); the claimed rows never reach
them.

**The return judgments (row 15)**: `CUR_RET` rides the checker's
state (set per function from the recorded return type), and the
return case mirrors ncc's three branches in order — a value in a
no-return function (the claimed row), the value/return-type
mismatch, and a bare `return` in a value-returning function — with
`never` counting as no-type, exactly as ncc's `is_never` does.

**Proven inside NyxOS (batches V45 + V46)**: the in-OS ncc+tcc
pipeline compiled check.n on target twice over. V45 ran it against
bad_mut.n; V46 ran it against a row from EACH new family —
bad_caps.n (structural) and bad_argty.n (types) — and both first
error lines matched the in-OS ncc's own, location and wording, with
only the filename column normalized away (two-sided on-target
differentials: reference and rewrite both ran inside the OS). V46's
in-OS ncc also compiled the 75-function checker itself — the 64→128
function-table growth, exercised by the operating system's own
compiler. V45 additionally closed the parser's last on-target gap
(methods.n's 53-line impl/M dump, byte-identical). Zero panics both
times; the VFS census held its exact baseline (219/512 live,
high-water 221) — the node pool recycles.

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
