# c64asm syntax reference

`c64asm` is a two-pass 6502/6510 assembler for the Commodore 64. It reads a
single source file and produces a `.prg` file: a two-byte little-endian load
address followed by the assembled machine code.

## Labels

```asm
loop:                   ; label with colon
loop    lda #$00        ; label without colon, followed by an instruction
SCREEN = $0400          ; constant assignment
```

## Numbers

```asm
$1234       ; hexadecimal
%01011010   ; binary
1234        ; decimal
'A'         ; character literal (PETSCII-mapped on output)
```

## Operators

`+  -  *  /  ( )` and the unary `<` (low byte) / `>` (high byte) operators.
`*` alone (not as a binary operator) means "current program counter".

## Addressing modes

```asm
LDA #$10          ; immediate
LDA $10            ; zero page
LDA $1000          ; absolute
LDA $10,X          ; zero page,X
LDA $1000,X        ; absolute,X
LDA $10,Y          ; zero page,Y
LDA $1000,Y        ; absolute,Y
LDA ($10,X)        ; indexed indirect
LDA ($10),Y        ; indirect indexed
JMP ($1000)        ; indirect
ASL A              ; accumulator
RTS                ; implied
BNE loop           ; relative (branches)
```

## Directives

| Directive | Meaning |
|---|---|
| `*=$0801` / `.org $0801` | Set the program counter / load address |
| `.byte` / `.db` | Emit a list of bytes |
| `.word` / `.dw` | Emit a list of 16-bit words |
| `.text` / `.asc` | Emit a string (converted to PETSCII) |
| `.fill` / `.ds` / `.res` | Reserve/fill a block of bytes |
| `.basic` | Emit a `10 SYS xxxx` BASIC loader stub |
| `.equ` | Same as `name = expr` |

## Comments

`;` runs to the end of the line.

## Command line

```
c64asm <input.asm> -o <output.prg> [--listing <file.lst>]
```
