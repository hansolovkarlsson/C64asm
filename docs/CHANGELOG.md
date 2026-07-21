# Changelog

Notable changes to c64asm, newest first. This project doesn't use
version numbers or dated releases, so entries are grouped by feature
rather than by version — each one names what changed and points at
where it's documented in full (`c64asm-reference.md`, `README.md`, or
`lib-reference.md`).

Every entry below shipped identically across all three implementations
(the Python reference `c64asm.py`, the single-file C `c64asm.c`, and
the 14-module split-source C in `c64asm-split-src.zip`) and passed
this project's full regression suite before being marked done — that
discipline is this project's own standing practice, not something
worth repeating in every entry below.

## Cycle counts in `--listing`

Every assembled instruction in the listing file now shows how many
cycles it takes, cross-checked entry-by-entry against 6502.org's
published NMOS 6502/6510 timing table rather than reconstructed from
memory, given how easy this is to get subtly wrong. Handles the two
well-known variable cases explicitly instead of guessing: `4/5` for a
*read* instruction (`lda`, `cmp`, and similar) using indexed
addressing that might cross a page boundary at runtime, and `2/3/4`
for a conditional branch (not taken / taken / taken across a page).
Correctly excludes the page-crossing bonus for writes (`sta`) and
read-modify-write instructions (`inc`, `asl`, and similar) using
indexed addressing, which always take a fixed number of cycles
regardless — confirmed directly in test output that `lda $1000,x`
shows `4/5` while `sta $2000,x` shows a plain `5`, not `4/5`, despite
using the same addressing mode. Illegal/undocumented opcodes are
deliberately left blank rather than guessed. Along the way, found and
fixed a small pre-existing inconsistency between the Python and C
implementations' listing output (C wasn't trimming all trailing
whitespace from the source column, only newlines) that had never
surfaced before since nothing previously compared listing file
*content* between implementations directly. See `c64asm-reference.md`
§19.

## `adventure.asm`: each `room_exits` row individually `.tag`'d

`.tag` checks one struct instance, not a whole array — but nothing
stops tagging each *element* of an array on its own, which turned out
to be a real, meaningful check `.struct` alone never provided:
`.struct` guarantees `Exits` itself is 4 fields, but says nothing
about whether any particular row in `room_exits` actually has 4
values written out. Before this, a mistyped row (three values instead
of four) assembled cleanly with no warning at all, silently shifting
every room after it by one byte — confirmed by deliberately
introducing exactly that mistake and watching it assemble without
complaint. Tagging each of the five rows individually closes that gap
— the same mistake now fails immediately, naming which row and by how
much it's off — while assembling to *exactly* the same bytes as
before, confirmed byte-for-byte identical to the prior shipped
`adventure.prg`, and re-verified against the full game-playing test
suite including the win-condition playthrough. See
`c64asm-reference.md` §12, "Tagging each element of an array".

## `.tag`/`.endtag`

Binds a data block to a `.struct`, automatically checking at
`.endtag` that the block is really that struct's size:

```asm
room_data: .tag Room
        .word room0_desc
        .byte FOREST, $ff, COTTAGE, $ff
.endtag
```

Replaces the manual pattern of an `_end` label plus `.assert
end_label - start_label == Name.size` with something automatic. Pure
observation, not transformation — unlike `.repeat`/`.struct`, `.tag`
doesn't capture or reshape the lines between `.tag` and `.endtag` at
all, it just records `pc` at each end and compares the difference, so
whatever's actually in between (`.byte`, `.word`, `.text`, `.incbin`,
ordinary instructions, anything) assembles completely normally on its
own terms. Recoverable, not fatal, the same as `.assert` — a wrong
`.tag` doesn't corrupt anything about the rest of the file the way a
malformed `.repeat`/`.struct`/`.macro` would, so there's no reason to
stop assembly over it. Checks a single struct instance's size only,
not an array of them — `adventure.asm`'s array-of-records `room_exits`
table still uses the `.assert`-based pattern for that case, documented
alongside `.tag` itself. See `c64asm-reference.md` §12.

## `lib/math.inc`: `DIV_2`/`DIV_4`/`DIV_8`/`DIV_16`

Truncating, unsigned division of the A register by a small power of
two, via right shifts (`LSR`) — the mirror of `MULT_N` below, for the
reverse operation: recovering a record index from a byte offset. See
`c64asm-reference.md` §10, "Indexing an array of records".

## `lib/math.inc`: `MULT_2`/`MULT_4`/`MULT_8`/`MULT_16`

