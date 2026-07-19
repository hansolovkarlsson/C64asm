# c64asm Opcode Reference

This is a companion to `c64asm-reference.md`, which covers assembler
syntax and lists every addressing mode's opcode byte. This document
covers what each of the 56 documented instructions actually *does*: its
operation, which processor status flags it changes, and a worked
example of each in `c64asm` syntax. A final section also covers the
illegal/undocumented opcodes `c64asm` can optionally assemble via
`.cpu 6510x` — clearly marked as non-standard, since MOS never
documented or supported them.

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

---

## Illegal / Undocumented Opcodes

> **These are not standard 6502 instructions.** Everything above this
> line is part of the documented NMOS 6502/6510 instruction set that
> MOS Technology specified and supported. Everything below is not: it's
> a set of additional opcode bytes the NMOS 6502/6510 happens to
> execute as a side effect of how its instruction decoder is wired, but
> that MOS never documented, specified, or guaranteed. `c64asm` refuses
> to assemble any of them unless the `.cpu 6510x` directive is used
> first — see `c64asm-reference.md` §13 for the directive's syntax and
> the full opcode/mode table. This section covers what each one
> actually *does*, the same way the sections above do for the
> documented set.
>
> A real C64/C128 uses an NMOS chip (6510/8500/8502), so every
> instruction below works reliably there — but a few are marked
> **unstable** or **highly unstable** even on NMOS hardware (see each
> entry), and none of them exist at all on non-NMOS members of the 6502
> family (65C02, etc.). Code using them is inherently less portable
> than code using only the documented set above, and is worth flagging
> with a comment at the point of use.

### SLO — Shift Left, then OR
**Operation:** `{adr} := {adr} << 1` (with the shifted-out bit 7 going
into carry), then `A := A or {adr}` — an `ASL` immediately followed by
an `ORA` using the same address, combined into one opcode.
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        slo $10         ; equivalent to: asl $10 : ora $10
```

### RLA — Rotate Left, then AND
**Operation:** `{adr} := {adr} rol` (rotate left through carry), then
`A := A and {adr}` — a `ROL` immediately followed by an `AND` using the
same address.
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        rla $10         ; equivalent to: rol $10 : and $10
```

### SRE — Shift Right, then EOR
**Operation:** `{adr} := {adr} >> 1` (with the shifted-out bit 0 going
into carry), then `A := A xor {adr}` — an `LSR` immediately followed by
an `EOR` using the same address.
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        sre $10         ; equivalent to: lsr $10 : eor $10
```

### RRA — Rotate Right, then ADC
**Operation:** `{adr} := {adr} ror` (rotate right through carry), then
`A := A + {adr} + C` — a `ROR` immediately followed by an `ADC` using
the same address.
**Flags affected:** N, V, Z, C

```asm
        .cpu 6510x
        rra $10         ; equivalent to: ror $10 : adc $10
```

### SAX — Store A AND X
**Operation:** `{adr} := A and X`. A pure store: the AND happens as a
side effect of both registers being on the data bus at the same
moment, and no flags are touched.
**Flags affected:** none

```asm
        .cpu 6510x
        sax $10         ; store (A and X) into $10, without disturbing A or X
```

### LAX — Load A and X
**Operation:** `A, X := {adr}` — an `LDA` and `LDX` from the same
address, combined into one opcode.
**Flags affected:** N, Z

```asm
        .cpu 6510x
        lax $10         ; equivalent to: lda $10 : ldx $10
```

*Unstable in immediate mode* (`LAX #imm`, opcode `$AB`): on some
NMOS chips this loses bits, behaving closer to
`ORA #$FF : AND #imm : TAX` than a clean load. Avoid `LAX #imm` in
code meant to run reliably on real hardware; the memory-operand forms
above don't have this problem.

### DCP — Decrement, then Compare
**Operation:** `{adr} := {adr} - 1`, then compare `A` against the
decremented value — a `DEC` immediately followed by a `CMP` using the
same address.
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        dcp $10         ; equivalent to: dec $10 : cmp $10
```

### ISC — Increment, then Subtract
**Operation:** `{adr} := {adr} + 1`, then `A := A - {adr} - (1-C)` — an
`INC` immediately followed by an `SBC` using the same address. Also
written `ISB` in some other assemblers/references.
**Flags affected:** N, V, Z, C

```asm
        .cpu 6510x
        isc $10         ; equivalent to: inc $10 : sbc $10
