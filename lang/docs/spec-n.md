# The N Language — Specification

**Version:** v0.3 (bootstrap) · **Implementation:** [`lang/ncc/ncc.c`](../ncc/ncc.c) · **Target:** NyxOS x86_64

This document specifies N exactly as implemented by the bootstrap compiler
`ncc`. It is a *descriptive* spec: everything here compiles today. Planned
features live in the [N++ design document](design-npp.md), not here — if you
read it in this file, you can use it.

---

## 1. Overview

N is a small systems language that transpiles to freestanding C. Its design
goals, in priority order:

1. **Direct kernel access.** NyxOS syscalls are first-class: declared once with
   their number, called like functions, compiled to the raw x86_64 `syscall`
   instruction with zero overhead and zero libc.
2. **Readable surface.** Type inference with `:=`, string interpolation,
   blocks-as-values — while keeping C-level predictability of what the
   generated code does.
3. **No hidden runtime.** The entire runtime is ~60 lines of freestanding C
   (`user/nyxrt.c`): a syscall primitive and three string-format helpers.
   There is no allocator, no GC, no startup machinery beyond the standard
   NyxOS `crt0`.

An N source file is a sequence of top-level *items*: `extern syscall` blocks
and function definitions. Execution starts at `fn main`.

## 2. Lexical structure

### 2.1 Source encoding

Source files are byte-oriented ASCII (UTF-8 passes through string literals
uninterpreted). The canonical file extension is `.n`.

### 2.2 Comments

```n
// line comment, runs to end of line
/* block comment /* nests */ properly */
```

### 2.3 Identifiers and keywords

Identifiers match `[A-Za-z_][A-Za-z0-9_]*`. The following are reserved:

```
as break continue else extern false fn if mut raw return
syscall true while
```

### 2.4 Integer literals

| Form | Example | Notes |
|---|---|---|
| Decimal | `1024`, `1_000_000` | `_` separators allowed, ignored |
| Hexadecimal | `0xFFFF_F000` | prefix `0x` |

Integer literals are 64-bit values; their concrete C type is currently chosen
by C's usual rules at the use site (see §9, Limitations).

### 2.5 String literals and interpolation

String literals are double-quoted with these escapes:
`\n \t \r \0 \\ \" \' \{ \}`.

A `{expression}` inside a string literal is **interpolation**: the expression
is evaluated and formatted into the string at that position.

```n
msg := "pid={getpid()} answer={40 + 2}\n";
```

Semantics: each interpolated expression is evaluated once, in order of
appearance, and formatted **by its inferred type** (§6.4): a `str` value is
inserted verbatim as text; every other type is converted as a signed 64-bit
integer and formatted in decimal. The result is a `str` built in a 256-byte
function-scope buffer (§7.3); text beyond the buffer capacity is truncated,
never overflowed. Use `\{` and `\}` for literal braces.

### 2.6 Operators and punctuation

```
( ) { } , ; : . ->
:= = += -=
+ - * / %
== != < <= > >=
! && ||
& | ^ << >>
as ?     (reserved: ? currently lexed but unused)
```

## 3. Types

### 3.1 Primitives

| N type | Meaning | C lowering |
|---|---|---|
| `i8 i16 i32 i64` | signed integers | `nyx_i8` … `nyx_i64` |
| `u8 u16 u32 u64` | unsigned integers | `nyx_u8` … `nyx_u64` |
| `isize usize` | pointer-width (64-bit) integers | `nyx_isize` / `nyx_usize` |
| `addr` | a virtual address (64-bit) | `nyx_addr` |
| `bool` | `true` / `false` | `nyx_bool` |
| `str` | string view: pointer + length, non-owning | `nyx_str` `{ const char* ptr; u64 len; }` |
| `void` | no value (also written by omitting `-> T`) | `void` |
| `never` | function does not return (e.g. `exit`) | `void` + unreachable loop |

`str` values expose two fields: `.ptr` (`*u8`-compatible pointer to the bytes)
and `.len` (byte count). Strings are not NUL-dependent; the length is
authoritative (the runtime does keep buffers NUL-terminated as a convenience).

### 3.2 Pointers

