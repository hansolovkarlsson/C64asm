# c64asm — multi-file source, with a guide

This is the same c64asm assembler as `c64asm.c` — same syntax, same
opcode table, same output for any given input — split into one file per
concern, with substantially more explanatory commentary than the
original single-file version. It exists for people who want to *read*
an assembler's source and understand how one is actually built, not
just use it.

**This is not a different assembler.** Every `.asm` file that worked
with `c64asm.c` produces byte-identical `.prg` and listing output here.
Every module was cross-checked against the original single-file
`c64asm.c` (and, separately, against the Python implementation) across
all six of this project's demo programs — `hello.asm`, `edge.asm`,
`bounce.asm`, `pong.asm`, `adventure.asm`, and `lander.asm` — plus both
of the assembler's own error-handling paths (an out-of-range branch, and
a backward `.org`), with zero differences in output or behavior. Macro
support (`macro.h`/`.c`, added after the initial split) was
cross-checked the same three-way way, against its own set of test
programs covering parameterized macros, zero-parameter macros, nested
macro invocations, and every documented error case (wrong argument
count, an unknown `\param` reference, a macro name colliding with a real
mnemonic, and runaway recursive expansion). Local labels (`locallabels.h`/`.c`,
added right after macros) were cross-checked the same way, against tests
covering same-macro-body labels invoked repeatedly with no collision,
ordinary (non-macro) code reusing local names across separate scopes, a
deliberate out-of-scope reference correctly becoming an "undefined
symbol" error, `@` inside a quoted string being left untouched, and
nested macro invocations each getting their own scope. `.include`
support (`includes.h`/`.c`, added after local labels) was cross-checked
the same three-way way too, against tests covering basic splicing,
nested includes resolving relative to their own directory (not the
top-level file's), a diamond dependency where two files both include a
shared library file with no duplicate-definition error, a macro
containing a local label defined inside an included file, and every
documented error case (a missing included file, a circular include
chain reported with its full path, and the nesting-depth backstop).
Filename-aware error messages were specifically checked both ways: that
a program using `.include` gets correct file-and-line attribution for
errors in *any* of its files (including the top-level one, once
`.include` is used anywhere), and that a program never using `.include`
produces byte-for-byte identical error text to before -- no filename
shown at all. Along the way, this cross-checking also caught two
genuine pre-existing bugs unrelated to `.include` itself: a missing
top-level input file produced a raw, uncaught exception in the Python
implementation instead of a clean error message (the C implementations
already handled this correctly), and macro body lines were being
stored with their original indentation stripped in the C
implementations but preserved in Python, which showed up as a listing-
file formatting difference for any macro invocation. Both are fixed.
Conditional assembly (`.if`/`.elif`/`.else`/`.endif`,
`.ifdef`/`.ifndef`, added to `assembler.c`/`symtab.c` after `.include`)
was cross-checked the same way, against tests covering the motivating
PAL/NTSC-style use case, `.elif` chains, nested conditionals, `.ifdef`/
`.ifndef`, a conditional wrapping instructions inside a macro body
invoked more than once, and every documented error case (a forward
reference in `.if` — a hard error on both passes, not deferred the way
other expressions' undefined-symbol checks are — mismatched `.elif`/
`.else`/`.endif`, `.elif` after `.ifdef`, and an unclosed `.if` at end
of file). One test specifically exercises the reason this needed real
design work rather than a direct port of `.ifdef` from macros: a symbol
referenced by `.ifdef` before its own definition line, which a naive
"is this symbol in the table right now" check would answer
inconsistently between pass 1 and pass 2 (the symbol table persists
across both passes), corrupting every address computed afterward.
`find_symbol_defined_before()` (`symtab.c`) is what makes the two
passes agree, and the test confirms they do. It
also runs clean under AddressSanitizer and UndefinedBehaviorSanitizer.

## Building

```
make
./c64asm input.asm -o output.prg [--listing out.lst]
```

Or by hand, without the Makefile:

```
cc -std=c99 -O2 -o c64asm src/*.c
```

## Where to start reading

Start with `src/main.c`. It's deliberately short — just command-line
handling and a roadmap of which module does what, called in the order
the assembler actually needs them. Once you've read that, the natural
reading order (which is also, not coincidentally, roughly the order
data flows through the program) is:

1. **`opcodes.h`/`.c`** — the 6502 instruction set: which mnemonics
   exist, which addressing modes each one supports, and what single
   byte each (mnemonic, mode) pair assembles to. This is closer to a
   hardware reference table than "code" in the usual sense.

2. **`fileio.h`/`.c`** — loads the top-level source file. Since
   `.include` support was added, this is now a thin wrapper: it
   allocates `g_lines` and hands off to `includes_process_file()`
   (`includes.c`) for the actual reading.

3. **`includes.h`/`.c`** — resolves and reads a file's lines, for both
   the top-level file (called from `fileio.c`) and every `.include`
   (called from `macro.c`). Doesn't know anything about macros, local
   labels, or assembly -- it takes a plain callback function to hand
   each line to, which is what lets `fileio.c` and `macro.c` both
   reuse it without this module needing to depend on either of theirs.
   The interesting parts: paths resolve relative to the *including*
   file's own directory (not the current working directory), which is
   what makes a library file able to `.include` another file sitting
   next to it regardless of where the assembler was invoked from; and
   `.include` is automatically include-once (like C's `#pragma once`,
   not like a raw `#include` needing manual guards) via a canonical-
   path comparison, so two library files sharing a common dependency
   don't collide with duplicate-definition errors.

4. **`macro.h`/`.c`** — expands `.macro`/`.endmacro` definitions and
   invocations, and recognizes `.include` lines (delegating the actual
   file-reading to `includes.c`). This sits directly between reading a
   raw line and `lineparser.c` (parsing a real one): every raw line
   passes through `macro_process_line()` first, which either absorbs it
   into an in-progress macro definition, splices in another file's
   lines (recursively, for `.include`), expands it (recursively, if it
   invokes a macro) into one or more *new* raw lines and feeds each of
   those back through itself, or -- for the ordinary case, a line that
   isn't part of any macro or include at all -- hands it straight to
   `split_line()`. Worth reading once you're comfortable with
   `lineparser.c`, since it leans on the same "what's the first token
   on this line" style of thinking, just one layer earlier and working
   on raw text instead of an already-recognized mnemonic.

5. **`locallabels.h`/`.c`** — `@`-prefixed local labels. Called from
   `macro.c`, this rewrites an `@name` to a scope-specific global name
   (e.g. `@loop` becomes `__local5_loop`) before `lineparser.c` ever
   sees it -- which is why nothing *else* in this codebase, not even
   `symtab.c`, knows local labels exist at all; the whole feature lives
   in this one small module's text rewriting. The interesting design
   idea here: a new scope begins both on an ordinary global label *and*
   on each macro expansion, which is what finally lets a macro's
   internal labels stay distinct across repeated invocations without
   the caller doing anything special -- read this right after
   `macro.c`, since the two are tightly coupled (macro.c calls
   `locallabels_push_scope()`/`locallabels_pop_scope()` around a body
   expansion).

6. **`lineparser.h`/`.c`** — the interesting one to start with if
   you've never looked at how an assembler's *line* grammar works.
   Splits one raw line of text into an optional label, an optional
   mnemonic-or-directive, and an operand string. This is where
   `"loop: lda #$00 ; comment"` becomes three separate, clean pieces.

7. **`expr.h`/`.c`** — a small hand-written recursive-descent parser
   for expressions like `SCREEN + 40*ROW`. If you've never written or
   read a recursive-descent parser before, this is a good one to learn
   from precisely *because* it's small: the whole grammar is four
   functions (`parse_expr`, `parse_term`, `parse_unary`, `parse_atom`),
   each one directly encoding one level of operator precedence, calling
   the next one down the list. The comments in this file walk through
   exactly how that works, and how it happens to be exactly why an
   earlier version of this project had `A*B` silently fail to multiply
   for a long time before anyone noticed — a real bug, and a good
   illustration of a subtle parsing mistake.

8. **`operand.h`/`.c`** — given an instruction's operand text, works
   out which of the 6502's 13 addressing modes it's using, purely from
   its punctuation (`#`, parentheses, a trailing `,X`). This is the
   piece that lets `LDA $10` and `LDA $1000` pick different encodings
   automatically.

9. **`symtab.h`/`.c`** — the symbol table: every label and named
   constant, and what it's bound to. Small and simple by design (see
   the comments in `symtab.c` on why a linear scan, not a hash table,
   is the right call at this codebase's scale). Also home to
   `find_symbol_defined_before()`, added for conditional assembly
   (`assembler.c`) -- a second, more careful way to ask "is this symbol
   defined" that accounts for the symbol table persisting across both
   assembly passes.

10. **`assembler.h`/`.c`** — the heart of the program: the two-pass loop
   that ties everything above together into actual machine code.
   **Read the big comment block in `assembler.h` first** — it explains
   *why* an assembler needs two passes at all (in one sentence: so a
   label can be used before it's defined), which is the single most
   important idea in the whole codebase once you're past the syntax
   details. The same comment block also covers conditional assembly
   (`.if`/`.elif`/`.else`/`.endif`, `.ifdef`/`.ifndef`) -- worth reading
   closely once you're comfortable with the two-pass idea itself, since
   it's a genuinely subtle case of that same idea: `.ifdef` can't just
   check "is this symbol defined right now", because the symbol table
   persists across both passes and would give a different answer on
   each one for the exact same line. `symtab.c`'s
   `find_symbol_defined_before()` is the fix, and is worth reading
   alongside this.

11. **`bytebuf.h`/`.c`**, **`basicstub.h`/`.c`**, **`listing.h`/`.c`** —
   three small, self-contained utilities `assembler.c` and `main.c`
   lean on: a growable output buffer, the tiny "10 SYS xxxx" BASIC
   loader the `.basic` directive emits, and the optional listing file.
   Each is short enough to read in a couple of minutes and has its
   design rationale explained in its header comment.

12. **`error.h`/`.c`** and **`strutils.h`/`.c`** — small utilities used
    everywhere else. Worth a quick look, not worth dwelling on.

## Module map

| File | What it answers |
|---|---|
| `common.h` | shared size limits (why fixed-size buffers, not dynamic strings, almost everywhere) |
| `error.h` / `.c` | how does the assembler report a mistake and stop? |
| `strutils.h` / `.c` | generic string helpers (trim, identifier-character tests, ASCII→PETSCII, comment stripping, comma-list splitting) |
| `opcodes.h` / `.c` | what are the 6502's addressing modes, and what does each (mnemonic, mode) pair assemble to? |
| `macro.h` / `.c` | how does `.macro`/`.endmacro` expand into real source lines before parsing ever sees them? |
| `includes.h` / `.c` | how does `.include "path"` find and read a file, and why doesn't including the same library file twice break? |
| `locallabels.h` / `.c` | how does `@loop` become a scope-specific label, and why doesn't it collide across macro invocations? |
| `symtab.h` / `.c` | where are labels and constants stored, and looked up? |
| `expr.h` / `.c` | how does `"SCREEN + 40*ROW"` become a number? |
| `lineparser.h` / `.c` | how does one line of source text become (label, op, operand)? |
| `operand.h` / `.c` | how does the assembler know `LDA $10,X` means zero-page,X and not something else? |
| `bytebuf.h` / `.c` | where do assembled bytes actually accumulate? |
| `basicstub.h` / `.c` | how does `.basic` generate a working `10 SYS xxxx` loader? |
| `listing.h` / `.c` | how is the optional `--listing` file produced? |
| `assembler.h` / `.c` | the two-pass loop — how does everything above combine into a working assembler? |
| `fileio.h` / `.c` | how is the source file read in? |
| `main.c` | command-line handling; the order everything above gets called in |

## A note on style

A few things you'll notice that are deliberate, not oversights:

- **Fixed-size buffers almost everywhere, except the assembled output.**
  `common.h`'s header comment explains the trade-off: fixed buffers
  avoid a large amount of allocation bookkeeping at the cost of hard
  ceilings (e.g. `MAX_IDENT` = 128 characters for any one label name).
  The one place that genuinely can't have a sensible fixed maximum —
  the assembled machine code itself, which could reasonably be
  anywhere from a few bytes to tens of kilobytes — uses a real growable
  buffer instead (`bytebuf.h`/`.c`).

- **A handful of module-level globals** (`g_lines`, `symtab`,
  `g_listing`), rather than passing state through every function call
  or bundling it into a big "assembler context" struct. For a
  single-threaded command-line tool that only ever assembles one file
  per run, this is a reasonable, honest simplification — there's
  exactly one of each of these in the whole program, ever. `expr.c`'s
  `EParser` struct is the one deliberate exception, bundling its
  per-call state together instead of using globals, specifically
  because expression evaluation happens recursively (an expression can
  contain a parenthesized sub-expression, which is itself evaluated by
  calling back into the same functions) and needs to be safely
  re-entrant in a way the rest of the program doesn't.

- **Two-pass, not one-pass-with-backpatching or multi-pass-until-fixed-
  point.** Two passes is the simplest scheme that correctly supports
  forward references, and is what the overwhelming majority of
  assemblers for 8-bit CPUs actually use. `assembler.h`'s header
  comment covers why, and `assembler.c`'s comment on `M_ZP`-vs-`M_ABS`
  selection covers the one real limitation this simplicity costs you
  (documented in more detail in `c64asm-reference.md`'s "known
  limitations" section, alongside every other documented edge case in
  this assembler's behavior).
