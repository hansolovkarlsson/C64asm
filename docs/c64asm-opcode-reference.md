# c64asm Opcode Reference

This is a companion to `c64asm-reference.md`, which covers assembler
syntax and lists every addressing mode's opcode byte. This document
covers what each of the 56 instructions actually *does*: its operation,
which processor status flags it changes, and a worked example of each in
`c64asm` syntax.

## Flag legend

The 6502 status register has seven meaningful flags:

| Flag | Name | Meaning |
|---|---|---|
| **N** | Negative | Set to bit 7 of the result (i.e. set when the result is negative as a signed byte) |
| **V** | Overflow | Set when a signed arithmetic operation overflows |
| **B** | Break | Only meaningful in the copy of the status byte pushed to the stack by `BRK`; distinguishes a software interrupt from a hardware one |
| **D** | Decimal | When set, `ADC`/`SBC` operate in BCD (binary-coded decimal) mode instead of binary |
| **I** | Interrupt disable | When set, maskable (IRQ) interrupts are ignored |
| **Z** | Zero | Set when the result is zero |
| **C** | Carry | Set/cleared by arithmetic, shifts, and comparisons — see individual instructions |

An instruction's **Flags affected** line lists only the flags it changes;
every flag not listed is left exactly as it was.

---

## Addressing modes

An addressing mode is *how an instruction's operand tells the CPU where to
find the data it operates on*. The same instruction — say, `LDA` — can
load from an immediate constant, a fixed memory location, or a location
computed from a register and a pointer, depending entirely on which
addressing mode the operand's syntax selects. Not every instruction
supports every mode (see each instruction's **Modes** line, or the full
opcode-byte table in `c64asm-reference.md` §8); `c64asm` picks the mode
automatically from how you write the operand.

### Implied
No operand at all — the instruction's meaning is fixed, so there's
nothing to address. `RTS`, `INX`, `SEI`, and so on all work this way.

```asm
        rts
        inx
```

### Accumulator
Also no memory operand — the instruction reads and writes `A` directly.
Only the four shift/rotate instructions (`ASL`, `LSR`, `ROL`, `ROR`) have
this mode, and it's written either as `A` or left blank; both assemble
identically.

```asm
        asl a           ; A = A << 1
        asl             ; exactly the same instruction
```

### Immediate
The operand *is* the value, encoded as a literal byte right after the
opcode — not an address to fetch from. Written with a leading `#`.

```asm
        lda #$10        ; A = the literal value $10
        cpx #40         ; compare X against the literal value 40
```

This is the mode to reach for whenever you want a fixed constant rather
than "the byte stored at this address" — a very common source of
beginner bugs is writing `lda $10` (zero page — reads whatever happens to
be stored at address `$10`) when `lda #$10` (immediate — loads the value
16) was intended.

### Zero page
The operand is a one-byte address in the range `$00`–`$FF` (the CPU's
"zero page"). Because the address fits in a single byte, the instruction
encoding is one byte shorter, and the CPU fetches it faster, than the
equivalent absolute-mode instruction — which is why the 6502's first 256
bytes of RAM are so often used for frequently-accessed variables and
pointers.

```asm
        lda $fb         ; A = the byte stored at address $00FB
```

`c64asm` selects this mode automatically whenever an operand's value is
known at assemble time to fit in a byte; see `c64asm-reference.md` §5 for
exactly how that choice is made (and how to force absolute mode instead
with a 4-digit hex literal).

### Zero page,X
Like zero page, but the effective address is `(operand + X) mod 256` —
the addition wraps *within* the zero page rather than carrying into page
1. This makes it useful for indexing into a small table or buffer that
lives entirely in the first 256 bytes.

```asm
        ldx #$02
        lda $10,x       ; A = the byte at address ($10 + X) mod 256 = $12
```

### Zero page,Y
The same idea as zero page,X, but indexed by `Y` instead of `X`. Only
`LDX` and `STX` support this particular combination (indexing by the
*other* register than the one being loaded/stored) — every other
instruction that supports zero-page indexing uses `,X`.