```n
*T        // pointer to T
raw *T    // same representation; documents intent to do unchecked arithmetic
```

Both lower to `T*` in C. In N v0.1 they are equivalent; the distinction exists
so N++ can attach checking to `*T` while leaving `raw *T` unchecked (see the
N++ design). Any type name not in §3.1 passes through to C unchanged, which is
the current FFI escape hatch.

## 4. Items

### 4.1 `extern syscall` — kernel bindings

The signature feature of N. Binds NyxOS syscalls by number:

```n
extern syscall {
    fn write(fd: i32, buf: *u8, len: isize) -> i64 = 1
    fn getpid() -> i64                             = 6
    fn exit(status: i32) -> never                  = 0
}
```

Each entry declares a typed function whose body *is* the syscall. The compiler
generates a `static inline` C wrapper that places the number in RAX and the
arguments in RDI, RSI, RDX, R10, R8, R9 (the NyxOS syscall ABI, identical in
register convention to the standard x86_64 one), executes `syscall`, and
returns RAX cast to the declared return type. Up to 6 arguments; missing
arguments are passed as 0.

- A `-> never` syscall is emitted as `void` followed by an unreachable loop —
  the kernel does not return from it.
- The trailing `= N` number is **required**: it is the single source of truth
  connecting the name to the kernel's dispatch table (`kernel/syscall.c`).
- Trailing `;` after an entry is optional.

The authoritative number table lives in [`user/syscall.h`](../../user/syscall.h);
`extern syscall` blocks must agree with it.

### 4.2 Function definitions

```n
fn put(s: str) {                 // no return type → void
    write(1, s.ptr as *u8, s.len as isize);
}

fn add(a: i64, b: i64) -> i64 {
    a + b                        // block tail value (§5.6)
}
```

Parameters are `name: type`, comma-separated. The return type follows `->` and
may be omitted for `void`. `fn main() -> i64` is the program entry point; its
return value becomes the process exit status (via `crt0` → `SYS_EXIT`).

## 5. Statements

Statements are separated by `;`. Blocks are `{ ... }` and may end with a tail
expression (§5.6).

### 5.1 Variable binding — `:=`

```n
x := 42;              // immutable binding, type inferred
mut counter := 0;     // mutable binding
```

`:=` declares a new variable in the current scope, typed by its initializer
(§6.4). Bindings without `mut` must not be reassigned — the compiler rejects
the assignment with an error (enforced since v0.2). Function parameters are
immutable bindings. Binding an expression with no value (a `void` or `never`
call) is an error.

### 5.2 Assignment

```n
counter = counter + 1;
counter += 1;
counter -= 1;
```

Targets must be assignable places (a variable or field). Valid on `mut`
bindings.

### 5.3 `while`

```n
while n > 0 {
    n = n - 1;
}
```

### 5.4 `if` / `else`

```n
if x > 10 {
    put("big\n");
} else if x > 0 {
    put("small\n");
} else {
    put("non-positive\n");
}
```

`if` at statement position; braces are mandatory, parentheses around the
condition are not.

### 5.5 `return`, `break`, `continue`

```n
return;          // from a void function
return x + 1;    // with a value
break;           // exit innermost while
continue;        // next iteration
```

### 5.6 Block tail value

The final expression of a function body, written **without** a trailing `;`,
is the function's return value:

```n
fn main() -> i64 {
    setup();
    0                // ← returned
}
```

This is exactly equivalent to `return 0;`.

## 6. Expressions

### 6.1 Precedence (highest binds tightest)

| Level | Operators | Associativity |
|---|---|---|
| 1 | calls `f(x)`, field `a.b` | left |
| 2 | unary `-` `!` | right |
| 3 | `as` (cast) | left |
| 4 | `*` `/` `%` | left |
| 5 | `+` `-` | left |
| 6 | `<<` `>>` | left |
| 7 | `&` | left |
| 8 | `^` | left |
| 9 | `\|` | left |
| 10 | `==` `!=` `<` `<=` `>` `>=` | left |
| 11 | `&&` | left |
| 12 | `\|\|` | left |

Note the placement of `as` above arithmetic: `x as u32 + 1` casts first, then
adds. Comparison sits *below* the bitwise operators (unlike C), so
`flags & MASK == 0` means `(flags & MASK) == 0` — the common intent.

