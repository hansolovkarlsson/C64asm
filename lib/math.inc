; math.inc - small 6502 arithmetic idioms that don't belong to any one
; hardware subsystem. Right now, multiply and divide by a small
; power-of-two constant, for indexing into an array of same-sized
; records -- most commonly an array of .struct (c64asm-reference.md
; section 10) records, where that section's own "Indexing an array of
; records" example uses MULT_N directly. See that section for the full
; worked pattern this pairs with, including how .assert (section 11)
; guards the choice of which macro below matches a given struct's
; size.
;
; The 6502 has no multiply or divide instruction. Multiplying (or
; dividing) A by a small power-of-two constant N is exactly log2(N)
; left shifts (or right shifts) -- MULT_2/MULT_4/MULT_8/MULT_16 and
; their DIV_N counterparts below are just that, spelled out for the
; shift counts a realistic .struct size actually needs. None of these
; need a parameter for the shift count itself, since a macro can't
; loop on one (an arbitrary shift count would need '.repeat', and
; '.repeat' can't appear inside a '.macro' body -- see that
; directive's own "Rules and limitations"); pick the macro name
; matching the multiplier or divisor you actually need instead.
;
; Every macro here operates on the A register in place, touches no
; zero page, and needs no .include of its own -- the plainest thing a
; macro can be, just named repetition.
;
; MULT_N is an 8-bit operation: A wraps silently, the same as ASL
; always does, if the true result would exceed 255. Fine for small
; arrays -- this project's own adventure.asm indexes a 5-room,
; 4-byte-per-room table (worst case 5*4 = 20) well within range -- but
; check your own array's worst case (record count * record size)
; against 255 before reusing these for something bigger; past that
; point this needs real 16-bit multiplication, which is a different,
; bigger thing than what a handful of ASL instructions can do.
;
; DIV_N is plain unsigned integer division -- LSR shifts a 0 in from
; the top and discards whatever shifts out the bottom, the same as any
; division that truncates rather than rounds (7 DIV_2 is 3, not 4 or
; 3.5). The typical use is the reverse of MULT_N's: given a byte
; offset into an array of same-sized records, which record is that
; (offset DIV_N record_size, matching whichever MULT_N built the
; offset in the first place).
;
; Neither MULT_N nor DIV_N applies at all if the constant in question
; isn't a power of two -- a .struct that comes out to, say, 6 bytes
; (like c64asm-reference.md's own Room example) needs a different
; technique entirely (an explicit lookup table, or genuine multi-bit
; multiplication/division), not something this file provides.

; A := A * 2
.macro MULT_2
        asl a
.endmacro

; A := A * 4
.macro MULT_4
        asl a
        asl a
.endmacro

; A := A * 8
.macro MULT_8
        asl a
        asl a
        asl a
.endmacro

; A := A * 16
.macro MULT_16
        asl a
        asl a
        asl a
        asl a
.endmacro

; A := A / 2 (unsigned, truncating)
.macro DIV_2
        lsr a
.endmacro

; A := A / 4 (unsigned, truncating)
.macro DIV_4
        lsr a
        lsr a
.endmacro

; A := A / 8 (unsigned, truncating)
.macro DIV_8
        lsr a
        lsr a
        lsr a
.endmacro

; A := A / 16 (unsigned, truncating)
.macro DIV_16
        lsr a
        lsr a
        lsr a
        lsr a
.endmacro