```asm
        ldy #$03
        ldx $20,y       ; X = the byte at address ($20 + Y) mod 256
```

### Absolute
The operand is a full 16-bit address, encoded as two bytes (low byte
first) after the opcode. This can address anything in the 64KB address
space, at the cost of one extra instruction byte compared to zero page.

```asm
        lda $d020       ; A = the byte at address $D020 (the border color register)
```

### Absolute,X / Absolute,Y
Absolute addressing with the value of `X` or `Y` added to form the
effective address — `operand + X` (or `+ Y`), computed as a full 16-bit
sum with no wraparound at a page boundary. This is the standard way to
walk through an array or screen buffer larger than 256 bytes.

```asm
        ldx #$00
loop:
        lda source,x    ; A = byte at (source + X)
        sta $0400,x     ; store it into screen memory at ($0400 + X)
        inx
        cpx #40
        bne loop
```

### Indirect
Used by `JMP` only. The two bytes at the operand address (and the byte
right after it) are read as a 16-bit pointer, and execution jumps to
*that* address — a jump through a pointer, rather than to a fixed
location. This is how jump tables (an array of addresses you jump through
based on some index) are typically built.

```asm
        jmp (vector_table)   ; jump to whatever address is stored at
                              ; vector_table/vector_table+1
```

Historically, NMOS 6502s have a well-known hardware bug where `JMP
(operand)` fails to carry into the high byte correctly if the low byte of
the pointer address is `$FF` (e.g. `JMP ($12FF)` reads the high byte of
the target from `$1200` instead of `$1300`). The C64's 6510 has the same
bug — avoid placing an indirect-jump pointer table at an address ending
in `$FF`.

### Indexed indirect — `(zp,X)`
A two-step lookup: `X` is added to the zero-page operand *first* (with
zero-page wraparound, like zero page,X), and the two bytes found there —
and at the next address — are read as a 16-bit pointer, which becomes the
effective address. In other words: `X` selects *which pointer* out of a
small zero-page table of pointers to use.

```asm
        ldx #$04
        lda ($10,x)     ; reads a pointer from ($10+X) and ($10+X+1),
                         ; then A = the byte at that pointer's address
```

### Indirect indexed — `(zp),Y`
The other way around, and far more commonly used: the zero-page operand
*itself* is read as a 16-bit pointer (no indexing happens yet), and *then*
`Y` is added to that pointer to form the effective address. This is the
standard way to iterate over a buffer whose start address is a runtime
value stored in zero page (rather than a fixed label known at assemble
time) — e.g. walking a string or record whose address was computed or
passed in.

```asm
; assume $fb/$fc hold a 16-bit pointer to some buffer
        ldy #$00
        lda ($fb),y     ; A = the byte at (pointer stored in $fb/$fc) + Y
        iny
```

The parenthesis placement is what distinguishes this from indexed
indirect, and mixing the two up is one of the most common 6502 mistakes:
`($10,x)` indexes *before* dereferencing (picks a pointer), while
`($10),y` indexes *after* dereferencing (offsets into what the pointer
points to).

### Relative
Used only by the eight branch instructions. The operand is a *label or
address*, but it's encoded as a signed 8-bit offset from the address of
the instruction immediately following the branch — not as an absolute
address at all. `c64asm` computes this offset for you from whatever label
you write, and reports a "Branch target out of range" error if the target
is more than 127 bytes forward or 128 bytes backward.

```asm
loop:
        dex
        bne loop        ; encoded as a small negative offset back to 'loop'
```

---

## Load / Store

### LDA — Load Accumulator
**Operation:** `A ← M`
**Flags affected:** N, Z (based on the loaded value)
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        lda #$00        ; A = 0, Z is set, N is clear
        lda SCREEN      ; A = contents of the SCREEN label's address
        lda $0400,x     ; A = byte at $0400+X
