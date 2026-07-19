# c64asm Reference Manual

`c64asm` is a two-pass 6502/6510 assembler for the Commodore 64. It reads a
plain-text assembly source file and produces a `.prg` file — a two-byte
little-endian load address followed by the assembled machine code — in the
format the C64 KERNAL and every C64 emulator expect.

Two interchangeable implementations exist, `c64asm.py` and `c64asm.c`. They
accept identical syntax and are verified to produce byte-identical output
for the same source file. Use whichever suits your environment.

---

## 1. Command-line usage

```
python3 c64asm.py <input.asm> -o <output.prg> [--listing <file.lst>] [--vice-labels <file>] [--lib-dir <dir>]
cc -O2 -o c64asm c64asm.c && ./c64asm <input.asm> -o <output.prg> [--listing <file.lst>] [--vice-labels <file>] [--lib-dir <dir>]
```

| Argument | Required | Description |
|---|---|---|
| `<input.asm>` | yes | Path to the assembly source file. |
| `-o`, `--output <file>` | yes | Path to write the assembled `.prg` file. |
| `--listing <file>` | no | Write a listing file: addresses, encoded bytes, source lines, and a final symbol table. |
| `--vice-labels <file>` | no | Write a VICE monitor label file for debugging by name (§16). |
| `--lib-dir <dir>` | no | Fallback search directory for `.include` (§10). See below. |
| `-h`, `--help` | no | Print a short usage message. |

On success, the assembler prints the number of bytes assembled and the
origin address:

```
Assembled 60 bytes, origin=$0801 -> hello.prg
```

On error, it prints one or more messages to stderr identifying the line
number and source text, and exits with a non-zero status. Most kinds of
mistake are collected and reported together rather than stopping at the
first one — see §17's "Multiple errors per run"; a handful of
whole-file structural problems still stop assembly immediately.

```
Assembly error: Undefined symbol in operand 'undefined_thing' (line 2: lda undefined_thing)
```

### `--lib-dir`: a shared library directory across projects

By default (§10), a non-absolute `.include "path"` resolves relative to
the directory of the file containing that line — which is exactly why
`.include "lib/text.inc"` works without needing to know where the
assembler itself was invoked from, but also means every project
directory needs its own copy of `lib/` sitting next to its source
files.

`--lib-dir <dir>` is a **fallback only** — it changes nothing about
default resolution, which is always tried first. It's consulted only
when that default lookup fails to find the file. `<dir>` names the
`lib/` directory **itself** (the one directly containing `text.inc`,
`input.inc`, and so on) — not its parent — so a leading `lib/` in the
`.include` path is stripped before joining with `--lib-dir`:

```
project-a/
  main.asm            ; .include "lib/text.inc"
project-b/
  main.asm            ; .include "lib/text.inc"
shared-c64lib/
  text.inc
  input.inc
  ...
```

```
cd project-a && c64asm main.asm -o main.prg --lib-dir ../shared-c64lib
```

Here, `.include "lib/text.inc"` first looks for `project-a/lib/text.inc`
(not present), then, because `--lib-dir ../shared-c64lib` was given,
strips the leading `lib/` and tries `../shared-c64lib/text.inc`,
finding it there. Neither `project-a/` nor `project-b/` needs its own
copy of `lib/`. A project that *does* keep a local `lib/` of its own is
unaffected either way — the local copy is found by the default lookup
before `--lib-dir` is ever consulted, so it's safe to pass `--lib-dir`
unconditionally (e.g. from a wrapper script or Makefile) without it
silently overriding a project's own local files. A requested path that
doesn't start with `lib/` is joined with `--lib-dir` as-is, unstripped.

If neither location has the file, the error names both paths that were
tried:

```
Assembly error: Cannot open included file 'lib/text.inc' (also tried '../shared-c64lib/text.inc' via --lib-dir) (main.asm, line 1: .include "lib/text.inc")
```

`--lib-dir` only applies to `.include` lines (an absolute `.include`
path is never subject to it, same as it's never subject to the default
relative resolution) — it has no effect on how `<input.asm>` itself is
found.

---

## 2. Source line structure

Each line may contain, in order: an optional **label**, an optional
**mnemonic or directive**, an optional **operand**, and an optional
**comment**. Any of these may be blank.

```
label:      LDA     #$00        ; comment
[label]     [op]    [operand]   [; comment]
```

- **Comments** start with `;` and run to the end of the line. A `;` inside
  a double-quoted string is not treated as a comment.
- **Labels** may be written with a trailing colon (`loop:`) or without one,
  as long as they're followed by whitespace and then either an
  instruction/directive, or nothing at all (a bare label line, useful for
  marking an address with no code on that line):

  ```
  loop:                   ; label with colon
  loop    lda #$00        ; label without colon, followed by code
  loop                    ; bare label, colon optional
          lda #$00
  ```
- Label and mnemonic names are case-insensitive for **mnemonics and
  directives** (`LDA`, `lda`, and `Lda` are equivalent). Label and symbol
  **names** are case-sensitive (`Loop` and `loop` are different symbols).
- Whitespace (spaces or tabs) between fields is not significant beyond
  separating tokens; indentation is purely stylistic.

### Constant / symbol assignment

```
SCREEN = $0400
BORDER = $D020
COUNT  = 10
```

or equivalently, using the `.equ` directive:

```
SCREEN .equ $0400
```

Unlike labels (which are bound to the current program-counter address),
a symbol defined with `=` or `.equ` is bound to the value of the
expression on the right-hand side, evaluated at that point in assembly.
Re-assigning the same symbol later in the source (e.g. inside a loop of
`.equ` redefinitions) is allowed; redefining a **label** to a different
address is an error.

---

## 3. Numbers and literals

| Form | Example | Meaning |
|---|---|---|
| Decimal | `1234` | Base-10 integer |
| Hexadecimal | `$1234`, `$ff` | `$` prefix, any number of hex digits |
| Binary | `%01011010` | `%` prefix, `0`/`1` digits only |
| Character | `'A'` | The byte value of the character |
| Current PC | `*` | The program counter at the start of the current line |

Character literals are minimal: `'A'` gives the value of `A` as written
(there is no PETSCII translation applied to character literals used in
*expressions* — that translation only happens for the text emitted by
`.text` / `.asc` / quoted `.byte` arguments; see §6).

---

## 4. Expressions

Expressions may combine numbers, symbols, and the current PC using:

| Operator | Meaning | Notes |
|---|---|---|
| `+` `-` | addition, subtraction | left-associative, lowest precedence |
| `*` `/` | multiplication, integer division | left-associative; division by zero evaluates to `0` rather than erroring |
| `( )` | grouping | |
| unary `-` | negation | |
| unary `<` | low byte | `<expr` = `expr & $FF` |
| unary `>` | high byte | `>expr` = `(expr >> 8) & $FF` |

