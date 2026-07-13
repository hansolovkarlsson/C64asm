# C64 Memory Map & Hardware Reference

A practical reference to the Commodore 64's most useful memory-mapped
hardware: the screen and color RAM, the VIC-II graphics chip (border and
background colors, the different graphics modes, sprites), the SID sound
chip, and joystick input via CIA1 — with worked `c64asm` examples for
each. Every code sample in this document has been assembled successfully
with `c64asm` before being included here.

---

## 1. Memory map overview

| Range | Contents |
|---|---|
| `$0000`–`$00FF` | Zero page (fast-access variables and pointers; see §8) |
| `$0100`–`$01FF` | The 6502 hardware stack |
| `$0200`–`$03FF` | KERNAL/BASIC workspace, cassette buffer |
| `$0400`–`$07E7` | Default screen (text/character) memory, 40×25 = 1000 bytes |
| `$07F8`–`$07FF` | Default sprite pointer table (see §4) |
| `$0800`–`$9FFF` | BASIC program area / general-purpose RAM |
| `$A000`–`$BFFF` | BASIC ROM (RAM underneath, bank-switchable) |
| `$C000`–`$CFFF` | Free RAM |
| `$D000`–`$D3FF` | VIC-II graphics chip registers (§3, §4) |
| `$D400`–`$D7FF` | SID sound chip registers (§5) |
| `$D800`–`$DBE7` | Color RAM, 1000 nibbles (one per screen cell, §2) |
| `$DC00`–`$DCFF` | CIA1 — keyboard, joystick ports, timers (§6) |
| `$DD00`–`$DDFF` | CIA2 — serial bus, VIC bank select, RS-232, timers |
| `$E000`–`$FFFF` | KERNAL ROM (RAM underneath, bank-switchable; §7) |

The `$A000`–`$BFFF`, `$D000`–`$DFFF`, and `$E000`–`$FFFF` regions are
bank-switched by the processor port at `$0001`: on a freshly reset
machine you see BASIC ROM, I/O registers, and KERNAL ROM respectively,
which is what every example in this document assumes. (Switching those
regions to plain RAM is an advanced technique for things like custom
KERNAL replacements — outside the scope of this reference.)

---

## 2. Screen and color memory

The default text screen is 40 columns × 25 rows starting at `$0400`; the
cell for row *r*, column *c* is at `$0400 + r*40 + c`. Each screen byte
holds a **screen code**, not a PETSCII code — these are different
numbering schemes, and mixing them up is a very common source of "why is
my text garbage" bugs.

| Character | PETSCII (what `CHROUT`/`.text` use) | Screen code (what you POKE into `$0400`) |
|---|---|---|
| `@` | `$40` | `$00` |
| `A`–`Z` | `$41`–`$5A` | `$01`–`$1A` |
| `0`–`9` | `$30`–`$39` | `$30`–`$39` (same) |
| space | `$20` | `$20` (same) |

Digits, space, and most punctuation happen to share the same code in
both systems; letters do not — screen code is PETSCII value minus `$40`
for the uppercase range. `c64asm`'s `.text`/`.asc`/`.byte "..."` all
produce **PETSCII**, intended for `CHROUT`; when you're writing screen
codes directly into `$0400` (bypassing `CHROUT` entirely, which is much
faster for things like game screens), work out the screen code by hand or
with the offset above.

Color RAM at `$D800`–`$DBE7` has the same 40×25 layout, one nibble (4
bits) per cell — the top nibble is unused/undefined. Values `0`–`15`
select the standard C64 palette (0=black, 1=white, 2=red, 3=cyan, 4=
purple, 5=green, 6=blue, 7=yellow, 8=orange, 9=brown, 10=light red, 11=
dark grey, 12=grey, 13=light green, 14=light blue, 15=light grey).