```

### LDX — Load X Register
**Operation:** `X ← M`
**Flags affected:** N, Z
**Modes:** IMM, ZP, ZPY, ABS, ABSY

```asm
        ldx #$00        ; typical loop-counter initialization
        ldx COUNT,y
```

### LDY — Load Y Register
**Operation:** `Y ← M`
**Flags affected:** N, Z
**Modes:** IMM, ZP, ZPX, ABS, ABSX

```asm
        ldy #$27        ; 39 decimal — e.g. last column of a 40-col screen row
        ldy $d3         ; load Y from a zero-page pointer's low byte
```

### STA — Store Accumulator
**Operation:** `M ← A`
**Flags affected:** none
**Modes:** ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        lda #$01
        sta $d020       ; set the border color to white
```

### STX — Store X Register
**Operation:** `M ← X`
**Flags affected:** none
**Modes:** ZP, ZPY, ABS

```asm
        ldx #$05
        stx $fb         ; save X to a zero-page scratch location
```

### STY — Store Y Register
**Operation:** `M ← Y`
**Flags affected:** none
**Modes:** ZP, ZPX, ABS

```asm
        ldy #$00
        sty $fc
```

---

## Register Transfers

### TAX — Transfer A to X
**Operation:** `X ← A`
**Flags affected:** N, Z

```asm
        lda #$10
        tax             ; X now $10
```

### TAY — Transfer A to Y
**Operation:** `Y ← A`
**Flags affected:** N, Z

```asm
        lda #$10
        tay             ; Y now $10
```

### TXA — Transfer X to A
**Operation:** `A ← X`
**Flags affected:** N, Z

```asm
        txa             ; commonly used to move a loop index into A for STA
        sta buffer,y
```

### TYA — Transfer Y to A
**Operation:** `A ← Y`
**Flags affected:** N, Z

```asm
        tya
        sta buffer,x
```

### TSX — Transfer Stack Pointer to X
**Operation:** `X ← SP`
**Flags affected:** N, Z

```asm
        tsx             ; X now holds the current stack pointer, e.g. to
        stx saved_sp    ; save/restore the stack around a routine
```

### TXS — Transfer X to Stack Pointer
**Operation:** `SP ← X`
**Flags affected:** none — this is the one register-transfer instruction
that does **not** touch N/Z, because the stack pointer isn't treated as a
"result" value.

```asm
        ldx #$ff
        txs             ; classic startup code: reset the stack pointer
```

---

## Stack Operations

### PHA — Push Accumulator
**Operation:** push `A` onto the stack; `SP ← SP - 1`
**Flags affected:** none

```asm
        lda $d020
        pha             ; save the current border color
```

### PHP — Push Processor Status
**Operation:** push the status register onto the stack (with the B flag
forced to 1 in the pushed copy); `SP ← SP - 1`
**Flags affected:** none (the live status register is unchanged — only the
stack copy has B set)

```asm
        php             ; commonly paired with PLP to save/restore flags
                         ; around a section of code
```

### PLA — Pull Accumulator
**Operation:** `SP ← SP + 1`; `A ←` byte pulled from the stack
**Flags affected:** N, Z (based on the pulled value)

```asm
        pla             ; restore A saved earlier with PHA
```

### PLP — Pull Processor Status
**Operation:** `SP ← SP + 1`; status register `←` byte pulled from the
stack
**Flags affected:** N, V, D, I, Z, C — all of them are overwritten with
whatever was on the stack

```asm
        php
        sei             ; disable interrupts for a critical section
        ; ... time-critical code ...
        plp             ; restore the interrupt flag (and everything else)
                         ; to what it was before
```

---

## Logical

### AND — Logical AND
**Operation:** `A ← A AND M`
**Flags affected:** N, Z
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        lda #$ff
        and #%00001111  ; mask off the high nibble; A = $0F
```

### ORA — Logical OR
**Operation:** `A ← A OR M`
**Flags affected:** N, Z
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        lda #%00000001
        ora #%00000010  ; set bit 1 as well; A = %00000011
```

