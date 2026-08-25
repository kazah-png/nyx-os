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
| **An AST** | ✓ expressions AND statements — whole bodies parse to a tree, then check → fold → emit run as passes (only fn headers + the fndef hop-over emit direct) | ✓ — parse → check → gen |
| Types (everything is `i64` in the toy) | ✓ a real TYPE COLUMN — int/str inferred bottom-up on the tree, per-name types fixed for life, int-only ops refuse strings with located errors | full checker |
| structs / enums / match / defer / own / caps | ✗ | ✓ |
| `str` values and string literals as data | ✓ at toy scale — strings bind, load, and print (a string IS its table index at run time; the type column keeps the kinds apart) | ✓ |
| Error diagnostics (`line N: message`) | ✓ expect points (rung 2) + a semantic pass: unknown variable, call arity | ✓ everywhere |

The structural gap *was* the AST row. On-the-fly emission works because
the toy checks nothing; a checker needs to *look at* the program before
code exists, which is why `ncc` builds `Expr`/`Stmt` trees first. Rung 3
closed it for the expression tier: the rules build a postorder node
array (`nk/nl/nr/nv` parallel i64 arrays in one by-value struct, plus a
flat four-slot argument table per call node), and a separate walker
emits by postorder recursion — verified byte-identical to the fused
emitter's code, so the architecture moved *under* the outputs.
Statements still emit directly; the seam is one function (`gen_cmp`).

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
3. **A postorder AST in nparse** — ✅ **landed**: the expression rules
   build parallel node arrays (kind/lhs/rhs/value in one by-value
   struct, calls with a flat four-slot arg table — the `fp[f*4+j]` move
   again) and `emit_node` walks the tree in postorder. The walker
   reproduces the fused emitter's code byte for byte — same values,
   same word counts — and the first dividend arrived immediately: with
   a whole call in hand as data, the parser checks argument count
   against parameter count ("wrong number of arguments"), a diagnostic
   the emit-on-the-fly version had no natural seat for. Honest cut:
   statements and control flow still emit directly (the jump-patching
   tier is unchanged); folding them into the tree is a later rung.

All three rungs are in — and rung 3's promised payoff arrived on its
heels: the toy now runs a real **check pass** (`check_node`) between
build and emit. Every load must name a *bound* variable — the enclosing
function's parameters plus the names bound by prior `:=` statements —
and anything else is refused with the node's recorded source line
(`line N: error: unknown variable`; nodes carry lines in a sixth Ast
array). Scope is conservative the way ncc's v0.18 branch merge is
conservative: a binding made inside an `if` arm or a `while` body dies
at the block's closing brace, because the checker cannot promise the
branch ran — so `if c { b := 1; } b` is refused, as are `x := x + 1`
self-feeds (a name binds only *after* its right-hand side checks) and
function bodies reading main's names (the flat runtime table would
allow it; the checker enforces the lexical story).

And the tree's *other* dividend followed: a **fold pass** (`fold_node`)
rewrites constant subtrees in place between check and emit — a binop
over two folded numbers becomes a number computed at compile time, a
negation likewise, call arguments fold while calls never do, and
DIV/MOD by a literal zero stay unfolded (the compiler must not crash
computing what the program would; the VM owns that failure). The
folding demo emits 10 code words where the unfolded tree emits 19 —
the first time the toy's output got *better* than what the fused
parser-emitter produced, which is the point: by the time the fused
version had seen both operands, their PUSHes were already emitted.
Check runs before fold so diagnostics describe the program as written.