```asm
SCREEN = $0400
COLOR  = $d800
ROW    = 5
COL    = 10

        lda #8                       ; screen code for 'H' (PETSCII $48 - $40)
        sta SCREEN + ROW*40 + COL     ; poke it directly onto row 5, column 10
        lda #14                      ; light blue
        sta COLOR + ROW*40 + COL     ; and color that cell to match
```

---

## 3. VIC-II: border, background, and graphics modes

### Border and background colors

```asm
BORDER = $d020
BG0    = $d021   ; background color 0 (used by every text/bitmap mode)

        lda #0    ; black
        sta BORDER
        lda #6    ; blue
        sta BG0
```

### Screen control register 1 — `$D011`

| Bit | Name | Meaning |
|---|---|---|
| 7 | RST8 | Bit 8 of the raster compare value (pairs with `$D012`) |
| 6 | ECM | Extended background color mode |
| 5 | BMM | Bitmap mode |
| 4 | DEN | Display enable (0 blanks the screen entirely) |
| 3 | RSEL | 1 = 25 rows, 0 = 24 rows |
| 2–0 | YSCROLL | Vertical fine scroll, 0–7 pixels |

### Screen control register 2 — `$D016`

| Bit | Name | Meaning |
|---|---|---|
| 4 | MCM | Multicolor mode |
| 3 | CSEL | 1 = 40 columns, 0 = 38 columns |
| 2–0 | XSCROLL | Horizontal fine scroll, 0–7 pixels |

### Memory pointers — `$D018`

| Bits | Meaning |
|---|---|
| 7–4 | Screen memory pointer: address = (value × `$400`), relative to the current 16K VIC bank |
| 3–1 | Character memory pointer (text modes) or bitmap pointer bit 3 only (bitmap modes): address = (value × `$800`) |
| 0 | Unused |

The power-on default, `$D018 = %00010100` (screen at `$0400`, characters
at `$1000`), is what every example below assumes; most simple programs
never need to touch this register.

### The five graphics modes

Combining ECM, BMM, and MCM selects one of five modes:

| ECM | BMM | MCM | Mode |
|---|---|---|---|
| 0 | 0 | 0 | Standard text mode |
| 0 | 0 | 1 | Multicolor text mode |
| 1 | 0 | 0 | Extended background color text mode |
| 0 | 1 | 0 | Standard (hi-res) bitmap mode |
| 0 | 1 | 1 | Multicolor bitmap mode |
| 1 | 1 | * | Invalid — produces a blank/black screen |

**Standard text mode** is the default (§2's example already demonstrates
it). The other four:

#### Multicolor text mode

Each character can use up to 4 colors, at half the usual horizontal
resolution. Color RAM's bit 3 decides, per character, whether that cell
is rendered in multicolor: if set, the cell uses `$D021`/`$D022`/`$D023`
plus color RAM's low 3 bits (0–7) as a fourth color; if clear, the cell
renders as an ordinary 2-color character using `$D021` and color RAM's
low 3 bits, same as standard text mode.

```asm
MCM_BIT = $d016
BG1     = $d022
BG2     = $d023
COLOR   = $d800

        lda MCM_BIT
        ora #%00010000   ; set MCM without disturbing XSCROLL/CSEL
        sta MCM_BIT

        lda #6            ; background color 1 = blue
        sta BG1
        lda #14           ; background color 2 = light blue
        sta BG2

        lda #%00001001    ; bit3 set = multicolor cell, low 3 bits = color 1
        sta COLOR          ; make the top-left character multicolor
```

#### Extended background color (ECM) text mode

Trades character-set range (only 64 of the normal 256 characters remain
selectable) for 4 separate background colors, chosen per character by
bits 6–7 of the *screen* byte (not color RAM) at that position.

```asm
        lda $d011
        ora #%01000000   ; set ECM
        sta $d011
        lda #0
        sta $d021        ; background color for screen-byte bits 6-7 = 00
        lda #2
        sta $d022        ; background color for bits 6-7 = 01
        lda #5
        sta $d023        ; background color for bits 6-7 = 10
        lda #7
        sta $d024        ; background color for bits 6-7 = 11
```