### EOR — Exclusive OR
**Operation:** `A ← A XOR M`
**Flags affected:** N, Z
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        lda #$ff
        eor #$ff        ; A = $00 — a common idiom for clearing A while
                         ; also clearing the carry-independent Z/N flags
        lda screen_char
        eor #$80        ; toggle bit 7 (a classic character-flash effect)
```

### BIT — Test Bits
**Operation:** computes `A AND M` to set Z, but **discards the result** —
A itself is never modified. Additionally, bits 7 and 6 of the memory
operand are copied directly into N and V, regardless of A.
**Flags affected:** N (= bit 7 of M), V (= bit 6 of M), Z (= 1 if `A AND M` is zero)
**Modes:** ZP, ABS

```asm
        lda #%00000001
        bit $d011       ; test a hardware register's bits without
        bmi raster_high ; disturbing A — branch if bit 7 of $D011 is set
```

---

## Arithmetic

### ADC — Add with Carry
**Operation:** `A ← A + M + C`
**Flags affected:** N, Z, C, V
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        clc             ; ADC always includes the carry, so clear it first
        lda #10
        adc #5          ; A = 15, C clear (no overflow past 255)

; 16-bit addition, low byte then high byte:
        clc
        lda ptr_lo
        adc #1
        sta ptr_lo
        lda ptr_hi
        adc #0          ; carry from the low-byte addition propagates in
        sta ptr_hi
```

### SBC — Subtract with Carry
**Operation:** `A ← A - M - (1 - C)` (equivalently, `A - M - borrow`)
**Flags affected:** N, Z, C, V
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        sec             ; SBC subtracts (1-C) as a borrow, so set C first
        lda #10
        sbc #3          ; A = 7, C set (no borrow needed)

; 16-bit subtraction:
        sec
        lda ptr_lo
        sbc #1
        sta ptr_lo
        lda ptr_hi
        sbc #0
        sta ptr_hi
```

### CMP — Compare Accumulator
**Operation:** computes `A - M` (like `SBC` but discards the result and
doesn't involve the carry going in)
**Flags affected:** N, Z, C — C is set if `A >= M` (unsigned), Z is set
if `A == M`, N reflects the sign of `A - M`
**Modes:** IMM, ZP, ZPX, ABS, ABSX, ABSY, INDX, INDY

```asm
        lda joystick
        cmp #$7f        ; test whether joystick reading equals $7F
        beq fire_pressed
```

### CPX — Compare X Register
**Operation:** computes `X - M`
**Flags affected:** N, Z, C (same semantics as `CMP`, using X)
**Modes:** IMM, ZP, ABS

```asm
        cpx #40
        beq row_done    ; branch once X has counted up to 40
```

### CPY — Compare Y Register
**Operation:** computes `Y - M`
**Flags affected:** N, Z, C (same semantics as `CMP`, using Y)
**Modes:** IMM, ZP, ABS

```asm
        cpy #$00
        beq list_empty
```

---

## Increments & Decrements

### INC — Increment Memory
**Operation:** `M ← M + 1`
**Flags affected:** N, Z
**Modes:** ZP, ZPX, ABS, ABSX

```asm
        inc $d020       ; bump the border color by one each time this runs
```

### INX — Increment X
**Operation:** `X ← X + 1` (wraps from 255 to 0)
**Flags affected:** N, Z

```asm
        ldx #$00
loop:
        lda data,x
        sta $0400,x
        inx
        cpx #40
        bne loop
```

### INY — Increment Y
**Operation:** `Y ← Y + 1` (wraps from 255 to 0)
**Flags affected:** N, Z

```asm
        iny
        bne no_wrap     ; branches unless Y just wrapped from 255 to 0
```

### DEC — Decrement Memory
**Operation:** `M ← M - 1`
**Flags affected:** N, Z
**Modes:** ZP, ZPX, ABS, ABSX

```asm
        dec lives_left
        bne still_alive
        jmp game_over