Multiply the A register by a small power of two via left shifts
(`ASL`), since the 6502 has no multiply instruction — mainly for
indexing into an array of `.struct` records. `adventure.asm`'s
room-navigation code now uses `MULT_4` in place of two hand-written
`asl a` lines. See `c64asm-reference.md` §10.

## `--warn-unused-all`, and `--warn-unused` scoped to the main file

`--warn-unused` now only reports symbols defined in the file named on
the command line, not anything it `.include`s, with a one-line count
of how many more were suppressed. `--warn-unused-all` restores the
original, unscoped behavior for anyone who wants the full picture. The
scoping exists because the original, unscoped version reported so much
routine library noise — 184 warnings against `demo.asm`, almost
entirely unused `keyboard.inc` constants for keys that particular demo
never checks — that it was hard to use in practice on any program
built on the standard library. See `c64asm-reference.md` §21.

## `--warn-unused`

Warns, after assembly finishes, about every label or `=`/`.equ`
constant (including `.struct` fields) defined but never referenced
anywhere in the program. Opt-in, never fails the build. See
`c64asm-reference.md` §21.

## `.assert`, and `==`/`!=` expression operators

`.assert condition[, "message"]` fails assembly (recoverably, the same
as `.error`) if `condition` evaluates to false — a compile-time
sanity check, most usefully paired with a `.struct`'s `Name.size`
(catching a struct that gained or lost a field out from under code
that assumed a specific size, say). Needed a real equality operator to
be useful for that, so `==` and `!=` were added to the expression
grammar at the same time — deliberately not `<`/`>`/`<=`/`>=` as
binary comparisons, since `<`/`>` are already unary low/high-byte
operators here, and overloading them for both meanings would be
genuinely ambiguous to parse. `adventure.asm`'s
`compute_room_exits_offset` now carries an `.assert` guarding its
hand-rolled multiply against exactly that kind of drift. See
`c64asm-reference.md` §11 and §4.

## `sprites.asm`: a real demo using `.incbin`

A 4-frame sprite animation loaded from `star_anim.bin`, an external
binary asset, via `.incbin`'s offset/length slicing — instead of the
hand-transcribed `.byte` sprite data every other demo uses. Same
W/A/S/D movement and border stop as `demo.asm`'s own star, plus
continuous frame-cycling independent of movement. See `README.md`'s
file table.

## `.struct`

Named byte offsets into a data record (`Room.north` instead of a bare
offset number) — `.byte`/`.word`/`.res` field declarations inside a
`.struct`/`.endstruct` block, producing `Name.field` symbols plus an
automatic `Name.size`. `adventure.asm`'s `room_exits` table was
converted from four separate parallel arrays
(`exit_north`/`exit_south`/`exit_east`/`exit_west`, one per direction)
into a single `.struct`-based combined table, indexed per room.
Required widening identifier parsing to allow a dot mid-symbol-name,
in exactly the two places that actually needed it, confirmed not to
change behavior for any previously-valid program (a dotted identifier
was always a hard parse error before). See `c64asm-reference.md` §10.

## `.incbin`

Imports raw bytes from an external binary file directly into the
assembled output, with optional `offset`/`length` arguments to slice
out part of a larger file — for sprite/font/music data made in an
external tool, instead of hand-transcribing it as `.byte` lists.
Reuses `.include`'s own path resolution rules (relative to the
including file first, `--lib-dir` as a fallback). Every error
`.incbin` can produce is fatal, not recoverable, deliberately unlike
`.byte`'s undefined-symbol handling: an `.incbin` problem means the
assembler doesn't know the emitted byte *count*, which would corrupt
every address computed after it if assembly tried to continue. See
`c64asm-reference.md` §14.

## `.repeat`/`.dup`

Assembles a block of code `count` times at assembly time, with an
optional index available inside the block via the same `\param`-style
substitution macro parameters already use. `.dup`/`.enddup` are exact
synonyms for `.repeat`/`.endrepeat`. Implemented as an anonymous macro
immediately invoked `count` times in a row, reusing the macro system's
own per-invocation local-label scoping rather than building a parallel
mechanism. `count` must be a plain integer literal, not a symbol or
expression, since `.repeat` is expanded during the same preprocessing
pass as `.macro`/`.include`, entirely before pass 1 builds a symbol
table. See `c64asm-reference.md` §9.

## `.error`/`.warning`