#### Standard (hi-res) bitmap mode

320×200 pixels, 1 bit per pixel. The bitmap itself is 8000 bytes,
organized in the same 40×25 grid of 8×8-pixel cells as text mode — cell
(row, col) starts at `bitmap_base + (row*40 + col)*8`, one byte per pixel
row within the cell, bit 7 = leftmost pixel. The screen-memory area
(still `$0400` by default) doesn't hold characters in this mode — instead
each byte's high nibble is the color used for *set* bits in that cell and
the low nibble is the color for *clear* bits.

```asm
BITMAP = $2000    ; must be 8K-aligned and inside the current VIC bank
SCREEN = $0400
ptr    = $fb      ; a free zero-page pointer, see §8

        lda $d011
        ora #%00100000   ; set BMM
        sta $d011
        lda $d018
        and #%11110000   ; keep the screen pointer bits, clear the rest
        ora #%00001000   ; bitmap pointer bit 3 set -> bitmap at $2000
        sta $d018

; fill the whole 8K (32 pages of 256 bytes) bitmap area with a
; checkerboard-ish test pattern so you can see the mode is working:
        lda #<BITMAP
        sta ptr
        lda #>BITMAP
        sta ptr+1
        ldx #32
        lda #%10101010
fill_bitmap:
        ldy #$00
fill_bitmap_byte:
        sta (ptr),y
        iny
        bne fill_bitmap_byte
        inc ptr+1
        dex
        bne fill_bitmap

; fill the screen/color area (4 pages = 1024 bytes, covers all 1000
; used cells) with high nibble = white foreground, low nibble = blue background:
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldx #4
        lda #%00010110
fill_colors:
        ldy #$00
fill_colors_byte:
        sta (ptr),y
        iny
        bne fill_colors_byte
        inc ptr+1
        dex
        bne fill_colors
```

#### Multicolor bitmap mode

160×200 effective resolution (each "pixel" is 2 bits wide), 4 colors per
cell: `%00`=`$D021`, `%01`=screen byte's high nibble, `%10`=screen byte's
low nibble, `%11`=color RAM's low 4 bits. Set it up the same way as
standard bitmap mode, but also set MCM:

```asm
        lda $d011
        ora #%00100000   ; BMM
        sta $d011
        lda $d016
        ora #%00010000   ; MCM
        sta $d016
```

---

## 4. Sprites

### Sprite registers

| Address | Purpose |
|---|---|
| `$D000`/`$D001` … `$D00E`/`$D00F` | X/Y position of sprites 0–7 (X is the low 8 bits; see `$D010` for bit 8) |
| `$D010` | Bit *n* = bit 8 of sprite *n*'s X position, letting sprites reach X=0–511 |
| `$D015` | Bit *n* = 1 enables sprite *n* |
| `$D017` | Bit *n* = 1 doubles sprite *n*'s height (Y expand) |
| `$D01B` | Bit *n* = 1 puts sprite *n* behind background graphics instead of in front |
| `$D01C` | Bit *n* = 1 makes sprite *n* multicolor |
| `$D01D` | Bit *n* = 1 doubles sprite *n*'s width (X expand) |
| `$D01E` | Sprite-sprite collision, one bit per sprite; **reading it clears it** |
| `$D01F` | Sprite-background collision, one bit per sprite; **reading it clears it** |
| `$D025` | Shared multicolor color 1 (used by every multicolor sprite) |
| `$D026` | Shared multicolor color 2 (used by every multicolor sprite) |
| `$D027`–`$D02E` | Individual color of sprites 0–7 |

### Sprite data and pointers

A sprite is 24×21 pixels, 1 bit per pixel, stored as 3 bytes per row ×
21 rows = 63 bytes; the data block must start on a 64-byte boundary
within the current VIC bank (so an extra padding byte is conventional).
Which 64-byte block each sprite uses is chosen through a **sprite
pointer**: the last 8 bytes of the current screen memory (`$07F8`–`$07FF`
for the default screen at `$0400`), one per sprite, holding
`data_address / 64`.