```

### DEX — Decrement X
**Operation:** `X ← X - 1` (wraps from 0 to 255)
**Flags affected:** N, Z

```asm
        ldx #10
countdown:
        dex
        bne countdown   ; loops until X wraps to 0
```

### DEY — Decrement Y
**Operation:** `Y ← Y - 1` (wraps from 0 to 255)
**Flags affected:** N, Z

```asm
        dey
        bpl still_positive
```

---

## Shifts and Rotates

All four shift/rotate instructions can operate on the accumulator
(`ASL A` / `ASL`, both accepted — see `c64asm-reference.md` §8) or on a
memory location.

### ASL — Arithmetic Shift Left
**Operation:** shifts all bits one place left; bit 7 moves into C; bit 0
becomes 0. Equivalent to multiplying by 2 (with overflow going into C).
**Flags affected:** N, Z, C
**Modes:** ACC/implied, ZP, ZPX, ABS, ABSX

```asm
        lda #%01000001
        asl a           ; A = %10000010, C = 0 (old bit 7 was 0)
        asl $d3          ; shift a zero-page byte in place
```

### LSR — Logical Shift Right
**Operation:** shifts all bits one place right; bit 0 moves into C; bit 7
becomes 0 (always — there's no sign extension). Equivalent to dividing an
unsigned byte by 2.
**Flags affected:** N (always cleared, since bit 7 is always 0 afterward), Z, C
**Modes:** ACC/implied, ZP, ZPX, ABS, ABSX

```asm
        lda #%00000011
        lsr a           ; A = %00000001, C = 1 (old bit 0 was 1)
```

### ROL — Rotate Left
**Operation:** like `ASL`, but the *old* value of C is shifted into bit 0
(instead of a 0), and the old bit 7 becomes the new C. This chains across
bytes for multi-byte shifts.
**Flags affected:** N, Z, C
**Modes:** ACC/implied, ZP, ZPX, ABS, ABSX

```asm
; 16-bit left shift, low byte then high byte, carry chains between them:
        asl val_lo      ; shifts the low byte, old bit 7 -> C
        rol val_hi      ; shifts the high byte, C rotates in at bit 0
```

### ROR — Rotate Right
**Operation:** like `LSR`, but the *old* value of C is shifted into bit 7
(instead of a 0), and the old bit 0 becomes the new C.
**Flags affected:** N, Z, C
**Modes:** ACC/implied, ZP, ZPX, ABS, ABSX

```asm
; 16-bit right shift, high byte then low byte:
        lsr val_hi      ; shifts the high byte, old bit 0 -> C
        ror val_lo      ; shifts the low byte, C rotates in at bit 7
```

---

## Jumps and Calls

### JMP — Jump
**Operation:** `PC ← <operand address>`
**Flags affected:** none
**Modes:** ABS, IND

```asm
        jmp main_loop

; indirect jump — commonly used for a jump table:
        jmp (vector_table)
```

### JSR — Jump to Subroutine
**Operation:** pushes the address of the last byte of the `JSR`
instruction (i.e. return-address-minus-one) onto the stack, then
`PC ← <operand address>`
**Flags affected:** none
**Modes:** ABS

```asm
        jsr clear_screen
        jsr print_message
        rts
```

### RTS — Return from Subroutine
**Operation:** pulls an address from the stack, adds 1, and jumps there —
undoing exactly what `JSR` pushed
**Flags affected:** none

```asm
print_message:
        ldx #$00
print_loop:
        lda message,x
        beq print_done
        jsr $ffd2
        inx
        jmp print_loop
print_done:
        rts
```

---

## Branches

All eight branch instructions test a flag and, if the condition holds,
add a signed 8-bit offset to the program counter — nothing else. **None
of them affect any flag**; they only *read* flags set by earlier
instructions. The branch target must be within −128..+127 bytes of the
address immediately after the branch instruction, or `c64asm` reports a
"Branch target out of range" error (see `c64asm-reference.md` §11).

### BCC — Branch if Carry Clear
**Condition:** `C = 0`

```asm
        cmp #$80
        bcc less_than   ; taken if A < $80 (unsigned)
