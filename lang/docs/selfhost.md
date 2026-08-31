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
| Decimal integers, hex `0x`, `_` separators | ✓ (lowercase hex at toy scale) | ✓ |
| Nested block comments | ✓ — depth-counted, the line counter ticking through | ✓ |
| Attributes `#[...]` | ✓* — lexed and deliberately ignored (surface parity; the semantics are ncc’s) | ✓ |
| **Interpolation mode stack** (`T_STR_HEAD/MID/TAIL`) | ✓ at toy scale — segment tokens + one mode flag (single level) | ✓ — the hardest lexer feature |
| Line/column tracking for diagnostics | ✓ — line*1000+col packed in one array, `fail()` decodes | ✓ |
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
| structs / enums / match / defer / own / caps | structs ✓, enums+match ✓ COMPLETE at toy scale (payloads, arm binders, checker-proved exhaustiveness), and defer ✓ WHOLE — ncc's v0.6 rule to the letter (fn-scoped, LIFO at exit, value first, outermost-block only); own/caps ✗ — stated out of scope, they stay ncc's | ✓ |
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

## The next mountain: parsing N itself

An honest accounting. The toy now covers its parity tables — but it
parses its OWN language, not N. "Parses the examples directory" means
the toy reading real `.n` files, and the first target is the canonical
one: **hello.n**. Read token by token against the toy's current
surface, the gap is smaller than expected — the years of rungs were
quietly converging on N's shape all along:

**Already 1:1**: `//` and `/* */` comments · `fn name(args)` · `:=`
bindings · calls (zero-arg included) · string literals **with
interpolation holes** (the toy lexes `{pid}` today) · `: Name`
parameter annotations · statement `;` rules · the `0` tail.

**The six rungs that remain**, ranked and scoped:

1. **String escapes** — ✅ **landed the same hour it was scouted**:
   `\n \t \r \0 \\ \" \{ \}` in both segment scanners (an unknown
   escape keeps its bytes as written). The escaped brace never
   reaches the lexer's hole test, so braces-as-text and
   interpolation finally coexist — N's own rule — and hello.n's
   `msg` string now lexes.
2. **Return annotations + builtin type names** — ✅ **landed**:
   `-> i64` parses on fns (every body held to the toy's one return
   kind, int — the tail check was already enforcing it), and the
   twelve builtin type names are reserved words now (token kind of
   their own), accepted in parameter annotations and all mapping to
   the toy's single numeric kind — stated, not pretended. Landing it
   forced the identifier rule fully honest: `i64` must be ONE token,
   so identifiers now read N's true `[A-Za-z_][A-Za-z0-9_]*` (digits
   after the first letter — the toy could not spell `i64` before).
3. **`as` casts, parsed and discarded** — ✅ **landed**: in the
   postfix position — so `5 as u8 * 3` is `(5 as u8) * 3`, N's own
   precedence for free — `as` consumes optional `*`s and a type name
   and keeps the value untouched: every toy value is an i64 word,
   types cannot change it, and the toy says so (the stated asterisk,
   like attributes).
4. **`str` fields** — ✅ **landed**: `ptr` joined `len` as a reserved
   word, and the postfix `.` resolves both AT PARSE — `.len` builds
   the existing len node (same opcode, same folding; `t.len` and
   `len(t)` emit identical words) and `.ptr` is the identity, no node
   at all (a string IS its table index; the asterisk stated). Other
   fields on a string refuse with "a string has ptr and len only".
