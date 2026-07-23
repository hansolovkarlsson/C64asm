; dir_sa_test.asm - opens "$" (the directory) with three different
; secondary addresses in turn -- 0, 2, and 4 -- and prints the first
; 8 bytes each one returns, so they can be compared directly.
;
; Built after dir_status.asm confirmed the drive itself is healthy
; ("00, OK,00,00"), which rules out "no drive" or "drive malfunction"
; as the cause of dir_raw.asm's garbage output. That leaves how the
; directory read is actually being requested as the more likely
; culprit -- and one detail was never actually confirmed rather than
; assumed: dir_demo.asm/dir_raw.asm used secondary address 4, based on
; the general rule that any value 2-14 is a valid data channel for an
; ordinary file. That's true for reading a normal file, but BASIC's
; own LOAD"$",8 uses secondary address 0 specifically, and this
; project never actually confirmed the "$" directory request behaves
; the same way as an ordinary read regardless of which secondary
; address is used -- it was assumed, not verified, which is exactly
; the kind of gap that's caused real bugs earlier in this project.
;
; A well-formed response starts 01 04 (the fake load address), then a
; nonzero link pointer, then 00 00 (line number 0, the disk name
; line), then 12 22 (reverse-on, open quote). Whichever secondary
; address produces that is the one to use; whichever doesn't points
; at exactly which byte the response actually starts diverging from
; expectation.

CHROUT = $FFD2
SETLFS = $FFBA
SETNAM = $FFBD
OPEN   = $FFC0
CLOSE  = $FFC3
CHKIN  = $FFC6
CLRCHN = $FFCC
CHRIN  = $FFCF
READST = $FFB7

; --- lib/text.inc's own required zero page ---
str_ptr = $fb
cmp_ptr = $fd
kw_ptr  = $02

sa_value    = $033c   ; which secondary address the current attempt uses
lfn_value   = $033d   ; and which logical file number
byte_index  = $033e

        .basic start

        .include "lib/text.inc"

start:
        CLS
        PRINT title_msg

        lda #$00
        ldx #$01
        jsr try_sa
        lda #$02
        ldx #$02
        jsr try_sa
        lda #$04
        ldx #$03
        jsr try_sa

        PRINT done_msg
        rts

; A = secondary address to try, X = logical file number to use for
; this attempt (kept distinct across the three calls so nothing from
; one attempt can be mistaken for state left over from another).
try_sa:
        sta sa_value
        stx lfn_value

        PRINT trying_msg
        lda sa_value
        jsr print_hex_byte
        lda #13
        jsr CHROUT

        lda #$01
        ldx #<dirname
        ldy #>dirname
        jsr SETNAM
        lda lfn_value
        ldx #$08
        ldy sa_value
        jsr SETLFS
        jsr OPEN
        bcc @open_ok

        pha
        PRINT open_failed_msg
        pla
        jsr print_hex_byte
        lda #13
        jsr CHROUT
        rts

@open_ok:
        ldx lfn_value
        jsr CHKIN

        lda #$00
        sta byte_index
@read_loop:
        jsr READST
        bne @stopped
        jsr CHRIN
        jsr print_hex_byte
        lda #$20
        jsr CHROUT
        inc byte_index
        lda byte_index
        cmp #$08
        bne @read_loop
        jmp @close_up

@stopped:
        PRINT eof_msg

@close_up:
        lda #13
        jsr CHROUT
        jsr CLRCHN
        lda lfn_value
        jsr CLOSE
        lda #13
        jsr CHROUT
        rts

; Prints A as two hex digits via CHROUT.
print_hex_byte:
        pha
        lsr a
        lsr a
        lsr a
        lsr a
        jsr print_hex_nibble
        pla
        and #$0f
        jsr print_hex_nibble
        rts

print_hex_nibble:
        cmp #10
        bcc @digit
        clc
        adc #$07
@digit:
        clc
        adc #$30
        jsr CHROUT
        rts

title_msg:
        .text "SECONDARY ADDRESS COMPARISON", 13, 13, 0
trying_msg:
        .text "SA=$", 0
open_failed_msg:
        .text "OPEN FAILED, ERROR CODE: ", 0
eof_msg:
        .text "(EOF/ERROR BEFORE 8 BYTES)", 0
done_msg:
        .text 13, "DONE -- COMPARE THE THREE ROWS ABOVE", 13, 0

dirname: .text "$"