Examples:

```
lda #<message       ; low byte of the message address
lda #>message       ; high byte of the message address
lda ($10+2),y        ; arithmetic inside a zero-page indirect expression
.word BASE + OFFSET * 4
```

A symbol referenced before it has been defined (a **forward reference**)
is allowed; the assembler resolves it in a first pass over the whole file
before generating any code in the second pass. Using an expression that is
still undefined by the *end* of assembly is an error.

---

## 5. Addressing modes

The assembler infers the addressing mode from the operand's syntax. Where
both a zero-page and an absolute encoding exist for an instruction, the
shorter zero-page form is chosen automatically whenever the operand's
value is known at assemble time to fit in a single byte (0–255); otherwise
absolute is used. A hex literal written with more than two digits (e.g.
`$0050`) is always treated as absolute even though its value is under
256, letting you force the longer encoding when you need to.

| Mode | Syntax | Example |
|---|---|---|
| Implied | *(no operand)* | `RTS` |
| Accumulator | `A` | `ASL A` |
| Immediate | `#expr` | `LDA #$10` |
| Zero page | `expr` (value ≤ 255) | `LDA $10` |
| Zero page,X | `expr,X` | `LDA $10,X` |
| Zero page,Y | `expr,Y` | `LDX $10,Y` |
| Absolute | `expr` (value > 255, or forced) | `LDA $1000` |
| Absolute,X | `expr,X` | `LDA $1000,X` |
| Absolute,Y | `expr,Y` | `LDA $1000,Y` |
| Indirect | `(expr)` | `JMP ($1000)` — JMP only |
| Indexed indirect | `(expr,X)` | `LDA ($10,X)` |
| Indirect indexed | `(expr),Y` | `LDA ($10),Y` |
| Relative | `expr` | `BNE loop` — branches only, target must be within −128..+127 bytes of the instruction after the branch |

If an instruction doesn't support the addressing mode implied by the
operand's syntax (e.g. `STA #$10`, since `STA` has no immediate mode), the
assembler reports an error naming the mnemonic and the unsupported mode.

---

## 6. Text and PETSCII

`.text`, `.asc`, and quoted string arguments to `.byte`/`.db` are encoded
as PETSCII bytes suitable for output through the KERNAL `CHROUT` routine.
How letters get encoded depends on the `.charset` directive:

```asm
        .charset upper      ; the default
        .charset lower
```

`.text`/`.asc` also accept numeric arguments mixed in with quoted
strings, exactly like `.byte` does — each one is emitted as a single
raw byte, not PETSCII-encoded, regardless of the current `.charset`
mode:

```asm
        .text "HELLO", 13, 0    ; string, then carriage return, then a
                                   ; null terminator -- no separate
                                   ; .byte line needed
```

This is really the same directive as `.byte`, just defaulting to
PETSCII-encoding its arguments instead of leaving them as raw numbers
— an argument like `13` means exactly the same thing whether it
appears in a `.text` line or a `.byte` line; only the quoted-string
arguments are treated differently between the two.

### `.charset upper` (the default)

Every letter, whatever case it was written in, becomes a PETSCII byte
in the `$41`–`$5A` range:

- Uppercase source letters are emitted unchanged — on the **default**
  (uppercase/graphics) C64 character set, these PETSCII codes display
  exactly like ASCII.
- Lowercase source letters are folded up to uppercase.

This mode assumes the program never switches the C64 away from its
default (power-on) character set. That's true of every program that
doesn't deliberately do otherwise, so this is a safe, predictable
default — but see the caveat at the end of this section.

### `.charset lower`

Letters keep their original case, using PETSCII's actual encoding for
it:

- Lowercase source letters become PETSCII `$41`–`$5A` (the "unshifted"
  range) — this displays as **lowercase** specifically on the C64's
  *lowercase/uppercase* character set, not the default one.
- Uppercase source letters become PETSCII `$C1`–`$DA` (the "shifted"
  range) — this displays as uppercase on **either** character set.

This is what actually produces mixed-case text on screen — but only
once the C64 has been switched, **at runtime**, onto its
lowercase/uppercase character set. `.charset lower` only controls
which *bytes* get assembled; switching the actual hardware character
set is a separate step your program has to do itself, typically via
`lib/text.inc`'s `SET_LOWERCASE_CHARSET` macro (see
`lib-reference.md`), or directly:

```asm
        lda #$0e        ; PETSCII "switch to lowercase/uppercase charset"
        jsr CHROUT
```

Forgetting this step is an easy mistake to make, and an easy one to
miss: the program assembles and runs fine, and the text just quietly
displays in uppercase, same as it always did, with nothing pointing at
a forgotten step.

### The mixing caveat

`.charset upper` text is only guaranteed to display as uppercase while
the C64's *default* character set is still active. If a program ever
switches to the lowercase/uppercase character set — for some
`.charset lower` text elsewhere — any `.charset upper` text printed
**after** that point displays as lowercase too, since `$41`–`$5A`
means something different once the character set has changed.

The fix is simple: once a program switches its character set at
runtime, use `.charset lower` for everything it prints from that point
on, even text you want to appear in all caps — just write that text in
actual uppercase in the source. `.charset lower` already encodes
uppercase letters using the `$C1`–`$DA` range specifically so this
works correctly regardless of which character set happens to be
active.

```asm
        .charset lower
title_msg:
        .text "ADVENTURE"      ; still displays in caps -- $C1-$DA is
        .byte 13, 0              ; uppercase on either character set
body_msg:
        .text "You are standing in a small room."
        .byte 13, 0
```

---

## 7. Directives

