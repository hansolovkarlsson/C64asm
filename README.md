# c64asm

A complete two-pass 6502/6510 assembler for the Commodore 64, available
in two interchangeable implementations — Python and portable C99 (as
both a single file and a commented, multi-file split for reading) —
plus a standard library, a from-scratch 6502/C64 emulator used for
automated testing, five demo programs, and a full set of reference
documentation.

All three assembler builds (Python, single-file C, split-source C)
accept identical syntax and are verified to produce **byte-identical**
`.prg` and listing output for the same source file, so you can use
whichever fits your workflow.

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
$ python3 c64asm.py hello.asm -o hello.prg
Assembled 60 bytes, origin=$0801 -> hello.prg
```

Load `hello.prg` into [VICE](https://vice-emu.sourceforge.io/) (File →
Autostart, or just drag the file onto the emulator window) or write it to
a real C64 to run it.

---

## Features

- Full NMOS 6502/6510 instruction set — all 56 documented mnemonics,
  every addressing mode
- Optional support for illegal/undocumented 6502/6510 opcodes (`LAX`,
  `SAX`, `DCP`, and 17 others) via `.cpu 6510x` — off by default, since
  they're not part of the documented instruction set; see
  `c64asm-reference.md` §17
- Two-pass assembly, so forward references to labels just work
- Automatic zero-page vs. absolute addressing selection
- A real expression evaluator: `+ - * /`, `==`/`!=` for equality
  checks, parentheses, `<`/`>` for low/high byte, `$hex`, `%binary`,
  decimal, `'char'` literals, `*` for the current program counter
- **Macros** (`.macro`/`.endmacro`) with named parameter substitution
  and recursive invocation
- **`.repeat`/`.dup`** — assembles a block of code N times at assembly
  time, with an optional index available inside via the same
  `\param`-style substitution macros use; see `c64asm-reference.md` §9
- **`.struct`/`.endstruct`** — named byte offsets into a data table
  (`Room.north` instead of a bare offset number), reusing `.byte`/
  `.word`/`.res` as field declarations; see `c64asm-reference.md` §10
- **`.assert`** — a compile-time sanity check (`.assert Exits.size ==
  4, "..."`) that fails assembly with a clear message if a condition
  built on a struct size, a computed address, or anything else known
  at assembly time turns out false; see `c64asm-reference.md` §11
- **`.tag`/`.endtag`** — binds a data block to a `.struct` and checks
  automatically that it's really that struct's size (`room_data: .tag
  Room` ... `.endtag`), instead of a manual end-label plus `.assert`
  each time; see `c64asm-reference.md` §12
- **Local labels** (`@label`), scoped between global labels and per
  macro expansion, so loop/branch labels inside a subroutine or macro
  never collide with anything else in the file
- **VICE monitor label export** (`--vice-labels`) — debug by name in
  VICE (`break .main_loop` instead of `break $0a60`) instead of bare
  hex addresses; see `c64asm-reference.md` §20
- **Cycle counts in `--listing`** — every assembled instruction is
  annotated with how many cycles it takes, including the well-known
  variable cases (`4/5` for a page-crossable read, `2/3/4` for a
  branch) rather than guessing; see `c64asm-reference.md` §19
- **Unused-symbol warnings** (`--warn-unused`) — flags every label or
  constant defined but never referenced, after assembly finishes.
  Scoped to the main file by default (an `.include`d library's own
  unused symbols are suppressed, with a count of how many — see
  `--warn-unused-all` to see them too); off entirely unless one of
  these flags is given; see `c64asm-reference.md` §21
- **`.error`/`.warning` directives** — paired with `.ifdef`/`.ifndef`,
  turn a missing precondition (a required zero-page symbol, say) into
  one clear message right at the point of the mistake, instead of a
  confusing `Undefined symbol` buried inside a macro or library
  routine several `.include`s away; see `c64asm-reference.md` §15
- **`.include`**, with automatic include-once semantics (no manual
  include guards needed), relative path resolution, and circular-include
  detection
- **`.incbin`** — import a raw binary file's bytes directly (sprite,
  font, or music data made in an external tool), with an optional
  offset/length to pull one asset out of a larger file; see
  `c64asm-reference.md` §14
- **Conditional assembly** (`.if`/`.elif`/`.else`/`.endif`,
  `.ifdef`/`.ifndef`) for things like PAL/NTSC timing variants
- Directives for raw bytes/words, text (with ASCII→PETSCII conversion,
  including a `.charset upper`/`.charset lower` switch for true
  lowercase output), memory fills, byte alignment (`.align`),
  symbol/constant definitions, and a `.basic` directive that
  auto-generates a correct `10 SYS xxxx` BASIC loader stub
- Clear, line-and-filename-aware error messages — undefined symbols,
  out-of-range branches, invalid addressing modes, and more, all caught
  before you waste time in an emulator
- Assembly listings (address / bytes / source, plus a final symbol table)
- Outputs standard C64 `.prg` files: a two-byte load address followed by
  the machine code, ready for any emulator or real hardware

## Quick start

**Python** (no dependencies beyond the standard library):

```
python3 c64asm.py <input.asm> -o <output.prg> [--listing <file.lst>] [--lib-dir <dir>]
```

**C** (portable C99; builds with `clang` on macOS or `gcc`/`clang` on
Linux, using only the standard library):

```
cc -O2 -o c64asm c64asm.c
./c64asm <input.asm> -o <output.prg> [--listing <file.lst>] [--lib-dir <dir>]
```

**Split-source C**, for reading how an assembler like this is actually
built (same syntax, same output — see `ARCHITECTURE.md`):

```
unzip c64asm-split-src.zip && make
./c64asm <input.asm> -o <output.prg> [--listing <file.lst>] [--lib-dir <dir>]
```

## Disassembler

`c64disasm.py` goes the other way — a `.prg` file back into
`c64asm.py`-compatible source:

```
python3 c64disasm.py yourfile.prg -o yourfile.asm [--entry $ADDR]
```

It follows actual code flow from the program's entry point (auto-
detected from a BASIC stub's `SYS` target, or the load address itself,
or an explicit `--entry` you provide) rather than blindly decoding
byte by byte — branches, jumps, and calls get followed recursively,
and anything never reached that way is shown as plain `.byte` data
rather than guessed at as an instruction. This is deliberately a
single Python tool, not matched across three implementations the way
`c64asm.py` itself is — see `c64disasm.py`'s own header comment for
the full reasoning, the algorithm, and its known limitations
(computed/indirect jumps and jump tables chief among them).

Its own correctness test is disassembling and then reassembling every
`.prg` this project ships — all of them, including the illegal-opcode
demo, byte-for-byte identical to the original across all three `c64asm`
builds, not just checked for a plausible-looking result.

## Project structure

| File | What it is |
|---|---|
| `GETTING-STARTED.md` | **Start here** — a short, example-driven walkthrough from nothing to a running C64 program |
| `c64asm.py` | The assembler, Python implementation |
| `c64asm.c` | The assembler, single-file portable C99 implementation |
| `c64asm-split-src.zip` | The same assembler split into one file per concern, heavily commented, with a `Makefile` — for reading, not a different implementation (see `ARCHITECTURE.md`) |
| `c64disasm.py` | **Disassembler** — turns a `.prg` back into `c64asm.py`-compatible source, following actual code flow rather than blind byte-by-byte decoding (see "Disassembler" above) |
| `ARCHITECTURE.md` | Guide to the split-source project's module layout |
| `CHANGELOG.md` | **Project history** — every notable feature and fix, newest first, with pointers into the docs below for the full detail on each |
| `c64asm-reference.md` | **Assembler syntax reference** — labels, expressions, addressing-mode syntax, macros, local labels, `.include`/`.incbin`, conditional assembly, every directive, error messages, VICE label export, CLI usage |
| `c64asm-opcode-reference.md` | **6502 opcode reference** — what every documented instruction does, which status flags it affects, and a worked example of each; a full write-up of all 13 addressing modes; and a section on the illegal/undocumented opcodes, clearly marked as non-standard |
| `c64-memory-reference.md` | **C64 hardware reference** — screen/color RAM, VIC-II graphics modes, sprites, SID sound, joystick input, common KERNAL routines, all with tested example code |
| `c64asm-stdlib.zip` | **Standard library** — `.include`-able text/input/graphics/sound/math routines, shared across the demos (see below) |
| `mini6502.zip` | **mini6502** — a from-scratch 6502/C64 emulator used to test-drive every demo and library routine below (see below) |
| `hello.asm` / `.prg` / `.lst` | Demo: prints text via `CHROUT` and cycles the border color |
| `demo.asm` / `.prg` / `.lst` | Demo: exercises every standard library file together (`lib/text.inc`, `lib/input.inc`, `lib/keyboard.inc`, `lib/graphics.inc`, `lib/sound.inc`) in one small program — a visible sprite, W/A/S/D movement with a border stop and a sound on each move, Q to exit; also this project's own integration test, cross-checked across all three implementations and actually executed, not just assembled (see `lib-reference.md`) |
| `sprites.asm` / `.prg` / `.lst` (+ `star_anim.bin`) | Demo: a 4-frame sprite animation loaded from `star_anim.bin`, an external binary asset, via `.incbin` (`c64asm-reference.md` §14) instead of hand-transcribed `.byte` data; same W/A/S/D movement and border stop as `demo.asm`'s own star, plus continuous frame-cycling independent of movement |
| `music_demo.asm` / `.prg` / `.lst` | Demo: two-voice SID music via `lib/music.inc` — "Twinkle Twinkle Little Star" (public domain) on a sawtooth melody voice, a triangle bass line underneath, border color pulsing on the beat |
| `editor.asm` / `.prg` / `.lst` | Demo: a simple one-screen text editor — direct screen memory writes (with PETSCII-to-screen-code conversion), a reverse-video block cursor, and full-screen cursor movement, all driven by the KERNAL's own keyboard buffer (`GETIN`) rather than `lib/keyboard.inc`'s matrix-scanning `READ_KEY`; the intended foundation for a future load/save/directory-listing follow-up, not a finished editor |
| `bounce.asm` / `.prg` / `.lst` | Demo: a sprite bouncing around a bitmap graphics screen, raster-synced |
| `pong.asm` / `.prg` / `.lst` | Demo: two-paddle Pong — joystick and keyboard-matrix input, ball/paddle collision, AI opponent; uses the standard library (`lib/graphics.inc`, `lib/input.inc`, `lib/sound.inc`) rather than reimplementing raster timing, input handling, and sound |
| `adventure.asm` / `.prg` / `.lst` | Demo: a small text adventure — typed commands via `CHRIN`, a room/item/puzzle state machine, room exits stored in a `.struct`-based table (`room_exits+Exits.north,x` instead of a bare offset number) indexed via `lib/math.inc`'s `MULT_4`, with an `.assert` guarding that choice and each row individually `.tag`'d against `Exits`; uses the standard library (`lib/text.inc`, `lib/input.inc`, `lib/math.inc`) rather than reimplementing string/input handling |
| `lander.asm` / `.prg` / `.lst` | Demo: lunar lander — bitmap graphics, physics, terrain collision, a fuel bar, sound, and an explosion animation; uses the standard library (`lib/graphics.inc`, `lib/input.inc`, `lib/sound.inc`, `lib/text.inc`) rather than reimplementing bitmap setup, input handling, sound, and text output |

Start with `c64asm-reference.md` for assembler syntax, `c64asm-opcode-reference.md`
for what a given instruction actually does, and `c64-memory-reference.md`
when you need a specific hardware register.

## Syntax at a glance

```asm
loop:   lda #$00        ; label + instruction + comment
        sta $d020        ; absolute addressing
        lda $10,x        ; zero page,X (auto-selected for values <= 255)
        lda #<message    ; low byte of an address
        bne loop          ; relative branch
