# c64asm

A complete two-pass 6502/6510 assembler for the Commodore 64, available
in two interchangeable implementations — Python and portable C99 — plus
a full set of reference documentation and two working demo programs.

Both implementations accept identical syntax and are verified to produce
**byte-identical** `.prg` output for the same source file, so you can use
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
- Directives for raw bytes/words, text (with ASCII→PETSCII conversion),
  memory fills, symbol/constant definitions, and a `.basic` directive
  that auto-generates a correct `10 SYS xxxx` BASIC loader stub
- Clear, line-numbered error messages — undefined symbols, out-of-range
  branches, invalid addressing modes, and more, all caught before you
  waste time in an emulator
- Assembly listings (address / bytes / source, plus a final symbol table)
- Outputs standard C64 `.prg` files: a two-byte load address followed by
  the machine code, ready for any emulator or real hardware

## Quick start

**Python** (no dependencies beyond the standard library):

```
python3 c64asm.py <input.asm> -o <output.prg> [--listing <file.lst>]
```

**C** (portable C99; builds with `clang` on macOS or `gcc`/`clang` on
Linux, using only the standard library):

```
cc -O2 -o c64asm c64asm.c
./c64asm <input.asm> -o <output.prg> [--listing <file.lst>]
```

## Project structure

| File | What it is |
|---|---|
| `c64asm.py` | The assembler, Python implementation |
| `c64asm.c` | The assembler, portable C99 implementation |
| `c64asm-reference.md` | **Assembler syntax reference** — labels, expressions, addressing-mode syntax, every directive, error messages, CLI usage |
| `c64asm-opcode-reference.md` | **6502 opcode reference** — what every instruction does, which status flags it affects, and a worked example of each; plus a full write-up of all 13 addressing modes |
| `c64-memory-reference.md` | **C64 hardware reference** — screen/color RAM, VIC-II graphics modes, sprites, SID sound, joystick input, common KERNAL routines, all with tested example code |
| `hello.asm` / `.prg` / `.lst` | Demo: prints text via `CHROUT` and cycles the border color |
| `bounce.asm` / `.prg` / `.lst` | Demo: a sprite bouncing around a bitmap graphics screen, raster-synced |

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
```

See `c64asm-reference.md` for the complete syntax, directive list, and
addressing-mode rules.

## Known limitations

- No macros, conditional assembly, or `.include` — everything lives in
  one source file
- No undocumented/illegal 6502 opcodes
- Assembly halts at the first error rather than collecting several
- Zero-page sizing of a *forward-referenced* label can, in rare cases,
  differ between passes — see `c64asm-reference.md` §12 for when this
  can matter and why it almost never does in practice
- `.text`/`.asc`/quoted `.byte` output uppercase-only PETSCII (no
  charset-switch support for true lowercase)

## A note on the demos

Both `hello.asm` and `bounce.asm` are more than filler — they're the
programs that shook out real bugs during development (a text-encoding
mixup, a broken multiplication operator, a silently-corrupting `.org`
gap, and a VIC-II character-ROM address collision), all of which got
fixed in the assembler itself, not worked around in the examples. They're
a reasonable starting point to build your own programs from.