| Directive | Aliases | Operand | Effect |
|---|---|---|---|
| `.org expr` | `* = expr` | one expression | Set the program counter. The *first* `.org` (or `.basic`) encountered also sets the file's load address (the two-byte header written to the `.prg`). |
| `.byte a, b, ...` | `.db` | comma-separated bytes or `"strings"` | Emit raw bytes. Numeric arguments are truncated to 8 bits; quoted strings are PETSCII-encoded (§6). |
| `.word a, b, ...` | `.dw` | comma-separated 16-bit values | Emit each value as two bytes, little-endian. |
| `.text "...", n, ...` | `.asc` | comma-separated `"strings"` and/or numeric bytes | PETSCII-encode and emit text; numeric arguments are emitted as raw bytes (§6), the same as `.byte` — useful for a terminator or control code right after a string, e.g. `.text "HELLO", 13, 0`. |
| `.fill count, value` | `.ds`, `.res` | count, optional fill byte (default 0) | Emit `count` bytes of `value`. |
| `.align n` | — | one expression | Pad with zero bytes, if necessary, until the program counter is a multiple of `n`. A no-op if it already is. `n` must evaluate to a positive value. A label on the same line (`sprite_data: .align 64`) is bound to the *aligned* address, matching how `.org`'s same-line label works. |
| `.basic [start]` | — | optional label/expression | Emit a tokenized BASIC line `10 SYS <addr>` at `$0801`, where `<addr>` is automatically computed to point at the very next byte of assembled code — i.e. wherever the code following `.basic` ends up. Typing `LOAD"...",8,1` then `RUN` starts the machine code directly. Must appear before any code you want it to `SYS` into. With an operand, also emits `jmp start` immediately after the stub — see the example and gotcha below. |
| `.cpu 6510` / `.cpu 6510x` | `.cpu 6502` (synonym for `6510`) | one mode name | Switches illegal/undocumented-opcode support (§13) off (`6510`, the default) or on (`6510x`) from this point in the file forward. |
| `.charset upper` / `.charset lower` | — | one mode name | Switches how `.text`/`.asc`/`.byte` string literals encode letters (§6) from this point in the file forward: forced-uppercase (`upper`, the default) or case-preserving (`lower`). |
| `.error "msg"` | — | one quoted string | Records a recoverable error with the given message (§11) — typically paired with `.ifdef`/`.ifndef` to check a precondition. |
| `.warning "msg"` | — | one quoted string | Prints the given message but never affects whether assembly succeeds (§11). |
| `label = expr` | `.equ` | one expression | Bind `label` to the value of `expr` (not to the current PC). See §2. |

`.byte`/`.word`/`.text`/`.fill` all accept comma-separated argument lists,
so `.byte $01, $02, "AB", $00` mixes numeric bytes and quoted text on a
single line — and the same goes for `.text`: `.text "HELLO", 13, 0` emits
the PETSCII-encoded string followed by two raw bytes (a carriage return
and a null terminator), without needing a separate `.byte` line the way
`.text "HELLO"` followed by `.byte 13, 0` used to.

### `.align` example

Sprite pointers, and several other things a real C64 program cares
about, need to sit at an address that's a multiple of some fixed size —
64 bytes for sprite data, 256 for a page boundary, and so on. Without
`.align`, getting there means manually working out the next safe address
by hand and hard-coding it (typically with `.org` or `*=`), then
redoing that arithmetic by hand every time the code above it grows or
shrinks enough to cross a boundary:

```
        * = $c000
        lda #$00
        sta $d015
        jmp start

        .align 64
sprite0_data:
        .byte $ff,$ff,$ff
        .fill 61, $00

start:
        lda #<sprite0_data
        sta $07f8
        rts
```

`sprite0_data` always lands on the next 64-byte boundary, however much
the code above it changes — no manual recomputation, ever.

### `.basic` example

```
        .basic
start:
        lda #$00
        sta $d020
        rts
```

assembles to a file whose first bytes are a valid `10 SYS 2061` BASIC
line (or whatever address the code actually starts at — the assembler
computes this for you), immediately followed by the `start:` code.

### `.basic start`: the entry-point gotcha, and its fix

Without an operand, `.basic`'s `SYS` stub jumps to whatever comes
**immediately after** the `.basic` line — not to a label named `start`
or anything else specific. That's harmless above, since `start:` really
is the next thing in the file. It stops being harmless the moment
something that emits real code — most commonly an `.include`d library
file — sits between `.basic` and your actual entry point:

```
        .basic
        .include "lib/text.inc"   ; emits print_msg's real code
start:
        ; SYS lands inside print_msg above, not here
```

`SYS` runs whatever that included code happens to be first, with
whatever garbage is in the registers at power-on, instead of your
program. This is a real bug this project's own demos hit more than
once — nothing about it looks wrong from the source, since `start:` is
still right there in the file; it's just not where execution actually
begins.

Giving `.basic` your entry point's label fixes it by construction:

```
        .basic start
        .include "lib/text.inc"
start:
        ; SYS now lands here correctly, no matter what's above
```

`.basic start` emits `jmp start` (a plain 3-byte absolute jump)
immediately after the loader stub, so it no longer matters what ends up
between `.basic` and `start:`. The operand can be any label or
expression, evaluated the same way any instruction operand is —
including a forward reference to a label defined later in the file, as
in the example above. There's no real downside to using this form even
in a program with nothing between `.basic` and `start:` yet, since it's
just one harmless extra jump; it's the recommended default.

---

## 8. Macros

```
.macro NAME param1, param2, ...
        ; body, referencing \param1, \param2, ...
.endmacro
```

Invoked like a pseudo-instruction:

```
        NAME arg1, arg2
```

At the point of invocation, every `\paramname` in the macro's body is
replaced with the corresponding argument's literal text, and the
resulting lines are assembled exactly as if you'd typed them out by
hand. Zero-parameter macros (`.macro NAME` with nothing after the name)
are fine too.

### Example

```
.macro PRINT msg
        lda #<\msg
        ldy #>\msg
        jsr print_msg
.endmacro

        PRINT hello_msg
        PRINT goodbye_msg
```

expands to exactly the same bytes as writing the six `lda`/`ldy`/`jsr`
lines out by hand, twice, with `hello_msg` and `goodbye_msg`
substituted in respectively.

### Labels inside a macro body

A macro is expanded as plain text substitution — an ordinary label
defined inside a macro's body is a completely ordinary label once
expanded, which means invoking that macro a second time defines the
*same* label name again and fails with "Symbol already defined". Use an
`@`-prefixed **local label** (§9) instead of an ordinary one, and it
just works, with nothing extra to write:

```
.macro DELAY
        ldx #$00
@loop:
        dex
        bne @loop
.endmacro

        DELAY
        DELAY
```

Each invocation automatically gets its own distinct `@loop`, with no
suffix parameter or other bookkeeping needed — see §9 for exactly why.

### Rules and limitations

- **Macros must be defined before they're used** — there's no
  whole-file pre-scan for definitions, so a macro invocation earlier in
  the file than its `.macro` block won't be recognized as one.
- **A macro invocation can't share a line with a label.** Put the label
  on the line above instead:
  ```
  loop:
          MY_MACRO arg
  ```
- **Macro arguments are split the same comma/paren/quote-aware way
  directive argument lists are** (see `.byte`, §7) — which means a full
  indexed addressing expression like `(ptr),Y` can't be passed as a
  single argument, since its comma sits *outside* the parentheses.
  Parameterize just the base address and bake the `,Y` into the macro
  body itself:
  ```
  .macro LOAD_INDIRECT_Y base
          ldy #$00
          lda (\base),y
  .endmacro
  ```