```

### BCS — Branch if Carry Set
**Condition:** `C = 1`

```asm
        cmp #$80
        bcs greater_or_equal
```

### BEQ — Branch if Equal (Zero Set)
**Condition:** `Z = 1`

```asm
        lda counter
        beq counter_is_zero
```

### BNE — Branch if Not Equal (Zero Clear)
**Condition:** `Z = 0`

```asm
        dex
        bne loop        ; the classic "loop while X isn't zero yet"
```

### BMI — Branch if Minus (Negative Set)
**Condition:** `N = 1`

```asm
        lda velocity
        bmi moving_left
```

### BPL — Branch if Plus (Negative Clear)
**Condition:** `N = 0`

```asm
        lda velocity
        bpl moving_right_or_still
```

### BVC — Branch if Overflow Clear
**Condition:** `V = 0`

```asm
        adc delta
        bvc no_overflow
```

### BVS — Branch if Overflow Set
**Condition:** `V = 1`

```asm
        adc delta
        bvs handle_overflow
```

---

## Status Flag Changes

These six instructions each set or clear exactly one flag and affect
nothing else.

### CLC — Clear Carry
```asm
        clc             ; always do this before ADC unless you specifically
                         ; want to add in a carry from a previous operation
```

### SEC — Set Carry
```asm
        sec             ; always do this before SBC/CMP-style subtraction
                         ; unless you specifically want to subtract a borrow
```

### CLD — Clear Decimal Mode
```asm
        cld             ; the C64 KERNAL leaves decimal mode in an unknown
                         ; state after some calls — clear it before doing
                         ; binary arithmetic to be safe
```

### SED — Set Decimal Mode
```asm
        sed             ; switch ADC/SBC into BCD mode, e.g. for a
        clc             ; score counter stored as decimal digits
        lda score_lo
        adc #1
        sta score_lo
```

### CLI — Clear Interrupt Disable
```asm
        cli             ; re-enable IRQs, e.g. at the end of setting up a
                         ; raster interrupt handler
```

### SEI — Set Interrupt Disable
```asm
        sei             ; disable IRQs before touching shared/critical state
```

### CLV — Clear Overflow
```asm
        clv             ; explicitly clear V; there is no "set overflow"
                         ; instruction — SEV doesn't exist on the 6502
```

---

## System

### BRK — Force Break (software interrupt)
**Operation:** pushes `PC+2` and the status register (with B forced to 1)
onto the stack, sets `I ← 1`, then loads `PC` from the IRQ/BRK vector at
`$FFFE`/`$FFFF`. Effectively a one-byte `JSR` to whatever interrupt
handler is installed, with a marker (the pushed B flag) that lets the
handler tell it apart from a real hardware IRQ.
**Flags affected:** I is set; B is set only in the copy of the status
byte on the stack

```asm
        brk             ; rarely used directly in application code; more
                         ; often used as a deliberate crash/breakpoint
                         ; when testing in an emulator with a debugger
```

### NOP — No Operation
**Operation:** does nothing for 2 cycles
**Flags affected:** none

```asm
        nop             ; used as filler, e.g. to pad timing-critical code
                         ; to an exact cycle count, or to patch out an
                         ; instruction without changing code layout/size
```

### RTI — Return from Interrupt
**Operation:** pulls the status register, then the program counter, from
the stack, and resumes execution there. Used to return from an interrupt
handler entered via a hardware IRQ/NMI or a `BRK`.
**Flags affected:** N, V, D, I, Z, C — all restored from the stack

```asm
irq_handler:
        pha
        txa
        pha
        tya
        pha
        ; ... handle the interrupt ...
        pla
        tay
        pla
        tax
        pla
        rti             ; restores flags and returns to the interrupted code
```
