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