- **A macro name can't collide with a real mnemonic or directive** —
  `.macro LDA ...` is rejected outright.
- **Nested macro *definitions*** (a `.macro` block inside another
  macro's body) **are rejected.** Nested *invocations* — one macro
  calling another — are fine, including recursively, up to a depth of
  16; a macro that (directly or indirectly) invokes itself past that
  depth is reported as an error rather than hanging the assembler.
- **Errors inside an expanded macro body are attributed to the line
  that invoked the macro**, not a specific line inside the macro's own
  definition. For the typically-short macro bodies this assembler is
  meant for, that's usually enough to find the problem; it just means
  the line number in an error message points at the call site, not
  necessarily the exact body line at fault.

---

## 9. Local labels

```
@name
```

A label name starting with `@` is **local**: it's automatically
distinct within its own scope, so the same `@name` can be reused
elsewhere in the file — in a different subroutine, or in a different
invocation of the same macro — without colliding.

A new scope begins:

- each time an ordinary (non-`@`) label is defined with the colon form,
  `label:`, and
- each time a macro invocation (§8) begins expanding, with the previous
  scope restored once that invocation is done.

### Example: two subroutines reusing the same local names

```
sub_a:
@loop:
        dex
        bne @loop
        rts

sub_b:
@loop:
        dey
        bne @loop
        rts
```

Both `@loop`s are distinct — `sub_a`'s `bne @loop` can only ever branch
to `sub_a`'s own `@loop:`, never `sub_b`'s.

### Example: a macro with an internal label, invoked repeatedly

```
.macro DELAY
        ldx #$00
@loop:
        dex
        bne @loop
.endmacro

        DELAY
        DELAY
```

Each `DELAY` invocation gets its own `@loop`, automatically — this is
the main reason local labels exist: before this feature, a macro
containing a label needed a dedicated suffix parameter (e.g.
`delay_loop\suffix:`) threaded through by every caller just to avoid
collisions. `@`-labels make that unnecessary.

### Rules and limitations

- **A new scope is only recognized from the explicit colon form**,
  `label:`. A bare label with no colon does not start a new scope. This
  matches how every label in this project's own example programs is
  actually written, so it isn't a practical restriction, but it is a
  real one worth knowing about.
- **Referencing an `@name` from outside the scope it was defined in**
  doesn't raise a dedicated "out of scope" error — it produces an
  ordinary **"Undefined symbol"** error instead, since the reference and
  the (different-scope) definition end up as two different internal
  names. This is a natural consequence of how local labels are
  implemented (see below), not a special case the assembler checks for.
- **`@` inside a double-quoted string is left alone.**
  `.text "user@example.com"` assembles the `@` as a literal character,
  exactly as you'd expect, not as a local-label reference.
- Implementation note, if the exact internal naming ever matters (e.g.
  reading a `--listing` file): `@name` is rewritten internally to
  `__local<N>_name` for some scope number `N`, before the rest of the
  assembler ever sees it. Don't manually define a label matching that
  pattern yourself.

---

## 10. Includes

```
.include "path"
```

Splices another file's lines into the source stream at that point, as
if they'd been pasted in directly. `path` is resolved **relative to the
directory of the file containing the `.include` line** — not the
current working directory — which is what lets one library file
`.include` another file sitting next to it, regardless of where the
assembler itself was invoked from. An absolute path (starting with `/`)
is used as-is. If that default resolution doesn't find the file, and
the `--lib-dir` command-line option (§1) was given, `path` is also
tried relative to it, as a fallback — see §1 for the full details and
an example of sharing one library directory across separate projects.

### Example: a small shared-constants library file

`constants.inc`:
```
BORDER_COLOR = $06
BACKGROUND_COLOR = $00
```

`main.asm`:
```
        * = $c000
        .include "constants.inc"
        lda #BORDER_COLOR
        sta $d020
        lda #BACKGROUND_COLOR
        sta $d021
        rts
```

This assembles to exactly the same bytes as pasting `constants.inc`'s
two lines directly into `main.asm` in place of the `.include` line.

### Nested includes

A file that was itself reached via `.include` can `.include` further
files of its own, resolved relative to *its own* directory (not the
top-level file's) — so a `lib/graphics.inc` that includes `sprites.inc`
finds `lib/sprites.inc`, not a same-named file next to `main.asm`.

### Including the same file more than once

Unlike C's `#include`, `.include` does **not** require manual include
guards to be used safely more than once. If two different files both
`.include` the same shared library file (a common "diamond dependency"
— e.g. `main.asm` includes both `graphics.inc` and `sound.inc`, and
both of *those* `.include` a shared `zeropage.inc` defining pointer
constants), the shared file is only actually processed the first time;
later `.include`s of the exact same file are silently skipped, the same
way `#pragma once` works in C. This assembler has no conditional
assembly, so it has no way to write a manual include guard even if it
wanted to — and a shared file being pulled in from more than one place
is the normal, expected shape of a real multi-file project, not a
mistake to flag with a "Symbol already defined" error.

This same-file detection compares each `.include`'s fully resolved,
canonical path (symlinks and `..` resolved) — not the literal text
after `.include` — so the same physical file reached via two
syntactically different relative paths (e.g. from two files in
different directories) is still correctly recognized as one file.

### Rules and limitations

- **Circular includes are detected and rejected**, with the full chain
  shown (`a.asm -> b.inc -> a.asm`), rather than hanging or failing with
  a generic error.
- **A hard limit of 16 levels of nested includes**, as a backstop in
  case an include cycle somehow isn't caught by the check above.
- **A missing included file** produces a clear error naming the file
  and where it was `.include`d from, rather than a generic filesystem
  error.
- Once `.include` is used **anywhere** in a program, every subsequent
  error message — including ones from the top-level file itself — names
  which file it came from (see §17). A program that never uses
  `.include` sees no change in its error output at all.

---

## 11. Conditional assembly

```
.if expr
        ; assembled only if expr is nonzero
.elif expr2
        ; assembled only if expr was zero and expr2 is nonzero
.else
        ; assembled only if every preceding condition was zero
.endif
```

```
.ifdef NAME
        ; assembled only if NAME is a defined symbol
.else
        ; assembled only if it isn't
.endif
```

```
.ifndef NAME
        ; the exact opposite of .ifdef
.endif
```

`.elif` and `.else` are both optional, and either form can be nested
inside the other. `.elif` is only valid after `.if` (not after `.ifdef`/
`.ifndef` — see Rules below).

### Example: PAL/NTSC timing

```
PAL = 1

.if PAL
        FRAME_CYCLES = 19656
.else
        FRAME_CYCLES = 17030
.endif
```

Flipping the single `PAL` constant at the top of the file is enough to
retarget every place `FRAME_CYCLES` is used, without maintaining two
separate copies of the surrounding code.

### What `.if` can and can't gate

Unlike macros (§8) and `.include` (§10), conditional assembly is
resolved **while assembling**, not beforehand — which is deliberate: it
means a condition can see real constants and labels defined with `=`,
not just things known before any actual parsing happens. The trade-off
is the reverse of what you might expect from other assemblers' `#if`-
style preprocessors: `.if` can gate whether **instructions and data
directives** get assembled (including ones inside a macro's body,
correctly, on every separate invocation), but it **cannot** gate which
`.macro` gets *defined* or which file gets `.include`d, because macro
definitions and includes are already fully resolved before `.if` is
ever evaluated. Writing something like:

```
.if DEBUG
.macro LOG msg
        ; ...
.endmacro
.else
.macro LOG msg
        ; a different, no-op definition
.endmacro
.endif
```

does **not** work — both `.macro LOG` definitions get registered
regardless of `DEBUG`'s value, and assembly fails with `macro 'LOG'
already defined`. If you need genuinely different macro behavior,
define one macro whose *body* contains the `.if`, the way the PAL/NTSC
example above does with ordinary code.

### `.error` and `.warning`

```
.error "message"
.warning "message"
```

Paired with `.ifdef`/`.ifndef` above, `.error` turns a missing
precondition into one clear, specific message right at the point of
the mistake, instead of a confusing `Undefined symbol` buried inside a
macro or library routine's own body several `.include`s away:

```asm
.ifndef gfx_ptr
.error "graphics.inc requires gfx_ptr (2-byte zero page) defined before this .include"
.endif
```

`.error`'s message is collected the same way any other recoverable
error is (§17) — several independent `.error`s (two different missing
zero-page symbols in two different included files, say) can all be
reported together in one run, not just the first. It does not stop
assembly by itself; whether assembly ultimately fails still depends on
whether *any* error (from `.error` or anything else) was recorded by
the time pass 1 finishes.

`.warning` prints its message the same way but never affects whether
assembly succeeds — no error is recorded, pass 2 still runs, and a
`.prg` still gets written. Useful for a softer note ("this feature is
experimental") that shouldn't block the build.

Both require a single quoted string operand; anything else (a bare
word, a numeric expression, more than one comma-separated argument) is
itself a recoverable error naming which directive and what's expected.
Neither directive PETSCII-encodes its message or does anything with
the current `.charset` (§6) setting — this text is a diagnostic for
whoever is assembling the program, not something that ends up in the
assembled output.

### Rules and limitations

- **`.if`/`.elif` conditions must not reference a forward-declared
  symbol.** Unlike almost every other expression in this assembler,
  this is a hard error — checked identically on both assembly passes,
  not deferred to the second one. This is a correctness requirement,
  not an arbitrary restriction: a wrong guess about an ordinary
  expression's value only affects a byte value, silently corrected once
  the real value is known, but a wrong guess about a *condition* changes
  which lines exist at all, which would desynchronize every address
  computed afterward between the two passes. In practice this means a
  condition should reference a constant defined with `=` earlier in the
  file (as in the PAL/NTSC example above), not a label or constant that
  appears later.
- **`.ifdef`/`.ifndef` ask "was this defined *before this line*"**, not
  "does it exist anywhere in the file" — so a symbol defined later still
  reads as undefined at an earlier `.ifdef`, exactly as you'd expect.
- **A hard limit of 16 levels of nested conditionals**, as a backstop
  against runaway nesting.
- **An unclosed `.if`/`.ifdef`/`.ifndef`** (missing its `.endif`) is a
  fatal error at end of file, naming the line the unclosed block started
  on.
- A label can't share a line with `.if`/`.elif`/`.else`/`.endif`/
  `.ifdef`/`.ifndef` — these directives don't advance the program
  counter, so there's no meaningful address to bind such a label to.

---

## 12. Instruction set


All 56 documented NMOS 6502/6510 mnemonics are supported. Each entry below
lists the addressing modes the instruction accepts and the opcode byte for
each. Modes not listed are not valid for that instruction and will produce
an assembly error.

| Mnemonic | Supported modes (mode=opcode) |
|---|---|
| ADC | IMM=$69 ZP=$65 ZPX=$75 ABS=$6D ABSX=$7D ABSY=$79 INDX=$61 INDY=$71 |
| AND | IMM=$29 ZP=$25 ZPX=$35 ABS=$2D ABSX=$3D ABSY=$39 INDX=$21 INDY=$31 |
| ASL | ACC/IMP=$0A ZP=$06 ZPX=$16 ABS=$0E ABSX=$1E |
| BCC | REL=$90 |
| BCS | REL=$B0 |
| BEQ | REL=$F0 |
| BIT | ZP=$24 ABS=$2C |
| BMI | REL=$30 |
| BNE | REL=$D0 |
| BPL | REL=$10 |
| BRK | IMP=$00 |
| BVC | REL=$50 |
| BVS | REL=$70 |
| CLC | IMP=$18 |
| CLD | IMP=$D8 |
| CLI | IMP=$58 |
| CLV | IMP=$B8 |
| CMP | IMM=$C9 ZP=$C5 ZPX=$D5 ABS=$CD ABSX=$DD ABSY=$D9 INDX=$C1 INDY=$D1 |
| CPX | IMM=$E0 ZP=$E4 ABS=$EC |
| CPY | IMM=$C0 ZP=$C4 ABS=$CC |
| DEC | ZP=$C6 ZPX=$D6 ABS=$CE ABSX=$DE |
| DEX | IMP=$CA |
| DEY | IMP=$88 |
| EOR | IMM=$49 ZP=$45 ZPX=$55 ABS=$4D ABSX=$5D ABSY=$59 INDX=$41 INDY=$51 |
| INC | ZP=$E6 ZPX=$F6 ABS=$EE ABSX=$FE |
| INX | IMP=$E8 |
| INY | IMP=$C8 |
| JMP | ABS=$4C IND=$6C |
| JSR | ABS=$20 |
| LDA | IMM=$A9 ZP=$A5 ZPX=$B5 ABS=$AD ABSX=$BD ABSY=$B9 INDX=$A1 INDY=$B1 |
| LDX | IMM=$A2 ZP=$A6 ZPY=$B6 ABS=$AE ABSY=$BE |
| LDY | IMM=$A0 ZP=$A4 ZPX=$B4 ABS=$AC ABSX=$BC |
| LSR | ACC/IMP=$4A ZP=$46 ZPX=$56 ABS=$4E ABSX=$5E |
| NOP | IMP=$EA |
| ORA | IMM=$09 ZP=$05 ZPX=$15 ABS=$0D ABSX=$1D ABSY=$19 INDX=$01 INDY=$11 |
| PHA | IMP=$48 |
| PHP | IMP=$08 |
| PLA | IMP=$68 |
| PLP | IMP=$28 |
| ROL | ACC/IMP=$2A ZP=$26 ZPX=$36 ABS=$2E ABSX=$3E |
| ROR | ACC/IMP=$6A ZP=$66 ZPX=$76 ABS=$6E ABSX=$7E |
| RTI | IMP=$40 |
| RTS | IMP=$60 |
| SBC | IMM=$E9 ZP=$E5 ZPX=$F5 ABS=$ED ABSX=$FD ABSY=$F9 INDX=$E1 INDY=$F1 |
| SEC | IMP=$38 |
| SED | IMP=$F8 |
| SEI | IMP=$78 |
| STA | ZP=$85 ZPX=$95 ABS=$8D ABSX=$9D ABSY=$99 INDX=$81 INDY=$91 |
| STX | ZP=$86 ZPY=$96 ABS=$8E |
| STY | ZP=$84 ZPX=$94 ABS=$8C |
| TAX | IMP=$AA |
| TAY | IMP=$A8 |
| TSX | IMP=$BA |
| TXA | IMP=$8A |
| TXS | IMP=$9A |
| TYA | IMP=$98 |

Note: `ASL`, `LSR`, `ROL`, and `ROR` accept either `A` (accumulator) or no
operand at all — both encode identically, so `ASL` and `ASL A` are
interchangeable.

The 6502/6510 also executes a further set of undocumented/illegal
opcodes, not part of the documented instruction set above — see §13
below.

---

## 13. Illegal opcodes

> **These are not standard 6502 instructions.** MOS never documented or
> supported any of the mnemonics on this page. They're real opcode
> bytes the NMOS 6502/6510 happens to execute as a side effect of how
> its instruction decoder is wired — nothing in the chip actually
> "traps" an unused opcode the way an illegal instruction would on a
> modern CPU — but their behavior was never a design guarantee, a few
> of them are explicitly unstable (see the table below), and non-NMOS
> variants of the 6502 family (65C02, and others) do **not** implement
> them the same way, or at all. Real C64/C128 hardware uses an NMOS
> chip (6510/8500/8502), so every opcode below is safe to rely on
> there specifically — but code using them is inherently less portable
> than code using only the documented instruction set, and is exactly
> the kind of thing worth calling out with a comment at the point of
> use.

By default, `c64asm` refuses to assemble any of these — using one
without first enabling them is an assembly error, the same as an
unrecognized mnemonic:

```
Assembly error: Illegal/undocumented opcode 'LAX' used without '.cpu 6510x' -- see c64asm-reference.md's "Illegal opcodes" section (line 12: lax $10)
```

### The `.cpu` directive

```asm
        .cpu 6510x      ; enable illegal/undocumented opcodes
        lax $fb         ; now assembles fine

        .cpu 6510       ; back to standard-only (also the default; "6502"
                         ; works identically as a synonym)
        lax $fb         ; error again -- .cpu takes effect from this
                         ; point in the file forward, not retroactively
```

`.cpu` takes exactly one operand, `6510` or `6510x` (case-insensitive;
`6502` is accepted as a synonym for `6510`). Any other operand is an
error. The setting is positional: it affects every instruction *after*
the `.cpu` line until the next `.cpu` line changes it again, and the
file starts in standard-only (`6510`) mode regardless of whether it
uses `.include` — each included file doesn't get its own independent
setting; there's just one setting, wherever the most recent `.cpu`
line (in whichever file) left it.

### Opcode table

Mnemonics and opcode assignments follow the widely-used
[oxyron.de table](http://www.oxyron.de/html/opcodes02.html), the
standard reference for this in the C64 scene. A `*` marks an opcode
noted as unstable or highly unstable — see the stability notes below
the table.

| Mnemonic | Supported modes (mode=opcode) | Effect |
|---|---|---|
| SLO | ZP=$07 ZPX=$17 INDX=$03 INDY=$13 ABS=$0F ABSX=$1F ABSY=$1B | `{adr} := {adr}*2; A := A or {adr}` (ASL + ORA) |
| RLA | ZP=$27 ZPX=$37 INDX=$23 INDY=$33 ABS=$2F ABSX=$3F ABSY=$3B | `{adr} := {adr} rol; A := A and {adr}` (ROL + AND) |
| SRE | ZP=$47 ZPX=$57 INDX=$43 INDY=$53 ABS=$4F ABSX=$5F ABSY=$5B | `{adr} := {adr}/2; A := A xor {adr}` (LSR + EOR) |
| RRA | ZP=$67 ZPX=$77 INDX=$63 INDY=$73 ABS=$6F ABSX=$7F ABSY=$7B | `{adr} := {adr} ror; A := A + {adr} + C` (ROR + ADC) |
| SAX | ZP=$87 ZPY=$97 INDX=$83 ABS=$8F | `{adr} := A and X` |
| LAX | ZP=$A7 ZPY=$B7 INDX=$A3 INDY=$B3 ABS=$AF ABSY=$BF IMM=$AB\* | `A, X := {adr}` (LDA + LDX in one) |
| DCP | ZP=$C7 ZPX=$D7 INDX=$C3 INDY=$D3 ABS=$CF ABSX=$DF ABSY=$DB | `{adr} := {adr}-1; compare A with {adr}` (DEC + CMP) |
| ISC | ZP=$E7 ZPX=$F7 INDX=$E3 INDY=$F3 ABS=$EF ABSX=$FF ABSY=$FB | `{adr} := {adr}+1; A := A - {adr}` (INC + SBC); also written `ISB` elsewhere |
| ANC | IMM=$0B | `A := A and #imm`, then bit 7 of the result is also copied into carry, as if an ASL/ROL had run |
| ALR | IMM=$4B | `A := (A and #imm) / 2` (AND + LSR); also written `ASR` elsewhere |
| ARR | IMM=$6B | `A := (A and #imm) / 2`, with carry/overflow set from an internal ADC-like mechanism — see the oxyron.de table for the exact flag behavior |
| XAA | IMM=$8B\*\* | `A := X and #imm`; also written `ANE` elsewhere |
| AXS | IMM=$CB | `X := (A and X) - #imm`, flags set like a compare, not like SBC; also written `SBX` elsewhere |
| USBC | IMM=$EB | Functional duplicate of `SBC #imm` ($E9) — same operation, different opcode byte |
| AHX | INDY=$93 ABSY=$9F\*\* | `{adr} := A and X and (high byte of address + 1)`; also written `SHA` elsewhere |
| SHY | ABSX=$9C\* | `{adr} := Y and (high byte of address + 1)`; also written `SYA` elsewhere |
| SHX | ABSY=$9E\* | `{adr} := X and (high byte of address + 1)` |
| TAS | ABSY=$9B\* | `S := A and X; {adr} := S and (high byte of address + 1)`; also written `SHS` elsewhere |
| LAS | ABSY=$BB | `A, X, S := {adr} and S` |
| KIL | IMP=$02 | Halts the CPU until reset. Also written `JAM`, `HLT`, or `STP` elsewhere. 11 other opcode bytes ($12,$22,$32,$42,$52,$62,$72,$92,$B2,$D2,$F2) do the same thing — `c64asm` only ever assembles `$02` for `KIL`, since all 12 are functionally identical |
| NOP | (in addition to the documented IMP=$EA) IMM=$80 ZP=$04 ZPX=$14 ABS=$0C ABSX=$1C | Fetches the operand, if any, and discards it — otherwise a true no-op, same as documented `NOP` |

\* unstable: generally reliable on real NMOS hardware, but with known
edge cases (e.g. page-boundary-crossing behavior that doesn't match
the pattern of the equivalent documented instructions).

\*\* highly unstable: results can depend on analog effects of the
specific chip, its temperature, and even the exact data present on the
bus at that moment — **do not use these in code meant to run reliably
on real hardware**, even though `c64asm` will assemble them once
`.cpu 6510x` is enabled. `XAA`/`ANE` in particular is singled out by
essentially every reference on this topic as unpredictable enough to
avoid entirely except for deliberate experimentation.

---

## 14. Output format

The `.prg` file written is:

```
byte 0-1:  load address, little-endian
byte 2..:  assembled machine code, starting at the load address
```

The load address is whichever address was current at the **first** `.org`
(or `*=`) directive encountered, or `$0801` if the source never sets one
explicitly (the address BASIC programs conventionally load at). If your
source starts with `.basic`, the load address is `$0801` and the BASIC
stub is the first thing written.

---

## 15. Listing file format

When `--listing` is given, the assembler writes a text file with one line
per assembled instruction or data-emitting directive:

```
<address>  <bytes>    <original source line>
```

followed by a `Symbol table:` section listing every defined symbol and its
final value, sorted alphabetically:

```
080D  A2 00             ldx #$00
080F  BD 30 08          lda message,x
...

Symbol table:
  BLACK                = $0000
  BORDER               = $D020
  CHROUT               = $FFD2
  message              = $0830
  start                = $080D
```

Directives that don't correspond to a single machine instruction (e.g.
`.byte`, `.word`, `.fill`) are not individually itemized in the listing;
only real 6502 instructions appear there.

---

## 16. VICE label export

`--vice-labels <file>` writes a file of `add_label` commands — one per
defined symbol, the same symbols `--listing`'s own "Symbol table:"
section shows, just reformatted for VICE:

```
; c64asm VICE label export  (origin $0801, 60 bytes)
; load in the VICE monitor with: ll "hello.vice"
add_label $D020 .BORDER
add_label $FFD2 .CHROUT
add_label $0830 .message
add_label $080D .start
```

Load it in the VICE monitor with `ll` (or the full name, `load_labels`):

```
(C:$0801) ll "hello.vice"
```

Once loaded, labels work anywhere the monitor accepts an address —
breakpoints, memory dumps, disassembly — so debugging can use names
instead of bare hex addresses:

```
(C:$0801) break .main_loop
(C:$0801) d .move_left .move_left+10
```

Disassembly also shows label names in place of addresses it recognizes,
including as the target of branches and jumps, which is often the more
immediately useful effect day to day — `jmp .main_loop` reads a lot
faster than `jmp $0a60`.

Every symbol gets exported, including plain numeric constants that
aren't really addresses at all (a bitmask, a loop count, a compile-time
bound like `XMIN = 24`) — this assembler's symbol table doesn't
distinguish "this is a real code/data address" from "this just happens
to be a small number," so neither does this export. VICE doesn't mind
either way — a label is just an optional name-to-address annotation,
not something that changes behavior — and other assemblers targeting
VICE (cc65, for one) export labels the same way for the same reason.