Statements then joined the tree: bind/if/while are node kinds, blocks
are seq *chains* (a cons list in the same parallel arrays), and each
BODY — a function's or main's — is one finished tree over which the
three passes run in order (`gen_body`: check → fold → emit, the
compiler's whole shape in eight lines). The jump-patching craft moved
from the parser into `emit_stmt` unchanged in shape, scope moved from
parse-time threading into the check walk where it belongs, and the
output did not move a byte — verified against the previous build, only
the banner differs. Only the outermost skeleton still emits as it
parses: function entry recording, parameter tables, the hop-over JMP —
headers, not meaning. Statement-level transforms followed at once:
an `if` whose condition folds to a constant **dissolves into its live
arm** (the node is rewritten in place into a seq wrapper — every
walker already handled seq and empty chains generically, so no other
pass changed), a `while` over constant false vanishes, and a
constant-true `while` is left alone because an infinite loop is the
program's right. The demo `if 1 > 2 { x := 999; } y := 5; y` emits 7
words where the unfolded tree needs 18 — and the dead arm was still
checked first, because dead code must still be legal code.

The second value kind arrived next: **string literals and `print`**.
A double-quoted literal lexes into a side string table (bytes plus
start/len spans — the interned-names pattern again, carried as one
by-value struct because N caps functions at 16 parameters and the
toy's plumbing was at the door), `print "text";` compiles to a PRINTS
opcode with a table index, `print expr;` to the expression plus PRINT
— the operand checks and folds like any expression, so `print 6 * 7`
emits `PUSH 42`. The staging is deliberate: a string is legal ONLY
directly after `print`, because the moment strings flow as values —
into bindings, comparisons, calls — the node arrays need a **type
column** to keep the kinds apart.

And the column landed: `nt` types every expression node (0 int,
1 str), inferred bottom-up by the same check walk that owns scope — a
load carries the type its name was bound with (a per-symbol table in
the checker's `Scope` bundle), and **a name's type is fixed for its
lifetime**, because the flat runtime table genuinely cannot tell an
integer from a string index — `x := 1; if c { x := "s"; } print x;`
would otherwise leave x's kind up to which way `c` ran. Strings now
bind, load, and print (`print` is ONE form; the operand's inferred
type picks PRINT or PRINTS at emit — same syntax, different code,
decided by the checker), while arithmetic, comparisons, conditions,
call arguments and body values stay int-only, each refusal located
("cannot use a string in arithmetic or comparison", "a name cannot
change type", "a condition must be an integer").

The column's second dividend followed: **typed string equality**. `==`
over two strings is legal — and is a *different operation*: the
checker rewrites the node's opcode from EQ to STREQ (byte-compare the
spans, push 0/1) — overload resolution at toy scale, with the tree
recording the resolved operation so the emit walker stays type-blind
for operators. Since `==` yields an int, `if name == "nyx" { ... }`
composes with plain conditions for free; two literal strings fold at
compile time (the fold pass carries the string table now). And the
pair is symmetric: `!=` over two strings resolves the same way to
STRNEQ — the identical byte-compare pushing the flipped answer, one
generalized fold arm covering both. Mixed comparisons and the
ordering operators on strings stay refused.

Concatenation answered its design question cleanly: `+` over two
strings resolves (same checker rewrite) to a CONCAT opcode whose
result type is *str* — the first operator that **makes** a string, so
concatenations bind, print, compare, and chain. The append mechanism
is one and shared: the table's write cursors live behind a pointer
field (`Strs.cur`), seeded by the lexer; the VM advances them when a
runtime CONCAT appends a fresh entry, and the **fold pass advances the
same cursors** when two literals concatenate at compile time — one
mechanism, two moments, and a fold-made entry survives into the run
(the demo PRINTS one). Runtime strings are per-program transient (the
next lex reseeds), and capacity is trusted like every toy buffer.

And the toy is FILE-DRIVEN end to end now — not just the lexer (rung
1, ntokens) but the whole compiler: an audited `#[caps]` wrapper reads
a toy-language source file the binary does not embed, and the same
pipeline lexes, parses, checks, folds, emits, and runs it. The move
that made it one pipeline instead of two: `lex` takes raw bytes plus
a length (`*u8 + i64` — ncc's own SRC shape) rather than a `str`, so
embedded demos hand over their `.ptr`/`.len` and a file buffer
arrives as itself. N still cannot wrap a pointer back into a `str` —
and now nothing ever asks it to.

Subset growth is now the whole remaining ladder, and it has begun:
the toy compiler honors **line comments** (they vanish in the lexer
like whitespace, with the line counter still ticking so diagnostics
stay honest around them) and carries the **full comparison set** —
`!=`, `<=`, `>=` joined `<`, `>`, `==` as two-char tokens with
dedicated opcodes, each folding over constants like every other
operator (`print 3 <= 3;` emits `PUSH 1`) — and `!=` resolves over
strings too (STRNEQ, above), so equality is the string-legal pair
and ordering stays int-only. **`else if`
chains** came free: the else slot always held a statement index and a
nested `if` is a statement, so the parser just recurses instead of
demanding braces — no walker changed, and dead-arm elimination
cascades straight through a chain of constant conditions (a three-way
chain over constants collapses to the one live `print`, 6 words).
And `len(s)` completed the string toolkit — make (`+`), compare
(`==`), **measure**: a typed builtin (str in, int out, "len takes a
string" otherwise), one STRLEN opcode, and `len` of a literal folds
to a plain number at compile time. `len` is a reserved word now, like
`print`. Bare `!` closed the integer operator set — logical not as a
factor (int-only, folding over constants), unambiguous because the
`!=` two-char arm eats its pair before a lone `!` can ever be seen. The subset
grows until the toy parses the examples directory — at which point it
stops being a toy.

## Definition of done for M5

`ncc` rewritten in N means: an N program that reads `.n` source, produces
the same C the C-hosted `ncc` produces for the supported subset, compiles
with the in-OS toolchain, and rebuilds itself inside NyxOS. The toy chain
is the training ground; every rung above retires one difference between
the training ground and the real thing.
