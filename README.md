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
- Two-pass assembly, so forward references to labels just work
- Automatic zero-page vs. absolute addressing selection
- A real expression evaluator: `+ - * /`, parentheses, `<`/`>` for
  low/high byte, `$hex`, `%binary`, decimal, `'char'` literals, `*` for
  the current program counter
- **Macros** (`.macro`/`.endmacro`) with named parameter substitution
  and recursive invocation
- **Local labels** (`@label`), scoped between global labels and per
  macro expansion, so loop/branch labels inside a subroutine or macro
  never collide with anything else in the file
- **`.include`**, with automatic include-once semantics (no manual
  include guards needed), relative path resolution, and circular-include
  detection
- **Conditional assembly** (`.if`/`.elif`/`.else`/`.endif`,
  `.ifdef`/`.ifndef`) for things like PAL/NTSC timing variants
- Directives for raw bytes/words, text (with ASCII→PETSCII conversion),
  memory fills, byte alignment (`.align`), symbol/constant definitions,
  and a `.basic` directive that auto-generates a correct `10 SYS xxxx`
  BASIC loader stub
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

## Project structure

| File | What it is |
|---|---|
| `c64asm.py` | The assembler, Python implementation |
| `c64asm.c` | The assembler, single-file portable C99 implementation |
| `c64asm-split-src.zip` | The same assembler split into one file per concern, heavily commented, with a `Makefile` — for reading, not a different implementation (see `ARCHITECTURE.md`) |
| `ARCHITECTURE.md` | Guide to the split-source project's module layout |
| `c64asm-reference.md` | **Assembler syntax reference** — labels, expressions, addressing-mode syntax, macros, local labels, `.include`, conditional assembly, every directive, error messages, CLI usage |
| `c64asm-opcode-reference.md` | **6502 opcode reference** — what every instruction does, which status flags it affects, and a worked example of each; plus a full write-up of all 13 addressing modes |
| `c64-memory-reference.md` | **C64 hardware reference** — screen/color RAM, VIC-II graphics modes, sprites, SID sound, joystick input, common KERNAL routines, all with tested example code |
| `c64asm-stdlib.zip` | **Standard library** — `.include`-able text/input/graphics/sound routines, shared across the demos (see below) |
| `mini6502.zip` | **mini6502** — a from-scratch 6502/C64 emulator used to test-drive every demo and library routine below (see below) |
| `hello.asm` / `.prg` / `.lst` | Demo: prints text via `CHROUT` and cycles the border color |
| `bounce.asm` / `.prg` / `.lst` | Demo: a sprite bouncing around a bitmap graphics screen, raster-synced |
| `pong.asm` / `.prg` / `.lst` | Demo: two-paddle Pong, CIA keyboard-matrix input, ball/paddle collision |
| `adventure.asm` / `.prg` / `.lst` | Demo: a small text adventure — typed commands via `CHRIN`, a room/item/puzzle state machine; uses the standard library (`lib/text.inc`, `lib/input.inc`) rather than reimplementing string/input handling |
| `lander.asm` / `.prg` / `.lst` | Demo: lunar lander — bitmap graphics, physics, terrain collision, a fuel bar, sound, and an explosion animation |

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
joystick/keyboard/typed-line input, bitmap graphics setup, and SID sound
effects — extracted from and cross-checked against the demo programs
that originally implemented each piece from scratch. Nothing in it is
new, untested logic; it's existing, hardware-verified patterns pulled
into reusable form. `adventure.asm` above is the clearest example of a
program built *on* the library rather than duplicating it — see
`lib-reference.md` (inside the zip) for the full API, required
zero-page setup per file, and worked examples, including the two
smaller, focused demo programs (`demo.asm`, `adventure.asm`) built
specifically to exercise the library in isolation.

By default, each project needs its own copy of `lib/` sitting next to
its `.asm` files (`.include` paths resolve relative to the including
file). To share one `lib/` directory across several separate projects
instead, pass `--lib-dir <path to the directory containing lib/>` on
the command line — it's a fallback only, tried just when the default
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
  cases, differ between passes — see `c64asm-reference.md` §16 for when
  this can matter and why it almost never does in practice
- Macros, local labels, `.include`, and conditional assembly are all
  supported but intentionally simple: macros must be defined before
  use, `.if`/`.elif` conditions can't reference a forward-declared
  symbol, and `.if` can gate instructions/data but not which `.macro`
  gets defined or which file gets `.include`d (see `c64asm-reference.md`
  §16 for the full list)
- No undocumented/illegal 6502 opcodes
- Assembly halts at the first error rather than collecting several
- `.text`/`.asc`/quoted `.byte` output uppercase-only PETSCII (no
  charset-switch support for true lowercase)
- A library file's code is assembled unconditionally once `.include`d,
  even for routines you never call — meaning, for example, any program
  including `lib/text.inc` needs `cmp_ptr`/`kw_ptr` defined even if it
  never calls `str_equal` (see `lib-reference.md`'s setup section)

## A note on the demos

Every demo here is more than filler — each one is the program that shook
out real bugs during development (a text-encoding mixup, a broken
multiplication operator, a silently-corrupting `.org` gap, a VIC-II
character-ROM address collision, a CIA data-direction-register
collision between joystick and keyboard reads, and — most recently, while
extracting shared code into the standard library — a stale calling
convention left behind by a refactor, and the discovery that typed
keyboard input and `.text`-encoded strings must use identical PETSCII
encoding or every keyword comparison in a program silently fails), all
of which got fixed in the assembler or library itself, not worked
around in the examples. They're a reasonable starting point to build
your own programs from.