---

## 17. Error messages

Each error is printed in the form:

```
Assembly error: <message> (line <N>: <source text>)
```

If `.include` (§10) has been used anywhere in the program, every error
message additionally names the specific file `<N>` refers to — including
errors in the top-level file itself, once *any* `.include` has been
used anywhere:

```
Assembly error: <message> (<filename>, line <N>: <source text>)
```

A program that never uses `.include` sees no change here at all: its
error messages are identical to what they'd have been before `.include`
existed.

### Multiple errors per run

Most kinds of mistake — an undefined symbol, a malformed expression, an
addressing mode a mnemonic doesn't support, a branch out of range, a
redefined symbol, an unrecognized mnemonic or directive, a source-
author-placed `.error` (§11) — don't stop assembly immediately.
Instead, the assembler keeps going, collects up to 20 independent
problems, and reports all of them together:

```
Assembly error: Undefined symbol in operand 'undefined1' (line 2: lda undefined1)
Assembly error: Undefined symbol in operand 'undefined2' (line 3: sta undefined2)
Assembly error: Invalid addressing mode for TXA (line 5: txa #$05)
3 errors.
```

If more than 20 problems are found, the assembler stops collecting and
prints a summary line instead of continuing indefinitely:

```
... and 4 more errors (stopping after 20)
24 errors.
```