`.error "message"` records a recoverable error with a custom message;
`.warning "message"` prints a message without affecting whether
assembly succeeds. Typically paired with `.ifdef`/`.ifndef` to check a
precondition — every file in the standard library now checks its own
required zero-page symbols this way, right at the top, turning a
confusing `Undefined symbol` error surfacing three files deep into one
clear message naming exactly what's missing. Building this out
surfaced a real, pre-existing bug: an earlier `demo.asm` checked for a
"Y (restart)" keypress using a keyboard-matrix value that was actually
the **5** key's position, not Y's — found while cross-checking against
`keyboard.inc`'s new named key constants, which made the mismatch
visible for the first time; fixed in both `demo.asm` and its test.
See `c64asm-reference.md` §15.

## `--vice-labels`

Writes a VICE monitor label file (one `add_label` command per symbol),
for debugging by name in the VICE monitor — `break .main_loop` instead
of `break $0a60`, and disassembly showing names instead of bare
addresses. See `c64asm-reference.md` §20.

## `keyboard.inc`

Named `KEY_<name>_COL`/`KEY_<name>_ROW`/`KEY_<name>_CODE` constants for
every key on the C64 keyboard matrix, verified against the standard
published matrix reference, plus `wait_any_key` for a blocking "wait
for any key, return which one" read. Writing this out surfaced the
`demo.asm` Y-key bug described above.

## `demo.asm`: WASD movement, Q to exit

The star sprite now moves with W/A/S/D, stopping at the screen edges,
with a short sound on each successful move; the exit key changed from
Y to Q, with updated on-screen instructions.

## `demo.asm`: a visible sprite

The demo's sprite data was previously all zero bytes — invisible, even
though sprite hardware setup was otherwise correct. Replaced with a
small hand-drawn star.

## `demo.asm`: wait for a keypress before switching to bitmap mode

Previously, `demo.asm` switched to bitmap mode immediately after
printing its welcome text, wiping the screen before there was any real
chance to read it. Now waits for a keypress first, with on-screen
instructions explaining the controls.

## `.charset upper`/`.charset lower`

Controls how `.text`/`.asc`/`.byte` string literals encode letters:
forced-uppercase (`upper`, the default, matching every C64 program's
default display charset) or case-preserving (`lower`, once a program
has switched to the C64's alternate character set at runtime).
`adventure.asm`'s room descriptions were converted to natural mixed
case using this. See `c64asm-reference.md` §6.

## `.text`/`.byte` mixed strings and numeric bytes

`.text`/`.asc` (and quoted-string arguments to `.byte`) accept
comma-separated numeric bytes alongside quoted strings on the same
line — `.text "HELLO", 13, 0` instead of a separate `.text "HELLO"`
followed by `.byte 13, 0`. See `c64asm-reference.md` §7.

## Illegal/undocumented 6502/6510 opcode support

`.cpu 6510x` enables the well-known undocumented NMOS 6502/6510
opcodes (`LAX`, `SAX`, `DCP`, and others); `.cpu 6510` (the default)
or `.cpu 6502` turns support back off. Off by default, since these
aren't part of the documented instruction set. See
`c64asm-reference.md` §17 and `c64asm-opcode-reference.md`.

## Multi-error reporting

Most kinds of mistake — an undefined symbol, a malformed expression,
an unsupported addressing mode, a branch out of range, a redefined
symbol — no longer stop assembly at the first one found. Independent
problems are collected (up to 20 per run) and reported together in one
pass, closer to how a modern compiler behaves. A smaller category of
whole-file structural problems (a missing `.include`d file, a
malformed `.macro`, and similar) still stops assembly immediately,
since the shape of the rest of the file becomes genuinely ambiguous
once one of those is true. See `c64asm-reference.md` §22.

## Foundational feature set

Everything else the assembler could already do by the time the entries
above began: the full NMOS 6502/6510 instruction set (all 56
documented mnemonics, every addressing mode), automatic zero-page vs.
absolute addressing selection, two-pass assembly with forward
references, a real expression evaluator (`+ - * /`, parentheses, unary
`<`/`>` for low/high byte, `$hex`/`%binary`/decimal/`'char'` literals,
`*` for the current program counter), macros (`.macro`/`.endmacro`)
with named parameter substitution and recursive invocation, local
labels (`@label`) automatically scoped per macro invocation, `.include`
with automatic include-once semantics and circular-include detection,
conditional assembly (`.if`/`.elif`/`.else`/`.endif`,
`.ifdef`/`.ifndef`), the `.prg` output format, `--listing` output, and
the original standard library files (`hardware.inc`, `text.inc`,
`input.inc`, `graphics.inc`, `sound.inc`) extracted from and
cross-checked against this project's own demo programs
(`hello.asm`, `bounce.asm`, `pong.asm`, `adventure.asm`, `lander.asm`).
See `c64asm-reference.md` and `README.md` for the full, current
picture of all of it.