```asm
SCREEN      = $0400
SPRITE_PTR0 = SCREEN + $3f8   ; = $07F8, sprite 0's pointer slot
SPRITE_DATA = $2000            ; 64-byte aligned, inside the VIC bank

        lda #(SPRITE_DATA / 64)
        sta SPRITE_PTR0

        lda #100
        sta $d000        ; sprite 0 X position
        lda #100
        sta $d001        ; sprite 0 Y position
        lda #1
        sta $d027        ; sprite 0 color = white
        lda #%00000001
        sta $d015        ; enable sprite 0, all others stay disabled

sprite_data:
        .byte %00111100,%00000000,%00000000
        .byte %01111110,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %01111110,%00000000,%00000000
        .byte %00111100,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte $00                              ; padding to a 64-byte block
```

(`SPRITE_DATA` is defined as a constant address here for clarity — in a
real program you'd normally place `sprite_data:` itself at a 64-byte
boundary with `.org`/`* =` and point `SPRITE_PTR0` at that label instead
of a separately chosen address, so the data and the pointer can't drift
out of sync.)

### Positioning a sprite beyond X=255

```asm
        lda $d010
        ora #%00000001    ; set sprite 0's X MSB
        sta $d010
        lda #44
        sta $d000         ; X = 256 + 44 = 300
```

### Multicolor sprite

```asm
        lda $d01c
        ora #%00000001    ; enable multicolor for sprite 0
        sta $d01c
        lda #2
        sta $d025         ; shared multicolor 1 = red
        lda #7
        sta $d026         ; shared multicolor 2 = yellow
```

### Collision detection

```asm
        lda $d01e         ; sprite-sprite collision bits (reading clears them)
        beq no_collision
        and #%00000001    ; did sprite 0 collide with anything?
        beq no_collision
        jsr handle_hit
no_collision:
```

---

## 5. Sound (SID)

The SID chip has three independent voices. Voice 1's registers start at
`$D400`; voice 2 and voice 3 use the identical layout, offset by 7 and 14
bytes respectively.

| Offset from voice base | Purpose |
|---|---|
| +0/+1 | Frequency, low/high byte (16-bit) |
| +2/+3 | Pulse width, low byte / low nibble of high byte (12-bit, pulse waveform only) |
| +4 | Control register: waveform select + gate (see below) |
| +5 | Attack (high nibble) / Decay (low nibble) |
| +6 | Sustain (high nibble) / Release (low nibble) |

Control register (offset +4) bits:

| Bit | Meaning |
|---|---|
| 7 | Noise waveform |
| 6 | Pulse waveform |
| 5 | Sawtooth waveform |
| 4 | Triangle waveform |
| 3 | Test (disables the oscillator while set) |
| 2 | Ring modulation |
| 1 | Sync |
| 0 | Gate — 1 starts the attack/decay/sustain envelope, 0 begins release |

Chip-wide registers:

| Address | Purpose |
|---|---|
| `$D415`/`$D416` | Filter cutoff frequency (11-bit) |
| `$D417` | Filter resonance (high nibble) / which voices are filtered (low 3 bits) |
| `$D418` | Bits 0–3: master volume (0–15). Bits 4–6: filter mode (low/band/high pass). Bit 7: disconnect voice 3's output entirely |

### Playing a single note