No `.prg` or listing file is written once *any* error has been
recorded, from either pass.

A small category of problems — a missing `.include`d file, a circular
or too-deeply-nested `.include` chain, a malformed `.macro`/`.endmacro`
or conditional-assembly (`.if`/`.elif`/`.else`/`.endif`) block, or a
forward reference inside a `.if`/`.elif` condition — still stops
assembly immediately with a single message, the same way every error
did before this feature existed. These are whole-file *structural*
problems: once one of them is true, the shape of the rest of the source
file is ambiguous enough that there's no reasonable way to keep
parsing it, so collecting further "errors" downstream of it wouldn't
be meaningful.

There's one trade-off worth knowing about: later error messages'
line numbers and text are always exactly correct, but if an earlier
mistake meant a value or an addressing-mode decision came out
different from what the source actually implies, a handful of the
messages after it may be downstream noise from that first real
mistake rather than independent problems of their own. If the error
list looks unexpectedly long or repetitive, fix the first one or two
and reassemble — the rest often disappear.

Common errors:

| Message | Cause |
|---|---|
| `Unknown mnemonic or directive '...'` | A token that isn't a recognized instruction, directive, or valid label position. |
| `Symbol '...' already defined` | A label was defined twice with two different address values. |
| `Undefined symbol in operand '...'` | A symbol used in an instruction operand was never defined anywhere in the file. |
| `Undefined symbol in .org expression` | Same, but for `.org`/`*=`. |
| `Undefined symbol in .byte '...'` | Same, but for a numeric `.byte`/`.db` argument. |
| `Undefined symbol in .word '...'` | Same, but for a `.word`/`.dw` argument. |
| `Undefined symbol in .text '...'` | Same, but for a numeric `.text`/`.asc` argument (§6). |
| `<MNEMONIC> requires an operand` | An instruction with no implied-mode encoding was given no operand. |
| `<MNEMONIC> does not support that addressing mode` | The operand's syntax implies a mode the instruction doesn't have (e.g. immediate mode on `STA`). |
| `Branch target out of range (+N)` | A branch instruction's target is more than 127 bytes forward or 128 bytes backward from the instruction following it. Reorganize the code, or replace the branch with an unconditional `JMP` reached via an inverted branch. |
| `Missing ')' in expression '...'` | Unbalanced parentheses in an expression. |
| `Cannot open included file '...'` | A `.include`'d path doesn't resolve to a readable, regular file. If `--lib-dir` (§1) was given, the message names both paths tried. |
| `circular .include detected: a.asm -> b.inc -> a.asm` | An `.include` chain loops back on itself; the message shows the full chain. |
| `.include nested too deeply (max 16)` | More than 16 levels of nested `.include` — almost always a sign of an undetected circular include. |
| `Bad character '...' in expression '...'` | A character the tokenizer doesn't recognize appeared in an expression. |
| `Undefined symbol in .if/.elif expression` | A `.if`/`.elif` condition referenced a symbol not yet defined at that point in the file — forward references aren't allowed here (see §11). |
| `'.elif'/'.else'/'.endif' with no matching '.if'` | One of these appeared without an open `.if`/`.ifdef`/`.ifndef` block. |
| `'.elif' is not allowed after '.ifdef'/'.ifndef'` | `.elif` only works in an `.if` chain; use `.else` after `.ifdef`/`.ifndef` instead. |
| `unclosed '.if'/'.ifdef'/'.ifndef' at end of file` | A conditional block's `.endif` is missing; the message names the line the block started on. |
| `conditional nesting too deep (max 16)` | More than 16 levels of nested `.if`/`.ifdef`/`.ifndef`. |
| `Undefined symbol in .basic start operand '...'` | `.basic`'s optional start-label operand (§7) referenced a symbol never defined anywhere in the file. |
| `Illegal/undocumented opcode '...' used without '.cpu 6510x'` | An illegal/undocumented opcode (§13) was used before `.cpu 6510x` enabled them. |
| `Unknown .cpu mode '...'` | `.cpu`'s operand (§13) was something other than `6510`, `6510x`, or `6502`. |
| `Unknown .charset mode '...'` | `.charset`'s operand (§6) was something other than `upper` or `lower`. |
| `.error requires a quoted message string, e.g. .error "message"` | `.error`'s (or `.warning`'s) operand (§11) wasn't a properly quoted string. |