### 6.2 Casts — `as`

```n
msg.ptr as *u8       // pointer conversion
count as isize       // integer width/signedness conversion
```

`as` performs a C-style explicit conversion to the named type. Narrowing casts
truncate exactly as in C. Chaining is allowed: `x as u32 as u64`.

### 6.4 Type inference

Every expression has an inferred N type, computed by these rules (top match
wins):

| Expression | Inferred type |
|---|---|
| integer literal | `i64` (the language default) |
| `true` / `false` | `bool` |
| string literal, interpolated string | `str` |
| variable / parameter | its binding's type |
| `f(...)` | `f`'s declared return type |
| `s.ptr` / `s.len` (on `str`) | `*u8` / `u64` |
| comparison, `&&`, `\|\|`, `!` | `bool` |
| other unary / binary arithmetic | the (left) operand's type |
| `e as T` | `T` — casts are authoritative |

Inference is *minimal by design*: it types `:=` bindings, drives interpolation
formatting (§2.5), and enforces `mut` (§5.1). It does **not** yet verify
argument or operand compatibility — that is the N++ type checker (P1). Where
inference must guess (an unknown name), it assumes `i64`; use `as` to
override at the use site.

### 6.5 Static checks (since v0.3)

The compiler rejects the following, each with a `file:line` diagnostic:

- **Undeclared variables** — every name used in an expression must be a
  binding or parameter in scope.
- **Unknown functions** — every callee must be a declared `fn` or an
  `extern syscall` entry (only named functions are callable in N).
- **Wrong argument count** — call arity must match the declaration.
- **Argument type mismatches**, under these compatibility rules: any two
  integer types are call-compatible (C conversion semantics then apply);
  `str` matches only `str`; pointers must agree in depth, and base types
  must match unless either side is `*u8`/`*void` (byte pointers); an `as`
  cast changes the inferred type and is therefore authoritative.
- **Assignment to immutable bindings** (§5.1).

Not yet checked (completing N++ P1): operand mixing inside binary
expressions, the `return`/tail value against the declared return type, and
the assigned value against the target's type.

### 6.6 Calls and fields

Function calls take positional arguments. Field access uses `.` and applies to
`str` values today (`.ptr`, `.len`); it generalizes to user types in N++.
Method-call syntax (`value.method()`) is reserved for N++ and rejected by
`ncc` with a clear error.

## 7. Code generation contract

This section specifies what C the compiler is *required* to emit, because N's
"no hidden runtime" promise is part of the language.

### 7.1 Lowering table

| N construct | Generated C |
|---|---|
| `extern syscall fn f(...) -> T = N` | `static inline T' f(...) { return (T')__nyx_syscall6(N, args…, 0…); }` |
| `fn f(a: A) -> R { … }` | `R' f(A' a) { … }` + forward prototype |
| `x := e;` | `T' x = e';` where `T` is the inferred type (§6.4) |
| `str` literal `"abc"` | `((nyx_str){"abc", 3})` |
| block tail `e` | `return e';` (in a value-returning function) |
| `never` return | `void` fn + `for (;;) {}` after the syscall |

### 7.2 The runtime

Generated code includes exactly one header, `nyxrt.h`, providing: the
primitive typedefs, `nyx_str`, `__nyx_syscall6` (the inline-asm syscall
primitive) and three formatting helpers (`__nyx_fmt_begin/_str/_i64`) that are
each bounds-checked and freestanding. Programs link `crt0.o + nyxrt.o` and
nothing else.

### 7.3 Interpolation lowering

A string with interpolations lowers to a hoisted buffer built *before* the
statement that uses it:

```c
char __b0[256];
nyx_str __s0 = __nyx_fmt_begin(__b0, 256);
__nyx_fmt_str(&__s0, __b0, 256, (nyx_str){"pid=", 4});
__nyx_fmt_i64(&__s0, __b0, 256, (nyx_i64)(getpid()));
put(__s0);
```

