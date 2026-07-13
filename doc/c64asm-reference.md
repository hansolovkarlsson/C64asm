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
python3 c64asm.py <input.asm> -o <output.prg> [--listing <file.lst>]
cc -O2 -o c64asm c64asm.c && ./c64asm <input.asm> -o <output.prg> [--listing <file.lst>]
```

| Argument | Required | Description |
|---|---|---|
| `<input.asm>` | yes | Path to the assembly source file. |
| `-o`, `--output <file>` | yes | Path to write the assembled `.prg` file. |
| `--listing <file>` | no | Write a listing file: addresses, encoded bytes, source lines, and a final symbol table. |
| `-h`, `--help` | no | Print a short usage message. |

On success, the assembler prints the number of bytes assembled and the
origin address:

```
Assembled 60 bytes, origin=$0801 -> hello.prg
```

On error, it prints a message to stderr identifying the line number and
source text, and exits with a non-zero status. Assembly stops at the first
error (there is no error-recovery / multi-error reporting).

```
Assembly error: Undefined symbol in operand 'undefined_thing' (line 2: lda undefined_thing)
```

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
as PETSCII bytes suitable for output through the KERNAL `CHROUT` routine
on the **default** (uppercase/graphics) C64 character set:

- Uppercase letters, digits, and punctuation are emitted unchanged — on
  the default charset these PETSCII codes (`$41`–`$5A` for `A`–`Z`) display
  exactly like ASCII.
- Lowercase input letters are folded up to uppercase, since the default
  charset has no separate lowercase glyphs at those codes.

There is currently no support for emitting true lowercase text (which
requires switching the VIC-II character set with PETSCII control code
`$0E` and using the `$C1`–`$DA` range) — all text output prints as
uppercase regardless of the case used in the source.

---

## 7. Directives

| Directive | Aliases | Operand | Effect |
|---|---|---|---|
| `.org expr` | `* = expr` | one expression | Set the program counter. The *first* `.org` (or `.basic`) encountered also sets the file's load address (the two-byte header written to the `.prg`). |
| `.byte a, b, ...` | `.db` | comma-separated bytes or `"strings"` | Emit raw bytes. Numeric arguments are truncated to 8 bits; quoted strings are PETSCII-encoded (§6). |
| `.word a, b, ...` | `.dw` | comma-separated 16-bit values | Emit each value as two bytes, little-endian. |
| `.text "..."` | `.asc` | comma-separated `"strings"` (or bare text) | PETSCII-encode and emit text (§6). |
| `.fill count, value` | `.ds`, `.res` | count, optional fill byte (default 0) | Emit `count` bytes of `value`. |
| `.basic` | — | none | Emit a tokenized BASIC line `10 SYS <addr>` at `$0801`, where `<addr>` is automatically computed to point at the very next byte of assembled code — i.e. wherever the code following `.basic` ends up. Typing `LOAD"...",8,1` then `RUN` starts the machine code directly. Must appear before any code you want it to `SYS` into. |
| `label = expr` | `.equ` | one expression | Bind `label` to the value of `expr` (not to the current PC). See §2. |

`.byte`/`.word`/`.text`/`.fill` all accept comma-separated argument lists,
so `.byte $01, $02, "AB", $00` mixes numeric bytes and quoted text on a
single line.

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

---

## 8. Instruction set

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

Undocumented/illegal 6502 opcodes (`LAX`, `SAX`, `DCP`, etc.) are **not**
supported.

---

## 9. Output format

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

## 10. Listing file format

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

## 11. Error messages

All errors are fatal (assembly stops immediately) and are printed in the
form:

```
Assembly error: <message> (line <N>: <source text>)
```

Common errors:

| Message | Cause |
|---|---|
| `Unknown mnemonic or directive '...'` | A token that isn't a recognized instruction, directive, or valid label position. |
| `Symbol '...' already defined` | A label was defined twice with two different address values. |
| `Undefined symbol in operand '...'` | A symbol used in an instruction operand was never defined anywhere in the file. |
| `Undefined symbol in .org expression` | Same, but for `.org`/`*=`. |
| `<MNEMONIC> requires an operand` | An instruction with no implied-mode encoding was given no operand. |
| `<MNEMONIC> does not support that addressing mode` | The operand's syntax implies a mode the instruction doesn't have (e.g. immediate mode on `STA`). |
| `Branch target out of range (+N)` | A branch instruction's target is more than 127 bytes forward or 128 bytes backward from the instruction following it. Reorganize the code, or replace the branch with an unconditional `JMP` reached via an inverted branch. |
| `Missing ')' in expression '...'` | Unbalanced parentheses in an expression. |
| `Bad character '...' in expression '...'` | A character the tokenizer doesn't recognize appeared in an expression. |

---

## 12. Known limitations

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
- **No macros, no conditional assembly, no `.include`.** Everything must
  live in a single source file.
- **No undocumented/illegal opcodes.**
- **Single error at a time.** Assembly halts at the first error rather
  than collecting and reporting multiple problems.
- **PETSCII output is uppercase-only** (see §6).

---

## 13. Complete example

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