---

## 18. Known limitations

- **Zero-page sizing of forward-referenced labels.** The assembler picks
  zero-page vs. absolute addressing based on whether an operand's value is
  known to fit in a byte *at the point it's evaluated*. For a label used
  before it's defined later in the same file, this is only settled during
  the second pass — by which point the label's real address is known, but
  the *first* pass (which fixes every instruction's size and thus every
  other label's address) already assumed absolute addressing for that
  reference. In practice this only bites if a forward-referenced **code**
  label resolves to an address under 256 — vanishingly rare, since code
  almost always starts above `$0801`. Zero-page constants (typically
  addresses under `$100`, e.g. pointers in `$FB`–`$FE`) are conventionally
  defined with `=` *before* they're used, which sidesteps the issue
  entirely, and is good practice regardless.
- **Macros (§8), local labels (§9), includes (§10), and conditional
  assembly (§11) exist but are intentionally simple:** macros must be
  defined before use; `.if`/`.elif` conditions can't reference a
  forward-declared symbol (§11); and `.if` can gate instructions and
  data but not which `.macro` gets defined or which file gets
  `.include`d, since those are resolved before `.if` is evaluated
  (§11). `.include` doesn't need manual include guards even without
  conditional assembly backing them — see §10.
- **Illegal/undocumented opcodes (§13) are opt-in, and a few are
  unstable even when enabled.** They're refused by default (an assembly
  error, same as any other unrecognized mnemonic) until `.cpu 6510x`
  turns them on; a handful of them are noted in §13's table as
  unstable or highly unstable even on real NMOS hardware, and none of
  them exist at all on non-NMOS 6502 variants.