```

### ANC — AND, then copy N into Carry
**Operation:** `A := A and #imm`, exactly like a normal `AND`, but bit
7 of the result is *also* copied into carry afterward, as if an
`ASL`/`ROL` had run on the result (without actually shifting `A`).
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        anc #$80        ; A := A and #$80; carry := bit 7 of the result
```

### ALR — AND, then Shift Right
**Operation:** `A := (A and #imm) >> 1` — an immediate `AND` followed
by an `LSR` on the accumulator. Also written `ASR` in some other
assemblers/references.
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        alr #$0F        ; equivalent to: and #$0F : lsr a
```

### ARR — AND, then Rotate Right (with ADC-like flags)
**Operation:** `A := (A and #imm) >> 1`, with bit 7 filled from carry
(a rotate, not a plain shift) — but unlike a plain `AND`+`ROR`, the V
and C flags afterward are computed the way an `ADC` would set them
from the AND'd value, not a simple rotate. See the
[oxyron.de table](http://www.oxyron.de/html/opcodes02.html) for the
precise bit-level flag derivation if you need it exactly.
**Flags affected:** N, V, Z, C

```asm
        .cpu 6510x
        arr #$0F        ; A := (A and #$0F) ror; V/C set ADC-style
```

### XAA — Transfer X to A, then AND
**Operation:** nominally `A := X and #imm` (a `TXA` immediately
followed by an immediate `AND`).
**Flags affected:** N, Z

```asm
        .cpu 6510x
        xaa #$0F        ; nominally: txa : and #$0F
```

**Highly unstable — do not use in code meant to run reliably.** Every
major reference on this opcode (including oxyron.de, quoted almost
verbatim here: "DO NOT USE!!! Highly unstable!!!") describes its actual
result as depending on analog effects of the specific chip, its
temperature, and the data present on the bus — not just the documented
operation above. `c64asm` will assemble it once `.cpu 6510x` is
enabled, but it's included mainly for completeness and deliberate
experimentation, not for real programs.

### AXS — (A AND X) minus immediate, into X
**Operation:** `X := (A and X) - #imm`. Flags are set the way a `CMP`
sets them (comparing `(A and X)` against `#imm`), not the way `SBC`
would. Also written `SBX` in some other assemblers/references.
**Flags affected:** N, Z, C

```asm
        .cpu 6510x
        axs #$01        ; X := (A and X) - 1, flags set CMP-style
```

### USBC — Subtract with Carry (duplicate encoding)
**Operation:** identical to the documented `SBC #imm` ($E9) — `A := A
- #imm - (1-C)`. This is the same instruction under a different name,
purely to avoid a naming collision with `SBC` in `c64asm`'s opcode
table; there's no reason to prefer `USBC` over `SBC` in new code, since
`SBC #imm` already assembles to the shorter, documented encoding.
**Flags affected:** N, V, Z, C

```asm
        .cpu 6510x
        usbc #$01       ; identical in effect to: sbc #$01
```

### AHX — Store A AND X AND (high byte + 1)
**Operation:** `{adr} := A and X and (H+1)`, where `H` is the high byte
of the target address. Also written `SHA` in some other
assemblers/references.
**Flags affected:** none

```asm
        .cpu 6510x
        ahx $1000,y     ; store (A and X and $11) into $1000+Y
```

**Highly unstable.** The `and (H+1)` part of the operation doesn't
reliably survive a page-boundary crossing on real hardware — the
"page" the value is stored in may not match what the source implies.
Avoid relying on the exact stored value in code meant to run reliably.

### SHY — Store Y AND (high byte + 1)
**Operation:** `{adr} := Y and (H+1)`, where `H` is the high byte of
the target address. Also written `SYA` in some other
assemblers/references.
**Flags affected:** none

```asm
        .cpu 6510x
        shy $1000,x     ; store (Y and $11) into $1000+X
```

**Unstable** in the same page-boundary-crossing sense as `AHX` above.

### SHX — Store X AND (high byte + 1)
**Operation:** `{adr} := X and (H+1)`, where `H` is the high byte of
the target address.
**Flags affected:** none

```asm
        .cpu 6510x
        shx $1000,y     ; store (X and $11) into $1000+Y
```

**Unstable** in the same page-boundary-crossing sense as `AHX` above.

### TAS — Transfer (A AND X) to Stack pointer, then store
**Operation:** `S := A and X`, then `{adr} := S and (H+1)`, where `H`
is the high byte of the target address — a combination of a stack
pointer transfer and a store. Also written `SHS` in some other
assemblers/references.
**Flags affected:** none

```asm
        .cpu 6510x
        tas $1000,y     ; S := A and X; store (S and $11) into $1000+Y
```

**Unstable** in the same page-boundary-crossing sense as `AHX` above,
and additionally overwrites the stack pointer, which affects every
subsequent `PHA`/`PLA`/`JSR`/`RTS` in ways that are easy to get wrong
even setting stability aside.

### LAS — Load A, X, and Stack pointer
**Operation:** `A, X, S := {adr} and S` — the addressed byte is AND'd
with the *current* stack pointer, and the result is loaded into `A`,
`X`, *and* the stack pointer all at once.
**Flags affected:** N, Z

```asm
        .cpu 6510x
        las $1000,y     ; A, X, S := ($1000+Y) and (current S)
```

Like `TAS` above, this overwrites the stack pointer as a side effect,
which affects every subsequent stack operation.

### KIL — Halt
**Operation:** stops the CPU dead. Nothing after this instruction ever
executes; only a hardware reset recovers. Also written `JAM`, `HLT`,
or `STP` in some other assemblers/references.
**Flags affected:** none (the CPU isn't running anymore to change them)

```asm
        .cpu 6510x
        kil             ; deliberately hang the CPU -- e.g. as a crude
                         ; "should never get here" guard during development
```

11 other opcode bytes ($12, $22, $32, $42, $52, $62, $72, $92, $B2,
$D2, $F2) do exactly the same thing; `c64asm` only ever assembles `$02`
for `KIL`, since all 12 encodings are functionally identical and a
disassembler, not this assembler, would be the tool that needs to
distinguish which one appeared in a given binary.

### NOP (extra addressing modes) — No Operation
**Operation:** identical in spirit to the documented `NOP` above:
fetches its operand, if the mode has one, and does nothing with it.
The NMOS 6502/6510 executes several additional opcode bytes this way,
across four addressing modes `NOP` doesn't documentedly support
(immediate, zero page, zero page,X, absolute, and absolute,X) — these
extend the same mnemonic's mode table rather than needing a distinct
name.
**Flags affected:** none

```asm
        .cpu 6510x
        nop #$00        ; fetches and discards an immediate byte
        nop $10         ; fetches and discards a zero-page byte
        nop $1000,x     ; fetches and discards an absolute,X byte
```