```asm
VOICE1_FREQ_LO = $d400
VOICE1_FREQ_HI = $d401
VOICE1_AD      = $d405
VOICE1_SR      = $d406
VOICE1_CTRL    = $d404
VOLUME         = $d418

        lda #$00
        sta VOICE1_CTRL   ; make sure the voice is off while we set it up

        lda #$09          ; attack = 0 (fast), decay = 9 (medium)
        sta VOICE1_AD
        lda #$00          ; sustain = 0, release = 0 (stops as soon as gated off)
        sta VOICE1_SR

        lda #$0f
        sta VOLUME        ; full master volume, no filter

        lda #$25
        sta VOICE1_FREQ_LO
        lda #$11
        sta VOICE1_FREQ_HI   ; $1125 -> roughly middle C

        lda #%00010001     ; triangle waveform, gate on: starts the note
        sta VOICE1_CTRL

        ldx #$00           ; hold the note for a while
delay_outer:
        ldy #$00
delay_inner:
        iny
        bne delay_inner
        dex
        bne delay_outer

        lda #%00010000     ; same waveform, gate off: starts the release
        sta VOICE1_CTRL
```

---

## 6. Joystick input (CIA1)

Joystick port 2 is read from CIA1's data port A. Each direction/fire bit
is **active low** — 0 means pressed, 1 means released — and multiple bits
can be low at once (e.g. moving diagonally).

| Bit | Meaning when 0 |
|---|---|
| 0 | Up |
| 1 | Down |
| 2 | Left |
| 3 | Right |
| 4 | Fire button |

(Port 1, at `$DC01`, reads the same way, but shares wiring with the
keyboard matrix scan — port 2 at `$DC00` is the one to use if you just
want simple, unambiguous joystick input.)

```asm
JOY2   = $dc00
SPRITE_X = $d000
SPRITE_Y = $d001

        lda JOY2
        and #%00000001
        bne not_up
        dec SPRITE_Y
not_up:
        lda JOY2
        and #%00000010
        bne not_down
        inc SPRITE_Y
not_down:
        lda JOY2
        and #%00000100
        bne not_left
        dec SPRITE_X
not_left:
        lda JOY2
        and #%00001000
        bne not_right
        inc SPRITE_X
not_right:
        lda JOY2
        and #%00010000
        bne not_fire
        jsr handle_fire_button
not_fire:
```

---

## 7. Common KERNAL routines

These live in KERNAL ROM and are called with `JSR` like any subroutine;
the KERNAL preserves the X and Y registers across all of them unless
otherwise noted.

| Address | Name | Effect |
|---|---|---|
| `$FFD2` | CHROUT | Print the PETSCII character in `A` to the current output device (the screen, by default) |
| `$FFCF` | CHRIN | Read one PETSCII character from the current input device into `A` |
| `$FFE4` | GETIN | Non-blocking read of one key from the keyboard buffer into `A` (`A`=0 if the buffer is empty) |
| `$FFF0` | PLOT | Get/set the cursor's row and column (carry flag selects get vs. set; see a full KERNAL reference for the calling convention) |
| `$FFE1` | STOP | Check whether the STOP key is currently held (sets the zero flag if so) |

```asm
CHROUT = $ffd2
GETIN  = $ffe4

        lda #$93          ; PETSCII "clear screen"
        jsr CHROUT

wait_key:
        jsr GETIN
        beq wait_key      ; loop until a key is actually pressed
        jsr CHROUT        ; echo it back out
```

---

## 8. Zero page notes

Zero page (`$00`–`$FF`) is valuable because zero-page addressing modes
are one byte shorter and one cycle faster than their absolute
equivalents, and because indirect-indexed/indexed-indirect addressing
(§ addressing modes in `c64asm-opcode-reference.md`) *requires* a
zero-page pointer. Much of it is used by the KERNAL and, if resident,
BASIC — but `$FB`–`$FE` (4 bytes) are conventionally left free for user
programs by both, which is why they're used as scratch pointers
throughout this document and in `hello.asm`. If you've turned BASIC ROM
off (bank-switched it out) and aren't calling any KERNAL routines, a much
larger range is safe to repurpose — but for programs that still `SYS`
into KERNAL/BASIC-resident code, stick to `$FB`–`$FE` (or `$02`–`$8F`,
a wider conventionally-free range, with more care) unless you've checked
a detailed KERNAL memory map for what else lives there.