- **Multi-error reporting has a noise trade-off.** A single run can
  surface several independent mistakes (see §17), but messages after
  the first one can occasionally be downstream noise rather than
  genuinely separate problems — see the note at the end of §17.
- **`.charset lower` (§6) only controls assembled bytes, not the C64's
  actual runtime character set** — that's separate hardware state the
  assembler has no way to touch, so `.charset lower` text needs to be
  paired with an explicit runtime switch (`lib/text.inc`'s
  `SET_LOWERCASE_CHARSET`, or an equivalent manual `CHROUT`) or it
  will still display in uppercase. See §6's mixing caveat too:
  `.charset upper` text is only reliably uppercase before that switch
  happens.

---

## 19. Complete example

```asm
; hello.asm - prints a message and cycles the border color
CHROUT  = $FFD2
BORDER  = $D020
BLACK   = $00

        .basic

start:
        ldx #$00
print_loop:
        lda message,x
        beq done_print
        jsr CHROUT
        inx
        jmp print_loop
done_print:
        lda #BLACK
        sta BORDER

color_loop:
        inc BORDER
        ldx #$00
delay_outer:
        ldy #$00
delay_inner:
        iny
        bne delay_inner
        dex
        bne delay_outer
        jmp color_loop

message:
        .text "HELLO, C64!"
        .byte $0d, $00
```

```
$ python3 c64asm.py hello.asm -o hello.prg --listing hello.lst
Assembled 60 bytes, origin=$0801 -> hello.prg
Listing written to hello.lst
```

Load `hello.prg` in VICE (File → Autostart, or drag the file onto the
emulator window) or on real hardware to run it.