5. **`extern syscall` blocks** — ✅ **landed, and hello.n's own shape
   parses verbatim**: the block records name/number/param-count per
   binding (param names are documentary — any word serves, which
   matters because hello.n names one `len`, the toy's own keyword); a
   call to a binding compiles to one SYSCALL opcode the VM services
   by NUMBER — `write` (1) pushes its span through the same audited
   boundary PRINTS uses **and the demo actually prints through it**,
   `getpid` (6) answers a constant, anything unmapped answers -1, the
   host shim's own rule. The shape is real; the kernel is the VM —
   stated plainly.
6. **`fn main` as the program** — ✅ **landed**: when the tokens end
   right after the fndefs there is no tail to demand — the program is
   one CALL to main (the two words a real 0-argument call site would
   emit), main's name found by byte-comparing each fndef's interned
   symbol against `m a i n` — no keyword, no lexer special case (so
   `MAIN` is just a function). A tail-less program without a main
   refuses ("a program needs statements or a main"); a main with
   parameters refuses too. And the rung hello.n hid between the named
   six: a bare `write(...);` is a CALL STATEMENT now — the toy's
   first real lookahead (scan to the matching `)`, nesting counted,
   then ask for the `;`) tells a call statement from a call tail by
   N's own rule — statements end in `;`, the tail does not — and the
   dropped value POPs.

**THE GRADUATION HAPPENED**: `run_file` pointed at **hello.n itself**
compiles the canonical N program off the disk — its `extern syscall`
block, its `fn main() -> i64`, its interpolated `{pid}`, its casts,
its bare `write(...);` — and the program PRINTS: `hello from N!
pid=7`, through its own write, 33 code words. That is the moment the
toy stopped being a toy and became what M5 always meant it to become:
the seed of `ncc` in N.
Out of scope, stated once: raw pointers, `#[caps]` semantics, `own`,
generics — those stay ncc's, and the toy keeps saying so.

## The second file: countdown.n

The graduation names its own sequel. Read token-by-token against the
toy, **countdown.n** — the second example ever written — is three
rungs away, and the first two are already in:

1. **`: str` parameter annotations** — ✅ **landed**: `str` is not a
   kind-22 token (that family is numeric), so it arrives as a plain
   name and `spells_str` resolves it by its bytes — the spells_main
   move again; the toy's second kind joins `fpt`, call sites are held
   to it ("argument type does not match the parameter" for an int,
   while a str into a *plain* parameter keeps its old refusal), and
   the body sees the parameter as a str — `s.len` and `print s` just
   work.
2. **Void bodies** — ✅ **landed**: `fn put(s: str) { write(...); }`
   has statements and no tail; when the closing brace stands at tail
   position the body compiles as statements only and its value is 0
   — the toy has one kind and it rides, stated asterisk (a call
   statement drops it anyway, which is how countdown uses `put`).
   Together these two are exactly countdown's `put(s: str)` shape.
3. **`mut` and assignment** — ✅ **landed, sweep and all**: `mut` is
   a keyword, `mut name := e;` records mut-ness on the bind (N's
   default stays immutable), and `name = e;` is an ASSIGNMENT
   statement — the checker holds it to bound + mut + same type, each
   refusal located, the immutability message in ncc's own words
   ("cannot assign to an immutable name (declare it with mut)").
   Params and match binders are immutable too. And the rule swap was
   honest about the divergence it exposed: in N, `:=` always DECLARES
   and shadowing is legal (innermost wins — ncc.c's own lookup); the
   toy's flat symbol-keyed table CANNOT shadow, so a re-`:=` of a
   live name refuses ("already bound: assign to a mut name (the toy
   cannot shadow)") instead of silently mutating — refuse loudly
   rather than pretend. The sweep converted every rebinding demo
   (gcd/fact/max, the disk program's while-sum) to the honest mut +
   `=` form — same values, same word counts to the byte, because
   `mut` emits nothing and `=` is the bind's own STORE.

**THE SECOND GRADUATION HAPPENED**: `run_file` pointed at
**countdown.n** compiles the second example ever written, whole and
verbatim — its `put(s: str)` void helper, its mut counter assigned
down the while loop, an interpolated bind re-executed per iteration,
a call inside an interpolation hole — and the program counts down:
five ticks, then `liftoff! from pid 7`, through its own write, 104
code words. (Landing it forced one honest capacity bump: the token
arrays grew 96 → 192 words — countdown runs ~125 tokens.) Both
canonical N examples now compile through the toy, on the host and
inside NyxOS.

## After the graduations: `defer`, whole

The last parity row that was ever going to land, landed — and it is
ncc's v0.6 rule to the letter, because the toy's shape happened to be
exactly the shape the rule was designed for. `defer expr;` registers
the expression in textual order; the registered expressions run in
**LIFO** order at the body's exit, **after** the tail value computes
(deferred values POP off above it — "a defer cannot change what the
function returns" falls out of the stack discipline); names resolve
at the **registration point** (the defer node checks in place, in the
statement chain, with the bound-count of its textual position) while
values read at **exit time** — both exactly ncc's C lowering. And the
v0.6 restriction came along whole: `defer` is legal only in the
body's outermost block, because a conditionally-registered defer
would need a runtime defer stack — nested ones refuse with ncc's own
words. The emitter's trick is worth stating: LIFO comes from
**post-order recursion** over the statement chain — recurse to the
chain's end, emit on the way back — no defer array, no stack, no cap.
The toy's bodies are single-exit (tail-only returns), so "every exit
path" is one exit — the honest asterisk on an otherwise complete row.
`own` and `caps` remain stated out of scope: they are semantic
systems, not parseable rungs, and the toy keeps saying so.

## The mountain after the toy

The parity table is at its honest end — every parseable row ✓, both
canonical programs compiling through the toy — so the question M5 has
been building toward can finally be asked plainly: **what now?** The
toy was the trainer. It proved every architectural pattern the real
compiler needs — token buffer, interned symbols, a postorder AST,
check/fold/emit as passes over it, a VM, file-driven compilation —
at toy scale, in N, on NyxOS. The mountain after the toy is the same
climb at full scale: **ncc's own passes, rewritten in N as real N
programs, each held to ncc's actual behavior differentially.** The
module ladder: `lex.n` → `parse.n` → `check.n` → `gen.n`, living in
`lang/selfhost/` when the first one lands.

**The first module is scouted and feasible.** ncc's lexer is ~65
token kinds (the toy speaks 28), 22 keywords, the full operator set
(compound assigns, logical and bitwise, `..`, `=>`, `?`, brackets),
attributes as real tokens, and interpolation — and N already has
everything the rewrite needs: sbrk'd arrays, byte reads, interning
(all proven in the toy's own `lex()`). **The road does not go through
the language** — no missing feature blocks it.

And the harness side is DONE: **`ncc --tokens`** dumps the C lexer's
exact stream — one `kind line` pair per token, `T_EOF` included — so
the contract is mechanical: `lex.n`, compiled by ncc and run on a
source file, must print the byte-identical stream over the whole
examples directory. hello.n is 73 tokens; countdown.n is 124; the
diff is the test. (The later modules need their own dump anchors —
an AST shape for `parse.n` is the next design question, deliberately
left to its own scout.)

**Rung 1 LANDED — [selfhost/lex.n](../selfhost/lex.n), and it
overshot its target**: the plan said hello.n byte-exact; the landing
is the **whole examples directory plus lex.n itself — 23 sources,
every stream byte-identical to `ncc --tokens`**, wired into the
verification suite as a permanent differential stage that fails on
any mismatch. The overshoot has a reason worth recording: a *stream*
lexer (kinds and lines — the dump needs no lexemes) turns out to be
almost all of `next_token`'s actual difficulty — the comment
depth-counting, the keyword table, the escape rules, the
interpolation brace stack with its `T_INTERP_R` resume — while the
part it postpones (capturing identifier bytes, string bodies, integer
values) is bookkeeping that `parse.n` will ask for when it exists.
One honest lesson from the landing: the first run mismatched exactly
one source — nparse.n, at 80% of the file — because the read buffer
was 128K and the file is 150K; the differential caught a *harness*
bug before it could ever have become a lexer lie. See
[selfhost/README.md](../selfhost/README.md) for the contract and the
commands. **And the rung closed where every rung of this project
closes — inside NyxOS**: the in-OS ncc compiled lex.n on target
(through the in-OS TinyCC, like everything else), and the two dumps
matched line for line over serial, hello.n and countdown.n both. The
first self-host module runs on the OS it is for.

**Rung 2 — lexemes — landed the same way, both sides in one step**:
the dump format grew on the C side and the N side together — idents
and `#[drop]` names verbatim, integers as their parsed value (the N
side reproduces ncc's exact accumulation, hex and `_` included —
`0x811C9DC5` must print `2166136261` from both lexers), string
segments as processed byte counts — and the differential stayed the
judge: 23/23 sources, still byte-identical. The stream now carries
everything `parse.n` will need from a lexer, which makes the parser's
scout the next honest step.

### The parser's anchor

The scout settled two questions. **Architecture first**: N has no
modules or imports, so `parse.n` cannot `use` the lexer — and the
honest answer was already established by the toy chain (ntokens →
ncalc → nemit → nstack → nparse, each link carrying its
predecessors' machinery forward): **the module chain ACCRETES**.
`lex.n` stays frozen as the lexer's differential artifact; `parse.n`
will be a new file that *contains* its lexer — the same functions,
stated duplication — and dumps the tree. That is ncc's own one-file
shape, mirrored. (Reading `lex.n`'s dump as input was weighed and
rejected: it would leave the parser never exercising N-lexing, and N
programs take no argv to select modes with.)

**The anchor second — `ncc --ast`, landed**: a POSTORDER dump of the
parsed tree, one line per node, children before parents, so a diff
pins both the shape and the order. The format, held stable from here:

- `E <k> …` expression nodes in EK order (0 int / 1 bool / 2 str /
  3 interp / 4 path / 5 call / 6 field / 7 unary / 8 binary / 9 cast
  / 10 struct-literal / 11 enum-literal / 12 index) — ints and bools
  by value, strings as `#count` (the --tokens rule), paths and fields
  by name, operators verbatim, casts and every other type as
  `ptrs:is_user:name`, interp frag specs as `#tlen` (text) or
  `@fmt.width.zero` (hole);
- `S <k> …` statements in SK order (0 let / 1 assign / 2 return /
  3 expr / 4 while / 5 if / 6 break / 7 continue / 8 defer / 9 match
  / 10 for), match arms as `A <variant> <binds…> <form>` lines;
- `B <n> <has-tail>` closes a block, `nil` marks an absent optional
  slot (walk order stays deterministic), `D`/`V`/`U`/`X`/`F` carry
  struct, enum-variant, enum, extern-fn and fn headers with full
  param/return types, `.` ends the program.

All 23 sources dump cleanly. The anchor stands ready; `parse.n` now
has an exact target to rise to, and its rungs begin next.

**parse.n rung 1 LANDED on the anchor's heels**: ~900 lines of real
N — the accreted lexer wearing a two-token window, plus the whole
expression ladder and the statement floor, printing the postorder
dump AS IT PARSES (recursive descent is a postorder walk; no tree is
stored — that is check.n's rung). **Nine sources byte-identical to
`ncc --ast`**, including lex.n itself, wired into the suite beside
the lexer differential. The gap list is short and structural —
struct/enum/impl declarations, `match`, `for` — and it includes
parse.n's own `struct T`, so the next rung closes a neat circle:
the parser learning to parse itself.

**Both modules are proven inside NyxOS now** — the lexeme-bearing
token stream and the parser's tree, each dumped by the in-OS ncc and
by the N module compiled on target, each pair identical over serial.
The run also delivered the differential's best catch yet: the first
pass mismatched because the C ANCHOR was wrong on target — the dump
printers said `%lld`, and NyxOS's printf speaks `%l[dux]` only, the
project's own rule since the M2 port. The N modules were right both
times. A differential test that can catch its own reference lying is
worth every line it took.

**Rung 2 closed the circle and nearly the ledger**: declarations
(struct with `#[drop]`/`own`, enums with payload variants), `match`
in all four positions (the S 9 anchor grew match-assign's lhs and
`aop`, both sides together), `for`, and `?` propagation — **23 of 24
sources now parse byte-identical, including parse.n ITSELF and the
whole 150K toy compiler.** The dump's category grouping is bought by
FOUR SKIP-PASSES over the file (print one category, brace-count past
the rest) instead of buffered text — N's hardest constraint, string
building, simply never comes up. The last source fell the
established way: the anchor grew `M` lines for ncc's METHODS table,
parse.n grew a fifth pass, and methods.n joined the ledger — **24 of
24, the parser's coverage complete**. And the whole of rung 2 is
proven inside NyxOS: batch V44 matched matchexpr.n's 84-line tree —
declarations, match-expressions, the extended anchor — dumped by the
on-target build, alongside hello.n's and both lexeme streams. The
parser module stands where the lexer stands: byte-exact against ncc
on the host and on the OS it is for. `check.n` is the mountain's
next face.

### check.n's anchor

The scout settled the third module's contract, and it is the
sharpest one yet: **the negative corpus**. ncc's checker rejects
wrong programs with located, worded errors — 142 `die` sites' worth
of judgments — and the verification suite has long carried a battery
of sixty-six bad programs asserting them. That battery is now a repo
artifact: [selfhost/tests/bad/](../selfhost/tests/bad/) holds the 66
sources, and `MANIFEST.txt` records each file's **exact first error
line** — `file:line: message` — generated by running ncc, never by
hand. `check.n`'s contract: reproduce those lines, location and
wording both, for every file it claims. An error message is the one
place a checker shows its whole reasoning in a single line; matching
it in the reference's own words is the strongest check available.

Two design facts the reading fixed. **The tree materializes here**:
`check.n` cannot print-as-it-parses — checking needs the program as
data — so the parser's code accretes once more, this time building
the toy's proven parallel-array AST at real scale (the trick that
carried lex.n and parse.n retires exactly where the toy's own
history said it would). And **the positive anchor can come later**:
ncc's `infer_type` re-infers on demand (types are not stored on the
tree), so a `--types` dump is feasible when check.n wants a positive
contract — the negative corpus is crisper and comes first.

### Rung 1 — the tree materializes

The transform the anchor promised happened, and it was mechanical in
exactly the way the design predicted: parse.n's functions accrete
into check.n with every `put` replaced by a node allocation — the
parser returns indexes now, and recursive descent builds in
postorder the same tree it used to print. The five category passes
collapse back to one (errors have no dump order to buy),
declarations land in tables, and the toy's parallel-array AST
reappears at real scale: a node is eight words, child lists live in
a flat side arena, and the arena pointers ride the same state block
the lexer already threads — the transform never changed a signature.

The checker walk is small because rung 1 claims the name-resolution
family: six manifest rows — undeclared variable, immutable
assignment (plain, through a struct field, and on a for-loop
variable), unknown function, arity — each byte-exact against ncc's
wording, plus hello.n and countdown.n as positive targets whose
required output is silence. The scope table is ncc's VARS
discipline (params immutable, innermost shadows, blocks truncate on
exit); the check order inside a statement is ncc's own — assignment
checks lhs, then rhs, then `mut`, and the order decides which error
a doubly-wrong program reports first, so it is part of the
contract. One normalization, recorded in the corpus README: check.n
cannot know the target's original basename, so it prints
`line: message` and the harness strips the manifest's filename
column. The first error stops the run — ncc dies on its first
error, and a checker held to first-error lines must die the same
way.

One host-side find: the shim that runs N programs on Linux served
sbrk from a 1M static arena, and the checker's node arena was the
first thing to outgrow it (8M now). The differential reported the
overflow as six segfaults before it could pass for an N bug — the
harness keeps ruling on both sides of the fence.

### Proven inside NyxOS — and the road up the manifest

The rung's on-target proof came a batch later, and it is the
prettiest differential the ladder has run yet: the in-OS ncc (itself
compiled by the in-OS tcc) compiled check.n on target, both ncc and
the freshly built checker were pointed at the same wrong program,
and the two first-error lines — the C reference's and the N
rewrite's, both produced inside the operating system — matched to
the byte once the filename column was stripped. The same batch
closed the parser's last on-target gap (methods.n's impl/M dump,
byte-identical), with zero panics and the VFS census on its exact
baseline.

The scout for the next rungs sorted the remaining sixty manifest
rows by what they need. **Rung 2 is the structural family, no types
required**: the capability check (`#[caps(syscall)]` — the caps bit
already rides the fn table), the C-keyword name check, and nested
`defer` — each a small judgment over machinery check.n already has.
**Rung 3 is the mountain's next real face: `infer_type` in N** —
struct and enum tables, parameter and binding types, and the
compatibility lattice — which unlocks the forty-odd rows that speak
about types (argument and assignment mismatches, str and pointer
operator rules, match and result semantics, and the own/move
family). The negative corpus keeps the order honest: every rung
claims exactly the rows its differential proves.

### Rung 2 — the structural family

The scout's three rows landed as predicted — each one a short
judgment over machinery the tree rung already built. The capability
gate found its data waiting (the caps bit has ridden the fn table
since rung 1) and its subtlety in ncc's source: `xfn_caps` searches
the extern table only, so a plain function calling another plain
function never gates — the capability is a property of the syscall
boundary, not of call graphs — and the check runs between existence
and arity, an order the differential can see (a call that is wrong
twice reports the capability first, and check.n's line matches
ncc's). The C-keyword refusal turned out to live in the parser: ncc
fires it in `pexp`, at the declaration's name, before the program is
whole — so check.n refuses there too, and a file whose first
function is named `double` never reaches the checker at all. The
defer depth judgment is the first line of the defer case, before the
deferred expression resolves its names — the manifest's
nested-defer row pins that order. Nine rows byte-exact; the type
family is next, and it is the mountain's face.

### Rung 3 — the type family opens

The face turned out to have a ledge, and the ledge held five rows.
The design questions all had short answers. What is `Ty` in N? The
parser's `struct T` — ptrs, is_user, name-span — was already ncc's
`Ty` member for member, with an empty span playing ncc's NULL name.
Where does a synthesized name point, when an int literal must be
`i64` and there is no `i64` in the source? Into the same buffer: the
six fixed names are interned AFTER the source text, so every span
comparison stays one uniform operation and the lexer, whose length
stops at the source's end, never meets them. Where do types live?
Where ncc keeps them: on the name table (each binding records what
its initializer inferred), on the fn table (full parameter records
and return types), and nowhere else — `cinfer` re-infers bottom-up
on demand, exactly as `infer_type` does, right down to the soft i64
fallback and the rule that arithmetic takes its type from the left
operand. `tcompat` preserves ncc's clause order because the order is
semantics: the `#[user]` boundary is checked before the byte-pointer
wildcards precisely so those wildcards cannot smuggle a pointer
across it.

The five rows: argument types (the plain mismatch and the `#[user]`
one — the differential renders `#[user] *u8` byte-for-byte),
assignment value-vs-target, the element-write type through a
pointer, and the str-index write refusal. One milestone hid in the
climb: check.n crossed sixty-four functions and ncc's own function
table had to double — the checker is now the largest N program ever
compiled, and it grows the compiler's limits from inside. Struct
fields, enums and methods still fall back softly; their tables are
the remaining rungs, and the corpus will call them out row by row.

The rung's coda: the return judgments (CUR_RET set per function,
ncc's three branches in ncc's order, `never` counting as no-type)
made row fifteen — and batch V46 took the new families onto the
machine. The in-OS ncc compiled the 75-function checker (the 64→128
function-table growth, exercised on target by the OS's own
compiler), then reference and rewrite were pointed at one structural
row and one type row each — bad_caps and bad_argty — and all first
error lines matched to the byte inside NyxOS, em-dash and
`expected str, got i64` included. Zero panics; the census on its
exact baseline. Fifteen rows, three families, both worlds.

### Rung 4 — the struct table

The decl-table rung the docs promised, and the first time accreted
code came BACK: pstruct — dropped to a brace-skip when the five
passes collapsed — returns as a real parse, transformed like every
other parser function, its fields landing in a table instead of a
dump line. The struct literal's node grew names beside its values
(the checker needs to know WHICH field each value initializes), and
with the table in place three of ncc's judgments transliterate in
ncc's exact order: unknown struct, every-declared-field-exactly-once
(checked per declared field, so the missing-field wording names the
field that is missing), and the extra-name refusal — then each
value checks against its declared field's type. Field reads got
ncc's three arms whole, and `cinfer`'s struct case replaced its soft
fallback with the real lookup — which means a struct field's type
now flows into every judgment downstream of it: the probe hands a
str field to an i64 parameter and gets ncc's argument-mismatch line,
byte for byte, because the table, the inference and the argument
check are all telling each other the truth. Three more rows and a
third positive target (structs.n, silent); eighteen rows and the
enum table left standing.

### Rung 5 — the enum table, and match comes back

The last standing family fell in one climb. penum accreted back the
way pstruct had, and with the enum table in place `match` — refused
since print-as-you-parse retired — returned to the parser in all
four of its positions, building nodes instead of S 9 lines. The
checker is ncc's S_MATCH block whole, in order: enum subject,
exhaustive-and-unduplicated cover (the wording names the variant
that is missing), stray-arm refusal, binds counted against each
variant's payload — and then the part that makes match an
expression: every arm checks with its binds seeded in scope, typed
positionally from the payload fields, the first arm fixes the
result type and the rest must agree with it, and the `:=`, `=` and
`return` targets replay their own statement's checks against that
result. Variant references judge before their enum name could
misresolve as a variable, and payload-carrying variants point the
user at the constructor — braces escaped in N's own strings,
because braces interpolate.

Seven rows landed at once (one of them free: the return-mismatch
check written two rungs early found its corpus row waiting), and
the positive ledger tripled — eleven examples now check in
enforced silence, enums.n and matchexpr.n among them. Twenty-five
rows; what remains of the corpus speaks own, try, drop, methods and
the pageflags dialect — each a bounded rung on a mountain whose
shape is now familiar.

### Rung 6 — `?` propagation, and the fifth family on target

Batch V47 first: the struct and enum/match families joined the
on-target ledger — four two-sided differentials inside NyxOS now,
the in-OS ncc and the in-OS-compiled checker agreeing on caps,
argument types, struct-literal fields and match cover, byte for
byte, with the census steady on its exact baseline.

Then the try rung, the scout's smallest-machinery pick, and it paid
exactly as predicted. The parser had been consuming `?` and
dropping it; now the flag rides the nodes, and a try statement
routes whole through gen_try's judgments in gen_try's order —
result-enum operand (Ok and Err exactly, transliterated), a
result-enum return to propagate into, Err-payload agreement across
DIFFERENT result enums (the subtlest wording in the family:
`cannot propagate R.Err (str) as S.Err (i64)`), and the
binding forms' demand for an Ok payload — which then types the
binding: the unwrap is what makes `x := e?;` see the payload's
type, not the enum's. Four rows, and the twelfth silence: results.n,
the example the ledger had to refuse honestly one rung ago, checks
clean now that the machinery it exercises exists. Twenty-nine rows;
own, drop, methods, pageflags and the format specs remain.

### Rung 7 — methods

The last brace-skipped item came back: pimpl accretes into a method
table — (impl type, name) keyed, `self` excluded from the params
the way ncc's own Method records exclude it — and the E_CALL
method arm completes. Dispatch keys on the RECEIVER'S TYPE: the
receiver resolves, its inferred type finds the method or the
refusal renders that type through ty_str (`Rect has no method
'grow'` — the type bare, no quotes, ncc's exact shape), then arity
in the method wording, then per-argument types. Two mirrored
subtleties matter more than the two rows they landed: method BODIES
check before function bodies, because that is gen_program's order
and first-error contracts inherit their reference's order; and
`self` enters the name table first, an immutable binding of the
impl type whose name lives in the intern tail beside the
synthesized type names — the same trick, stretched one word
further. methods.n, the dispatch machinery's own example, is the
thirteenth silence; thirty-one rows, and the corpus's remainder is
own/move, drop, pageflags, the format specs and the no-return
analysis.

### Rung 8 — the format specs, and the great silence sweep

Batch V48 put the try and method families on target first — six
two-sided differentials inside NyxOS, every line byte-identical.
Then the parser stopped dropping `:spec`: the grammar validates at
parse time in ncc's exact walk, the teaching message prints
verbatim (all three refusal shapes — an unknown letter, a width
digit missing, trailing garbage — share it), and at check time a
spec'd hole must be an integer. Five rows — and then the sweep.
Seven more examples check in silence, and so does nparse.n: the
150K toy compiler, fifteen thousand nodes of it, through the
checker's arenas without a squeak, along with lex.n itself.
Twenty-one of twenty-two examples are enforced silences;
pageflags.n alone waits for its constants. Thirty-six rows. What
remains: own/move, drop, pageflags, and the no-return analysis —
the corpus's last four dialects.

### Rung 9 — pageflags, and the ledger closes

W^X is the language's proudest check — a security property proven
totally at compile time, because pageflags values can only be built
from four predeclared constants and `|`, so the compiler always
knows every value's exact bits. The transliteration kept the whole
argument: the constants predeclare before name resolution, every
binding carries its statically-known mask (a parameter's is opaque
— its flags were proven at its call sites), `|` compositions fold,
anything else refuses, and a mask that holds both WRITE and EXEC is
refused with the row the corpus has waited nine rungs to claim.
The operator rules rode along in ncc's arm order — pageflags, str,
pointers, integers — collecting the no-arithmetic-on-strings and
pointers-compare-only rows on the way.

And with that, pageflags.n checks in silence, and the ledger
closes: **every example the N language ships — all twenty-two —
now passes through its own self-hosted checker with zero output
enforced by the suite.** Forty-two manifest rows to the byte. The
corpus's remainder is three dialects deep in the semantics: own and
move, drop, and the not-every-path-returns analysis.

### The own/move ladder — the scout's verdict

Batch V49 first: nine families on target, and the eighth fence is
the one to frame — the W^X refusal verified differentially inside
the operating system whose kernel enforces W^X. The reference and
the rewrite, both compiled and run in NyxOS, agreeing on a memory-
safety violation byte for byte.

Then the scout read v0.17–18 whole, and the deepest family
resolved into a three-rung ladder. **Rung 10a, states and consume
points**: the struct table records `own` and its drop function,
bindings carry an ownership state (NONE, LIVE, HELD for parameters,
MOVED), and `own_move_expr` transliterated — a bare path naming an
own binding, consumed as a value at a call argument, a binding, an
assignment, a return or a tail, transfers ownership there; moved
values refuse further use; and the v0.18 gates refuse moves where a
statement may run zero or many times (loop bodies AND while
conditions) or inside match arms. That rung alone claims six rows.
**Rung 10b, the flow analysis**: LIVE values must be consumed —
the leak scan at returns and function ends — and `if`/`else` arms
are checked from the same pre-branch states and must AGREE at the
merge point, with an arm that returns exempted (its own scan
already policed that path). **Rung 10c, the declarations**: the
`#[drop]` validations and the own-cannot-nest rule, whose messages
are FILE-level — no line number — which will need one more
normalization design in the harness. The ladder's shape is
familiar by now; the semantics are the deepest yet.

### Rung 10a — own states, and the branch merge that came early

The mechanical half went exactly as scouted: VARS grew from eight
words a record to ten (slot 8 is the ownership state), the struct
table from four to eight (the own flag, and the `#[drop]` span
parked for the declarations rung), and every reader was swept in
one asserted pass. The semantic half is ncc's `own_move_expr` as
`cmove`: a bare path naming an own binding, consumed as a value at
a call argument, a `let` init, an assignment, a `return` or a
block tail, transfers ownership — MOVED values refuse further use,
`own` bindings refuse `mut`, an own result nobody binds refuses
the discard, and the v0.18 gates refuse moves under LOOP_DEPTH
(while bodies AND while conditions — the condition re-evaluates)
or MATCH_DEPTH. One parser fidelity find rode along: ncc stamps an
expression statement AFTER its semicolon, so `make();` discarded
on line 4 reports line 5 — the rewrite now stamps the same token.

The scout drew rung 10a at states and consume points, six rows.
own.n redrew it. The language's own showcase example uses v0.18
branch-aware consumption throughout — both arms of an `if` moving
the value, a returning arm consuming and exiting — and the rung's
first probe run refused it. Keeping own.n silent (the standing
bar: every example an enforced silence) meant pulling the S_IF
machinery forward from the flow rung: snapshot the pre-if states,
check both arms from the same start, restore between them, and
reconcile at the merge — a returning arm never reaches it, and
two live exits must agree on every binding. With the machinery in,
the agreement judgment is three more rows for free (`bad_own_branch`,
its else-less twin, and the `#[drop]` variant). Nine rows, then:
fifty-one of the corpus byte-exact, twenty-two silences held, and
what remains of own/move is exactly the leak scan — rung 10b is
now just that — before the declarations and their FILE-level
normalization question.

### Rung 10b — the leak scan closes the ledger

Ownership had one open side left: a value could be born and simply
never go anywhere. ncc closes it with two sweeps that always run in
the same order — the v0.19 auto-drops first (every LIVE binding
whose type carries a `#[drop]` destructor drops, last born first,
and becomes MOVED), then the leak scan (the first binding still
LIVE is refused). The rewrite places them at ncc's exact three
sites: both return forms drain and scan the whole frame at the
return's line ("at this return"), and every block close drains and
scans its own births — from the scope's saved base — wording the
refusal by block depth ("the end of the function" vs "the end of
this block") and anchoring it at the last statement's line, zero
for an empty block, which is ncc's own quirk kept faithfully.

The drop flip is what makes the scan honest: own.n leans on v0.19
throughout — `use_page` never consumes its page and expects the
destructor to fire at the function's end, and `touch_pages` births
one inside a loop body where moves are refused, relying on the
body-close drop. Both stay silences. With `bad_own_leak` claimed,
fifty-two rows hold and the own/move family is done but for its
declarations: rung 10c is `#[drop]` validation and the own-nest
rule, whose FILE-level messages carry the harness's next
normalization design.

### Batch V50 — ownership proven on target

The tenth family boarded the in-OS ledger. Batch V50 carried
`bad_own_useafter` and `bad_own_leak` into NyxOS and ran the
two-sided fence on each: the in-OS ncc names the violation, the
in-OS-compiled checker names it independently, and the serial log
shows the two lines byte-identical. There is something fitting
about this pair in particular — the ownership analysis exists so
that OS resources (a file handle, a page) provably go somewhere,
and here it is running inside the OS it guards, twice over, in
agreement with itself. Ten differentials now stand: capabilities,
argument types, struct fields, match coverage, result enums,
method dispatch, format specs, W^X, use-after-move, and the leak.
The remaining corpus — the `#[drop]` declarations, own-nest, and
the not-every-path-returns family — is FILE-level territory: the
next rung designs how a message with no line number rides the
same byte-exact harness.

### Rung 10c — the declarations, and the two-shape contract

The design question resolved itself with pleasing economy. A
FILE-level manifest row reads `file.n: message`; the harness strips
the filename column with one `cut`, which leaves ` message` — a
leading space. So check.n simply prints that: its FILE-level
refusals are a leading space, the message, no line. One cut, two
shapes, zero special cases — the contract grew a second form
without the harness learning anything new.

The machinery under it is ncc's declaration battery in ncc's exact
order. The item loop was reshaped to the reference's sequence —
caps, fn/extern, the caps-on-anything-else refusal, then `#[drop]`
demanding `own` — which claimed two parse-time rows on the way,
one of them (`bad_caps_item`) a straggler from outside the own
family entirely. Then a `cdecls` pass runs where ncc's main runs
it, between parsing and the body checks: `validate_drops`
(destructor exists — the lexer had already isolated the bare name
inside `#[drop(...)]`, so the check was a table lookup — takes
exactly one value of the struct's type, returns nothing), then the
containment walks: no own in struct fields, none in enum variants,
none across the syscall boundary, no impl on an own receiver. Six
rows, fifty-eight held, and the own dialect — the deepest thing
the corpus speaks — is complete: seventeen rows across four rungs,
with own.n silent above them the whole way. What remains of the
manifest is one family: not-every-path-returns, FILE-level itself,
riding the contract this rung just built.

### The stragglers — the last line-level rows

Five rows never belonged to a family: indexing a value with no
elements, indexing by a non-integer, a counted `for` over
non-integer bounds, `#[user]` on a non-pointer, and `#[user]`
beside `raw`. Each transliterated from its own site — the two
index judgments into the E_INDEX arm in ncc's base-then-index
order, the bounds rule into the for statement before its loop
variable seeds, and the two pointer-attribute refusals into the
type parser itself, stamped at the type's first token exactly as
ncc's parse_type stamps them. The rung's one discovery was an
honest kind of gap: the index node had existed for eight versions
without ever printing an error, and its line slot had quietly held
zero the whole time — the first refusal to speak from it said
`0:` and the differential caught it within the minute. Sixty-three
rows byte-exact, twenty-two silences, and the corpus now divides
cleanly in two: every line-bearing error held, and one FILE-level
family — not-every-path-returns — left to close it.

### The corpus completes — 66 of 66

The missing-return analysis went last because it is the only check
that reasons about ABSENCE: not "this expression is wrong" but "no
path through this function produces the value it promised".
`block_guarantees` transliterated cleanly — tail, return, both-arm
ifs, exhaustive matches, `never` calls, and the deliberate refusal
to special-case `while true` — and its three rows landed in the
FILE-level shape on the first probe run, with all twenty-two
examples still silent above it (the real stress: every function
the language ships had to genuinely guarantee its return).

So the anchor holds the whole ledger now: sixty-six wrong programs,
each refused with ncc's exact first error — location and wording,
line-bearing and FILE-level alike — and twenty-two right ones held
to enforced silence, by a checker written in the language it
checks, compiled by the compiler it mirrors, verified inside the
operating system both belong to. Batch V51 carried the new shapes
to the target the same hour: twelve differentials, the FILE-level
fence's debut among them, byte-identical through the serial port.
The checking module of the self-host ladder has nothing left to
climb in this corpus; what stands above it is generation.

### What stands above — the gen.n verdict

The judgment, weighed with the corpus behind us. **GO.** gen.n —
ncc's code generator rewritten in N — is the fourth and final
module: lex, parse, check, gen, and the self-host ladder has no
fifth word. Three things decide it.

First, the differential mode is the strongest the ladder has ever
had, and it needs NO normalization at all. The harness pattern
stays what it has always been — copy the source to a fixed path,
run both compilers over the same file — but the comparison drops
from "the same first error, filename column stripped" to `diff`:
ncc's generated C against gen.n's generated C, byte for byte,
comment header included (both read the same fixed path, so even
the provenance line matches). A 24-line hello.c already shows the
whole discipline in miniature: the syscall wrappers, the forward
prototypes, the interpolation lowering with its numbered `__b0`
and `__s0` temporaries, the escaped strings. Deterministic output,
deterministically compared.

Second, the accretion pattern is proven three times over — parse.n
grew out of lex.n, check.n out of parse.n, and gen.n grows out of
check.n the same way: the checker stays (generation assumes checked
input, exactly as ncc's single pass does), and emitters join it.

Third, the honest risks are known and priced: gen.n will be the
ladder's largest file; ncc's function-count cap (128, with check.n
at 105) will be crossed early, so the cap bump and the package-copy
refresh are planned work, not a surprise; and byte-equality is
merciless about ncc's emission order — the prototype walk, the
temp-counter numbering, the indentation — which is not a risk so
much as the point: the reference's every habit becomes a held
invariant.

The rungs, sketched: **rung 1**, hello.n byte-exact — header,
extern wrappers, prototypes, one function with a binding, an
interpolation and a call. **Rung 2**, the expression ladder and
statements — countdown and inference join. **Then** one rung per
construct family, retracing the corpus in emission order: structs,
enums and match, defer and the `__ret` form, `?`, counted for,
indexing, own drops — each rung measured the same way, more
examples' generated C held to the byte. **The summit above them
all**: gen.n emitting lex.n, parse.n, check.n and gen.n itself —
the N compiler, written in N, compiling itself inside NyxOS. That
is milestone M5's original sentence, and for the first time every
word of it is load-bearing.

### gen.n rung 1 — first C, byte for byte

The fourth module opened the way the verdict predicted, and faster.
gen.n is check.n accreted whole plus the emitters, and its first
probe run held hello.n's generated C byte-identical to ncc's — the
syscall wrappers, the prototypes, the interpolation lowering with
its numbered temporaries, the escaped strings, the provenance
comment. inference.n rode along free; countdown.n needed one more
statement arm (`if`/`else`) and then matched too. Three programs,
three `cmp`s, zero bytes of difference.

The accretion cost three honest changes. The lexer had thrown raw
string bodies away once it counted them — the emitters need to
re-walk them, so string tokens now carry the body's start beside
the count, and a decoder-reencoder translates N's escape spellings
to C's (they are the same spellings, which is not a coincidence:
ncc chose them). The interpolation parser had kept only the holes —
the emitters need the text between them, so fragments became
4-word records, text and holes alike. And the extern parser had
discarded the syscall numbers the wrappers embed. None of these
touched check.n: the third module is complete and frozen; the
fourth carries its own copy forward, the way parse.n carried
lex.n's.

What makes this rung different from every rung before it is the
comparison. The checker was held to ncc's first-error lines — a
contract about wording. The generator is held to ncc's OUTPUT — a
contract about everything: every space, every cast, every numbered
temporary, the order of every prototype. Suite stage [8d] runs
`cmp`, not `diff` with a normalization; there is nothing to
normalize. The remaining distance to the summit is enumerable:
else-if chains, struct and enum layouts, methods, defer's `__ret`
form, match's switch lowering, try, own drops — the same corpus of
constructs the checker climbed, now climbed again on the emission
side, with the four self-host sources themselves waiting at the
top as the final byte-exact targets.

### gen.n rung 2 — fourteen of twenty-two

The scout ran all twenty-two examples through the `cmp` harness and
bucketed the diffs; the widest bucket by far was the type layouts
— twelve programs whose first divergence was a missing typedef —
so the rung took layouts, the literal forms, and the counted `for`,
and collected the small change the diffs pointed at along the way:
`break` and `continue` had no arms, the `else if` chain needed
ncc's synthesized block braces, the format specs needed their
parsed width/zero/hex values carried through the fragment records
to reach `__nyx_fmt_num` and `__nyx_fmt_hex`, and the unary `!`
had simply never been rendered — the operator table was built for
binary diagnostics, where `!` cannot appear, and the byte-diff
found the gap inside a minute.

Fourteen programs now emit byte-identical C, and one of them is
nparse.n — the 150K-character toy compiler, the largest N program
in the tree, its thousands of expressions, statements and
interpolations all numbered and spaced exactly as ncc numbers and
spaces them. What remains is the heavy lowerings, one rung each:
match's switch, defer's braced `__ret`, try's propagation form,
method bodies, and the own auto-drops — eight examples between
here and a generator that holds the whole directory.

### Batch V52 — the generator emits inside the OS

The fence went through three designs, and the OS taught something
each time. Design one redirected `ngen`'s output to a file and
hashed both sides with the in-OS `sha256sum` — two sums, one
fence, a three-character verdict. The run returned one hash and
one `cannot open`: the shell's `>` captures BUILTIN output but not
an exec'd user binary's — the generator's C had gone straight to
the console. That is a real kernel-shell gap, reported upstream
(issue #88), and it pointed at design two: fence the program
itself and `cat` the reference's file beside it. The `cat` side
then came back truncated mid-line — a burst-sized write loses
bytes on the serial console where `ngen`'s line-paced output
survives intact. So the final fence keeps what the OS delivers
reliably — the generator's own output, line by line over serial —
and diffs it against the reference compiler run by the harness,
with exactly one declared normalization: the provenance comment's
path is rewritten on the reference side, because the host cannot
create a file at the OS's `/mnt` root.

The verdict: hello.n's 24 lines and structs.n's 40, emitted by a
generator compiled inside NyxOS minutes earlier by the compiler it
mirrors, identical to that compiler's output for the same source.
The generator now emits inside the OS it was written for — and
the batch that proved it also filed a kernel bug on the way.
(The main line fixed it within hours: `>` and `>>` redirect an
exec'd binary's stdout as of v6.5.29.)

### gen.n rung 3 — the match switch, and methods with it

The heaviest lowering the language owns went down in one climb.
A match statement becomes ncc's exact shape: the subject evaluated
once into `__mN`, a switch on its tag with the case numbers in
declared-variant order regardless of arm order, and each arm's
binds materialized as typed locals read from the payload fields.
The expression forms add the second temp: `__mresN`, declared with
a deliberately dead zero-store (the switch is exhaustive — C's
flow analysis just can't know it), assigned in every arm, and
consumed only after the switch closes, which is precisely why an
arm bind may shadow the very name being bound. `return match`
computes the value first and returns it after the (still
unregistered) defers — the three match examples carry none, so
the emitted shape is ncc's to the byte.

Methods rode the same hour: prototypes after the function
prototypes, definitions before the function definitions, `self`
seeded first into the emitter's own name table — the interned
span trick from the checker serving emission now — and every
`recv.m(a)` call site rewritten to `Type_m(recv, a)` on the
receiver's inferred type. Eighteen of twenty-two byte-exact; what
remains is defer's braced `__ret`, try's propagation form, and
the own auto-drops — four examples, three lowerings, and then
the generator holds the directory.

### gen.n rung 4 — defer and `?`, and batch V53 between them

Batch V53 went first: matchexpr.n — 111 lines, the complete match
lowering with both temps — emitted inside NyxOS by the in-OS-built
generator, line-identical to the in-OS reference beside it. Three
generator fences on target now, and the census steady.

Then the two lowerings that share an exit discipline landed as one
rung. The defer registry is ncc's: sixteen slots, per-function
reset, LIFO at every exit — and the subtle part is WHERE the
deferred expression's preludes run: at the exit, not the
registration, so a defer carrying an interpolation numbers its
buffer at every return it is copied to, interleaving with the
surrounding temps exactly as the reference interleaves. The braced
forms follow from one sentence of semantics — the return value is
computed before the defers run — which becomes `{ TYPE __ret = v;
defers...; return __ret; }` at a valued return, the same shape
inside the block's own braces at a returning tail, and a bare
defer drain at a void fall-off.

`?` reuses all of it. The operand evaluates once into `__tN`; if
the tag says Err, the defers drain and the function returns — the
value itself when the operand and return enums are the same type,
or an `__eN` rewrap that copies the Err payload across field names
when they differ — and if the tag says Ok, the payload binds,
assigns, or is discarded, per form. The Ok and Err variant numbers
come off the interned spans the checker has carried since its own
try rung: the accretion pattern paying for itself one module
later. defer.n, results.n, fsio.n — all byte-exact, first probe
run each. Twenty-one of twenty-two; own.n alone remains, and it
asks for something new: the ownership state machine, replayed on
the emission walk, so the auto-drop calls land where the checker
proved they belong.

### gen.n rung 5 — the generator holds the directory

The replay taught its lesson the honest way. The first attempt
reused the checker's own mover at the emission sites — and the
checker's mover PRINTS when it sees an illegal move. The emit walk
diverged from the checked states in exactly one place (the
`if`/`else` arms were not being restarted from the pre-branch
states), the mover mistook the divergence for a use-after-move,
printed its diagnostic INTO the generated C, and — the deeper
wound — set the error flag, which silently froze every state
transition after it: a later binding stayed LIVE and collected a
destructor call ncc never emits. Two programs diverged; the
byte-diff pointed at both within a minute.

The fix is a clean split of roles. The emit phase gets a silent
mover — state transfer only, no judgment, because judgment already
happened — and the full S_IF discipline from the checker: snapshot
the pre-branch states, restore between the arms, merge with the
returning-arm exemption. With the states faithful, the drops fall
out: `gdrops` walks the frame in reverse birth order emitting
`destructor(binding);` wherever a LIVE droppable stands — after
the defers at a return, at the scope close otherwise — and
`own_drops_pending` joins the defers in deciding when a return
needs its braced `__ret` form.

All twenty-two examples now emit byte-identical C. The module
crossed ncc's function-count ceiling on the way (128 → 256 — the
checker forced 64 → 128; each module outgrows the reference's
capacities in turn, which is its own kind of progress report).
What stands above is the summit ladder: gen.n emitting the four
selfhost sources themselves — lex.n, parse.n, check.n, and its
own — first on the host, then inside NyxOS: the self-hosting
sentence with every word load-bearing.

And the hardest lexer feature landed: **interpolation**. A toy string
containing a brace lexes as segments — HEAD before the first hole,
MID between holes, TAIL after the last (ncc's own
`T_STR_HEAD/MID/TAIL` shape) — with the lexer switching to expression
mode inside each hole; one mode flag stands in for ncc's mode stack
(single level: a string literal inside a hole is refused with a
located error). The parser desugars as it parses: segments and holes
join into a plain `+` chain, each hole wrapped in an ITOS node, and
the *existing* type machinery compiles it — the checker resolves
`str + str` to CONCAT, an int hole keeps its ITOS (opcode 28: pop the
integer, append its decimal image through the shared cursor, push the
entry), and a str hole's ITOS rewrites to its child in place — a
string inserts as itself, N's format-by-inferred-type rule decided by
the same pass that picks PRINT vs PRINTS. The fold pass closes the
story: ITOS of a constant writes its digits at compile time, the
CONCATs above collapse, and `print "{1 + 2} and {40 + 2}";` compiles
to a single PUSH of one ready-made literal — 6 code words, no
CONCAT, no ITOS. The sugar compiles through the sugar-free tree.

The file-driven bar rose with it: the disk program the toy compiles
(now a checked-in source, [examples/n_toy_demo.toy](../examples/n_toy_demo.toy))
uses the whole surface at once — a function, a `while` sum, a bound
string, and an interpolated print whose holes hold a variable and a
call — and it composed with **zero new machinery**: nothing in the
pipeline knows the bytes came from a file, which was the point of the
one-lexer design all along.

The number spellings and comment forms then caught up with N's own:
`0x` hexadecimal literals (lowercase at toy scale) and `_` digit
separators lex in the digit arm and fold and interpolate like any
constant, and `/* block comments nest */` by a depth counter — with
newlines inside them still ticking the line counter, so a diagnostic
three lines below a two-line comment still names the right line.

And diagnostics now carry the **column**: every token records
`line*1000 + col` packed into the one `ln` array, so the token
machinery and every AST node's line slot carry both for free, and
`fail()` is the single decode point — twenty-seven callers unchanged
(toy lines stay under 1000 columns; ncc carries two fields, same
idea). `line 2:9: error: cannot use a string...` points at the `+`
itself, and the column survives interpolation holes and multi-line
nested comments.

And with attributes — `#[...]` lexed and *deliberately ignored*
(surface parity: the toy reads N's syntax; the meanings of `#[user]`,
`#[caps]`, `#[drop]` belong to ncc's semantic world, and pretending
otherwise would be dishonest) — **the lexer parity table is complete**:
every row is covered, each at its stated scale. One deliberate
refusal stands documented on the checker side too: `==` over two enum
tags stays an error, because ncc's own enums have no `==` either —
`match` is the eliminator in both worlds.

## The struct arc — rung 1 landed

With the lexer effectively at parity, the big remaining ✗ was the
parser/checker row: structs, enums, match. The scouted design held,
and **rung 1 is in**: `struct P { x, y }` declarations lead the
program (tables, not code — the name and its field symbols, up to
four); a literal `P{3, 4}` allocates a flat-i64-word record in a side
RECORD store through a shared cursor — exactly the string-table move —
and the value on the stack IS the record's base index; `p.x` compiles
to one RGET at base+slot. The real design step was the type column
growing **type ids**: `nt` is now 0 int / 1 str / 2+k for struct k,
so the checker resolves each field name against the *base's* struct
(the tree records the resolved slot — the STREQ pattern — and emit
stays type-blind), enforces literal arity and integer field values,
and refuses a record index everywhere a number is expected:
arithmetic, print, interpolation holes, call arguments — the machine
cannot tell them apart, which is precisely why the checker must.
Two honest notes: `name {` opens a literal only when the name is a
*declared struct*, so `if ok { ... }` keeps meaning what it meant
(ncc refuses cond-position literals syntactically; the toy
disambiguates by declaration — a documented divergence), and there is
**no struct folding** — records exist at run time only, the VM being
the one allocator. Landing the rung also forced the identifier
alphabet honest: `is_letter` now reads `A-Z` and `_` like N's own
(struct names are capitalized by convention, and the toy could not
even lex one). The remaining ladder:

2. **Field writes** — ✅ **landed**: `p.x = e;` is a statement (name
   `.` name `=` — all four tokens checked at the statement gate, so a
   tail that merely *reads* a field is never taken for a write); the
   base must be a bound name holding a struct, the value an integer,
   the check pass resolves the field to its slot exactly as reads do,
   and one RSET opcode stores the word in place — the demo mutates
   `p.x` from 3 to 8 and the read after it sees the new value.
3. **Declared parameter types** — ✅ **landed**: `fn bump(q: P)` is
   N's own annotation syntax at toy scale (unannotated = integer; the
   per-function type table rides in the struct bundle, which already
   reached parser and checker — zero new plumbing). Call sites are
   held to the declaration — wrong struct, int-into-struct, and
   struct-into-plain each refused with located errors — and the body
   sees its parameter AS the struct, so field reads and writes on
   parameters just work. What crosses the call is the record's
   *index*: reference semantics, stated plainly — the callee's
   mutation is visible in the caller (the demo prints 4 twice: the
   returned `q.y`, then the caller's own `p.x` after `bump` bumped
   it). Returns stay integer-only for now.
4. **Enums + `match`** — ✅ **landed, tag-only**: an enum declares
   like a struct (a variant IS its index), `Shape.square` resolves at
   parse to one PUSH — typed `100+e`, so a tag never adds, prints,
   interpolates, or slips into a plain parameter: `match` is the only
   eliminator. Arms compile to DUP/EQ/JZ chains over the scrutinee and
   the **last arm runs untested — the checker proved exhaustiveness
   first** (every variant exactly once; unknown variants, duplicates,
   and gaps are located errors). Tags cross calls via `fn pick(s:
   Shape)` annotations, and a callee can match on its parameter.
   **And payload variants landed**: `num(x)` flags a variant (payload =
   one integer word); an enum with any payload variant represents ALL
   its values as records — tag at word 0, payload after — built by
   the same RNEW that builds structs, ncc's tag+union layout in
   miniature (tag-only enums keep bare integers; the representation is
   a per-enum choice). A match arm binds with `num(x) { ... }` — the
   payload is stored into `x` for that arm's chain only, dying at the
   brace — and construction arity is checked both ways ("the variant
   takes a payload" / "takes no payload"). The enum row is COMPLETE at
   toy scale.

What does NOT carry over from ncc: field offsets in bytes (toy records
are flat i64 words), methods, and ownership — those stay ncc-side.
After the arc, the toy's ✗ column is enums/match and the attribute
row — and "parses the examples directory" stops being a slogan.

## Definition of done for M5

`ncc` rewritten in N means: an N program that reads `.n` source, produces
the same C the C-hosted `ncc` produces for the supported subset, compiles
with the in-OS toolchain, and rebuilds itself inside NyxOS. The toy chain
is the training ground; every rung above retires one difference between
the training ground and the real thing.
