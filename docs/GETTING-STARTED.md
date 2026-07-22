# Getting started with c64asm

A short, example-driven walkthrough to get you from nothing to a
running Commodore 64 program. For the complete syntax reference once
you're past this, see `c64asm-reference.md`; for the standard library,
see `lib-reference.md`; for a map of everything else in this project,
see `README.md`.

## What you need

- **Python 3** (for the Python implementation — no other dependencies),
  **or** a C compiler (`cc`/`gcc`/`clang` — for the C implementation).
  Either one alone is enough; you don't need both.
- Optionally, [VICE](https://vice-emu.sourceforge.io/), a C64 emulator,
  to actually run what you assemble. Not required to *use* the
  assembler, but you'll want it (or real hardware) to see anything
  happen.

## Step 1: get the assembler running

Pick whichever of these fits how you like to work — all three accept
identical syntax and produce byte-identical output, so it's purely a
matter of preference.

**Python**, no build step at all:

```
python3 c64asm.py yourfile.asm -o yourfile.prg
```

**C**, a single `cc -O2 -o c64asm c64asm.c` away:

```
cc -O2 -o c64asm c64asm.c
./c64asm yourfile.asm -o yourfile.prg
```

**Split-source C**, the same assembler broken into one file per
concern with a `Makefile` — worth it if you want to read or modify how
the assembler itself works (see `ARCHITECTURE.md`), not a different
assembler:

```
unzip c64asm-split-src.zip && make
./c64asm yourfile.asm -o yourfile.prg
```

The rest of this guide shows the Python command; swap in whichever
you picked.

## Step 2: your first program

Save this as `first.asm`:

```asm
        .basic
        lda #$00
        sta $d020
        rts
```

Three real instructions: set the accumulator to 0, store it at $D020
(the border color register), and return — cycling nothing yet, just
turning the border black. `.basic` generates the small BASIC stub
every runnable `.prg` needs, so typing `RUN` (or VICE's autostart)
actually starts your code, without you having to hand-assemble that
stub yourself.

Assemble it:

```
$ python3 c64asm.py first.asm -o first.prg
Assembled 18 bytes, origin=$0801 -> first.prg
```

That's it — `first.prg` is a real, loadable C64 program. Load it into
VICE (File → Autostart, or just drag the file onto the emulator
window) or write it to real hardware to see the border go black.

## Step 3: when something's wrong

Programs don't always assemble cleanly. Add a line referencing a name
you never defined:

```asm
        .basic
        lda #$00
        sta $d020
        lda undefined_label
        rts
```

```
$ python3 c64asm.py first.asm -o first.prg
Assembly error: Undefined symbol in operand 'undefined_label' (line 4: lda undefined_label)
1 error.
```

Every error names the line number and shows the actual source text,
so you don't need to go hunting for what triggered it. Most kinds of
mistake — not just this one — get collected and reported together
rather than stopping at the first one, so a typo-riddled first draft
tells you everything wrong with it in one pass instead of one error
at a time; see `c64asm-reference.md`'s "Error messages" section for
the full behavior.

## Step 4: using the standard library

Writing text output, sprite setup, or sound from scratch every time
gets old fast — `c64asm-stdlib.zip` has ready-made routines for exactly
that. Here's the smallest complete example, printing one line of text:

```asm
        .basic start
str_ptr = $fb
cmp_ptr = $fd
kw_ptr  = $02
        .include "lib/text.inc"

start:
        PRINT hello_msg
        rts

hello_msg:
        .text "HELLO FROM THE STANDARD LIBRARY", 13, 0
```

```
$ python3 c64asm.py hello_lib.asm -o hello_lib.prg --lib-dir lib
Assembled 98 bytes, origin=$0801 -> hello_lib.prg
```

A few things worth noticing here, all covered in more depth in
`lib-reference.md`:

- **`str_ptr`, `cmp_ptr`, and `kw_ptr`** are zero-page addresses
  `lib/text.inc`'s own routines need as working space. Every library
  file documents exactly which zero-page locations it needs, right at
  the top of the file (a comment block starting `; Requires`) — declare
  them with `=` *before* the `.include` line, or you'll get a clear
  message naming exactly what's missing (try deleting the `kw_ptr =
  $02` line above and reassembling, to see one). This is deliberate:
  a missing precondition fails loudly and specifically, right where
  the mistake is, instead of surfacing as a confusing `Undefined
  symbol` three subroutine calls deep.
- **`.basic start`, not bare `.basic`.** This one is worth
  internalizing early, because it's an easy trap: bare `.basic` jumps
  to whatever comes immediately after it in the file. If a `.include`
  comes right after `.basic` — which it almost always does, since
  zero-page declarations and `.include` lines both tend to sit near
  the top of a file — that "immediately after" code is the *library's*
  own subroutines, not yours, and your program starts executing there
  by accident. Naming an explicit label (`.basic start`, with a
  matching `start:` wherever your own code actually begins) sidesteps
  this entirely, and is worth doing as a habit even in small programs.
- **`--lib-dir lib`** tells the assembler where to look for
  `.include`d library files if they're not sitting relative to your
  own source file. Point it at wherever you unpacked
  `c64asm-stdlib.zip`'s `lib/` folder.

## Step 5: bigger, real, tested examples

Every demo shipped with this project is a complete, working C64
program, not a toy snippet — and the more involved ones (`demo.asm`
through `music_demo.asm` below) have each been actually executed, not
just assembled, as part of this project's own testing (`hello.asm` is
simple enough that it's verified by assembling cleanly alone). Reading
one is often the fastest way to see a technique used for real:

- **`hello.asm`** — the classic text-and-border-color example, in
  full.
- **`demo.asm`** — a visible sprite moved with W/A/S/D, using most of
  the standard library at once; the best single file to read for "how
  do these library pieces fit together."
- **`bounce.asm`**, **`pong.asm`**, **`lander.asm`** — progressively
  more complete games: raster-synced animation, joystick and keyboard
  input, collision, sound.
- **`adventure.asm`** — a text adventure showing `.struct`, `.assert`,
  and `.tag` used together on real, non-throwaway data.
- **`sprites.asm`** — sprite animation loaded from an external binary
  asset via `.incbin`, instead of hand-transcribed `.byte` data.
- **`music_demo.asm`** — two-voice SID music via `lib/music.inc`, a
  real tune (public domain), not a single test tone.

`README.md`'s file table has a one-line description of what each one
specifically demonstrates.

## Where to go next

| If you want to know... | Look at... |
|---|---|
| The complete assembler syntax — every directive, addressing mode, and error message | `c64asm-reference.md` |
| What a specific 6502 instruction does, and its addressing modes | `c64asm-opcode-reference.md` |
| A C64 hardware register or KERNAL routine | `c64-memory-reference.md` |
| What's in the standard library, and its own worked examples | `lib-reference.md` |
| How the split-source build is organized | `ARCHITECTURE.md` |
| What's changed recently, and why | `CHANGELOG.md` |
| The full feature list and every demo's file table | `README.md` |
| Turning a `.prg` back into source | `c64disasm.py` — see `README.md`'s "Disassembler" section |

If you're coming from another 6502 assembler, `c64asm-reference.md`'s
early sections (source line structure, expressions, addressing modes)
will look familiar fast — the differences worth knowing about early
are the multi-error reporting (§22: most mistakes don't stop assembly
at the first one), and the standard library's `.error`-guarded
zero-page requirements (§15 covers `.error`/`.warning`; `lib-
reference.md` covers each library file's own requirements in full).
