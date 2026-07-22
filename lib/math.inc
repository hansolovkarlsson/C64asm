; math.inc - small 6502 arithmetic idioms that don't belong to any one
; hardware subsystem. Right now, multiply and divide by a small
; constant, for indexing into an array of same-sized records -- most
; commonly an array of .struct (c64asm-reference.md section 10)
; records, where that section's own "Indexing an array of records"
; example uses these macros directly. See that section for the full
; worked pattern this pairs with, including how .assert (section 11)
; guards the choice of which macro below matches a given struct's
; size.
;
; The 6502 has no multiply or divide instruction. Multiplying (or
; dividing) A by a small power-of-two constant N is exactly log2(N)
; left shifts (or right shifts) -- MULT_2/MULT_4/MULT_8/MULT_16 and
; their DIV_N counterparts below are just that, spelled out for the
; shift counts a realistic .struct size actually needs.
;
; MULT_3/MULT_5/MULT_6/MULT_7/MULT_9/MULT_10/MULT_12 cover the
; smallest non-power-of-two sizes a realistic small struct is likely
; to come out to (a .struct that comes out to, say, 6 bytes -- like
; this file's own worked example below -- couldn't be multiplied by
; anything here before these existed). Each is a shift-and-add (or,
; for MULT_7, shift-and-subtract) built from the same power-of-two
; shifts MULT_2/4/8 already use -- MULT_6, for instance, is (A*4) +
; (A*2). Extending this file with a multiplier not listed here (11,
; 13, and so on) is the same technique: decompose the multiplier into
; a sum (or difference) of two powers of two, and combine two shifted
; copies of A the same way MULT_6 or MULT_7 below do.
;
; Unlike the power-of-two macros, THESE NEED ONE BYTE OF ZERO PAGE --
; combining two shifted copies of A means having both of them
; available at once, and the 6502 has no register-to-register
; arithmetic at all (ADC/SBC only ever read their second operand from
; memory, immediate, or an addressing mode -- never from another
; register directly), so one of the two copies has to live somewhere
; addressable while the other stays in A. Declare a 1-byte zero-page
; location named mult_scratch before .include-ing this file if you use
; any of MULT_3/5/6/7/9/10/12 -- MULT_2/4/8/16 and every DIV_N still
; need nothing. There's no .error guard for this (unlike this
; project's other library files' zero-page requirements): whether
; mult_scratch is actually needed depends on which macros in this one
; file you use, not on including the file at all, so a plain
; "Undefined symbol 'mult_scratch'" the first time an unguarded
; MULT_3/5/6/7/9/10/12 is used is what points you at this paragraph.
;
; None of the macros below are safe to use with A already holding
; something you need afterward -- like ASL/LSR themselves, they
; overwrite A (and, for the non-power-of-two ones, mult_scratch) as
; part of computing the result.
;
; Every macro here needs no .include of its own, beyond the zero-page
; declaration just described for the non-power-of-two ones.
;
; MULT_N is an 8-bit operation: A wraps silently, the same as ASL
; always does, if the true result would exceed 255. Fine for small
; arrays -- this project's own adventure.asm indexes a 5-room,
; 4-byte-per-room table (worst case 5*4 = 20) well within range -- but
; check your own array's worst case (record count * record size)
; against 255 before reusing these for something bigger; past that
; point this needs real 16-bit multiplication, which is a different,
; bigger thing than what a handful of ASL/ADC instructions can do.
;
; DIV_N (power-of-two divisors only -- see below) is plain unsigned
; integer division -- LSR shifts a 0 in from the top and discards
; whatever shifts out the bottom, the same as any division that
; truncates rather than rounds (7 DIV_2 is 3, not 4 or 3.5). The
; typical use is the reverse of MULT_N's: given a byte offset into an
; array of same-sized records, which record is that (offset DIV_N
; record_size, matching whichever MULT_N built the offset in the
; first place).
;
; There's no non-power-of-two DIV_N here, deliberately: unlike
; multiplication, dividing by an arbitrary small constant isn't a
; short shift-and-add away -- it needs either a real division
; algorithm (a bit-at-a-time restoring division, typically) or a
; reciprocal-multiplication trick that itself needs a 16-bit
; intermediate result, either of which is a genuinely bigger, slower
; piece of code than anything else in this file, not a natural
; extension of MULT_6's own three-instruction pattern. If you need
; this, a precomputed lookup table (record index -> byte offset, or
; the reverse) is usually both simpler and faster for the small,
; fixed array sizes this file's other macros are meant for.

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

; The macros below need mult_scratch (1 byte of zero page) declared
; before this file is .include'd -- see this file's own header
; comment for why.

; A := A * 3  (A*2 + A)
.macro MULT_3
        sta mult_scratch
        asl a
        clc
        adc mult_scratch
.endmacro

; A := A * 5  (A*4 + A)
.macro MULT_5
        sta mult_scratch
        asl a
        asl a
        clc
        adc mult_scratch
.endmacro

; A := A * 6  (A*4 + A*2)
.macro MULT_6
        asl a
        sta mult_scratch
        asl a
        clc
        adc mult_scratch
.endmacro

; A := A * 7  (A*8 - A)
.macro MULT_7
        sta mult_scratch
        asl a
        asl a
        asl a
        sec
        sbc mult_scratch
.endmacro

; A := A * 9  (A*8 + A)
.macro MULT_9
        sta mult_scratch
        asl a
        asl a
        asl a
        clc
        adc mult_scratch
.endmacro

; A := A * 10  (A*8 + A*2)
.macro MULT_10
        asl a
        sta mult_scratch
        asl a
        asl a
        clc
        adc mult_scratch
.endmacro

; A := A * 12  (A*8 + A*4)
.macro MULT_12
        asl a
        asl a
        sta mult_scratch
        asl a
        clc
        adc mult_scratch
.endmacro