The buffer is declared in the enclosing block scope — **never** inside a
statement-expression — so the resulting `nyx_str` cannot dangle. (This rule
exists because the naive `({ ... })` lowering was tried and produced a
dangling pointer; it is a required property of any future backend.) Inside a
loop body the buffer is per-iteration. The `str` produced by an interpolation
is valid until the end of the enclosing block.

## 8. Grammar

Condensed EBNF of the implemented language:

```ebnf
program      = { extern_block | fn_decl } ;

extern_block = "extern" "syscall" "{" { extern_fn } "}" ;
extern_fn    = "fn" ident params [ "->" type ] "=" int_lit [ ";" ] ;

fn_decl      = "fn" ident params [ "->" type ] block ;
params       = "(" [ param { "," param } ] ")" ;
param        = ident ":" type ;

type         = [ "raw" ] { "*" } ident ;

block        = "{" { stmt } [ expr ] "}" ;          (* trailing expr = tail *)
stmt         = [ "mut" ] ident ":=" expr ";"
             | expr ( "=" | "+=" | "-=" ) expr ";"
             | "return" [ expr ] ";"
             | "break" ";" | "continue" ";"
             | "while" expr block
             | "if" expr block [ "else" ( if_stmt | block ) ]
             | expr ";" ;

expr         = or ;
or           = and  { "||" and } ;
and          = cmp  { "&&" cmp } ;
cmp          = bor  { ("=="|"!="|"<"|"<="|">"|">=") bor } ;
bor          = bxor { "|" bxor } ;
bxor         = band { "^" band } ;
band         = shift{ "&" shift } ;
shift        = add  { ("<<"|">>") add } ;
add          = mul  { ("+"|"-") mul } ;
mul          = cast { ("*"|"/"|"%") cast } ;
cast         = unary { "as" type } ;
unary        = ( "-" | "!" ) unary | postfix ;
postfix      = primary { "." ident | "(" [ expr { "," expr } ] ")" } ;
primary      = int_lit | "true" | "false" | string | interp_string
             | ident | "(" expr ")" ;
```

String interpolation is handled lexically: the lexer emits
`HEAD { expr } [MID { expr }]* TAIL` token runs with a brace-depth stack, so
interpolated expressions are parsed by the ordinary expression grammar.

## 9. Limitations (v0.1 — honest list)

These are known, deliberate gaps in the bootstrap; each is queued for the
N++/type-checker phase:

1. **Checking is call-site-deep, not everywhere.** Names, arity, and argument
   types are verified (§6.5), but operand mixing inside binary expressions,
   return values, and assignment values still defer to the C compiler on the
   generated file. Completing this is the rest of N++ P1.
2. **Interpolation is text-or-decimal only.** `str` inserts text, everything
   else formats as signed decimal; there are no hex/width format controls yet.
3. **Missing constructs:** `struct`/`enum` definitions, `match`, `for`,
   closures, methods, modules/`use`, `defer`, `Result`/`?` — all specified in
   the N++ design document.
4. Fixed implementation caps (per file: 64 functions, 64 syscalls; per call:
   16 arguments; per function: 256 live locals) — generous for the bootstrap,
   diagnosed clearly when exceeded.

Resolved since v0.1: `:=` bindings now get concrete types with `i64` as the
integer default; interpolation dispatches by type; `mut` is enforced; the
generated C is strict C99 with no GNU extensions (TinyCC-compatible — this
removed `__auto_type`, which tcc does not support).

## 10. Toolchain

```bash
# build the compiler (any C99 compiler)
gcc -O2 -Wall -Wextra -o ncc lang/ncc/ncc.c

# transpile
./ncc lang/examples/hello.n -o hello.c

# compile for NyxOS (the user-space flags) and link
gcc -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone \
    -I user -c hello.c user/nyxrt.c
nasm -f elf64 user/crt0.asm -o crt0.o
ld -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 \
   -o hello.elf crt0.o nyxrt.o hello.o
```

See [`lang/ncc/README.md`](../ncc/README.md) for host-run testing (executing N
programs directly on Linux through the syscall-number shim) and the full
development workflow.

## 11. Versioning

The N spec version tracks the language, not the OS: v0.x are bootstrap
revisions (this document), v1.0 is the first checked language (type checker
enforcing §5.1/§9). Each spec change lands in the same commit as the compiler
change that implements it.