SCREEN = $0400            ; constant definition
        .byte $01,$02,"AB",$00
        .word SCREEN + 40

.macro SET_COLOR addr, value   ; macros...
        lda #\value
        sta \addr
.endmacro
        SET_COLOR $d020, WHITE

.include "lib/text.inc"         ; ...and shared library code
        PRINT my_message
```

See `c64asm-reference.md` for the complete syntax, directive list, and
addressing-mode rules.

## Standard library

`c64asm-stdlib.zip` unpacks to a `lib/` directory of `.include`-able
files — register constants, PETSCII text output and string comparison,
joystick/keyboard/typed-line input (including named constants for
every key on the keyboard matrix, and a blocking "wait for any key"
read), bitmap graphics setup, and SID sound effects — extracted from
and cross-checked against the demo programs that originally
implemented each piece from scratch. Nothing in it is new, untested
logic; it's existing, hardware-verified patterns pulled into reusable
form. Every file checks its own required zero-page symbols with
`.error` (see above) right at the top, so a missing one fails with a
specific message naming exactly what to define, not a generic
`Undefined symbol` from somewhere inside a routine you never called
directly. Eight of the nine programs above (`adventure.asm`,
`bounce.asm`, `pong.asm`, `lander.asm`, the library-focused `demo.asm`,
`sprites.asm`, `music_demo.asm`, and `editor.asm`) are built *on* the
library rather than duplicating it — only `hello.asm` still has
everything written out locally, since it's deliberately meant as the
simplest possible complete program, with no dependencies at all — see
`lib-reference.md` (inside the zip) for the full API, required
zero-page setup per file, and worked examples, including two smaller,
even more focused demo programs (`demo.asm`, alongside `bounce.asm`)
built specifically to exercise the library in relative isolation.

By default, each project needs its own copy of `lib/` sitting next to
its `.asm` files (`.include` paths resolve relative to the including
file). To share one `lib/` directory across several separate projects
instead, pass `--lib-dir <path to the lib/ folder itself>` on the
command line — it's a fallback only, tried just when the default
lookup doesn't find the file, so it's safe to pass unconditionally
without it overriding a project's own local files. See
`c64asm-reference.md` §1 for the full behavior and an example.

## mini6502: the test harness

`mini6502.zip` contains a 6502 CPU emulator plus a C64Machine layer
(CIA keyboard-matrix and joystick emulation with data-direction-register
awareness, `CHROUT`/`CHRIN` trapping, zero-page KERNAL-poisoning
simulation) written specifically to test-drive this project's own
output — not a general-purpose VICE replacement. Every demo and every
library routine has been played through programmatically with it: full
game solution paths, failure paths, and simulated typed keyboard input.
It isn't a substitute for testing on real hardware or in VICE, though —
several bugs in this project's history (see "a note on the demos"
below) were only caught that way, and mini6502 was then updated to
model the behavior that had been missed (see `mini6502-reference.md`
for the API and `c64asm-stdlib.zip`'s `test_demo.py`/`test_adventure.py`
for worked regression suites built on it).

## Known limitations

- **Zero-page sizing of a *forward-referenced* label** can, in rare
  cases, differ between passes — see `c64asm-reference.md` §23 for when
  this can matter and why it almost never does in practice
- Macros, local labels, `.include`, and conditional assembly are all
  supported but intentionally simple: macros must be defined before
  use, `.if`/`.elif` conditions can't reference a forward-declared
  symbol, and `.if` can gate instructions/data but not which `.macro`
  gets defined or which file gets `.include`d (see `c64asm-reference.md`
  §23 for the full list)
- Assembly can surface several independent errors from one run (see
  `c64asm-reference.md` §22), though messages after the first can
  occasionally be downstream noise rather than genuinely separate
  problems; a handful of whole-file structural errors (missing/circular
  `.include`, a broken macro or conditional-assembly block) still stop
  assembly immediately
- `.charset lower` (§6) produces real mixed-case PETSCII bytes, but
  pairing it with `lib/text.inc`'s `SET_LOWERCASE_CHARSET` macro (or
  an equivalent manual runtime switch) is required to actually see
  lowercase on screen — the assembler can't do that part itself, since
  it's runtime hardware state, not something that exists at assembly
  time
- A library file's code is assembled unconditionally once `.include`d,
  even for routines you never call — meaning, for example, any program
  including `lib/text.inc` needs `cmp_ptr`/`kw_ptr` defined even if it
  never calls `str_equal` (see `lib-reference.md`'s setup section)

## A note on the demos

Every demo here is more than filler — each one is the program that shook
out a real bug during development, all fixed in the assembler or
library itself rather than worked around in the examples:

- A text-encoding mixup, a broken multiplication operator, and a
  silently-corrupting `.org` gap (assembler bugs, caught early)
- A VIC-II character-ROM address collision (sprite/bitmap data placed
  where the VIC-II substitutes its own character ROM instead)
- A CIA data-direction-register collision between joystick and
  keyboard reads — found *three separate times*, independently, in
  `demo.asm`, `pong.asm`, and `lander.asm` (the last one the worst:
  fuel drained and the ship drifted sideways on every single flight,
  with nothing held at all)
- A stale calling convention left behind by a refactor, and the
  discovery that typed keyboard input and `.text`-encoded strings must
  use identical PETSCII encoding, or every keyword comparison in a
  program silently fails
- A missing newline that ran a typed command's response onto the same
  screen line as what was just typed
- A sprite that visibly stopped short of the screen's true edges
  because its position was tracked in a single byte — found in both
  `bounce.asm` (the ball) and `pong.asm` (the right paddle and net,
  using only about 2/3 of the available width)
- A more subtle sibling of that same bug: once `pong.asm`'s paddle
  bounds were corrected to actually reach the true bottom edge, a
  served ball could land exactly on that edge while still heading
  toward it — past where the wall-bounce check (built to catch a ball
  only at the instant it *arrives* there) could ever detect it — and
  sail straight through, wrapping around instead of bouncing

They're a reasonable starting point to build your own programs from.
