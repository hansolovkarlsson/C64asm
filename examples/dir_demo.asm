; dir_demo.asm - standalone demo: lists the disk directory, and
; nothing else.
;
; Pulled out of editor.asm on purpose, to make it possible to test and
; debug the directory-listing code on its own, separate from
; everything else editor.asm does. If something's wrong with how this
; project reads a disk directory, this is the smallest program that
; can show it.
;
; Two real gaps found on review, fixed here (and worth porting back
; into editor.asm once confirmed): editor.asm's own directory code
; never checked whether OPEN actually succeeded, and had no way to
; stop the text-reading loop early if the stream ended before a $00
; terminator ever showed up. Neither gap was visible against
; mini6502.py's own virtual disk, since that simulation's OPEN always
; succeeds and its generated listing is always well-formed -- exactly
; the kind of thing that can look correct in testing and still fail
; against a real drive, or VICE with no disk image attached. Testing
; that gap now needed mini6502.py to grow a way to simulate a missing
; drive too (device_present); see that file's own _do_open comment.
;
; Uses SETLFS/SETNAM/OPEN/CHKIN/CLRCHN/CLOSE/READST and CHRIN's own
; file-redirected behavior -- see editor.asm's own header comment for
; the full reasoning behind these calling conventions and the
; directory listing's own byte format (a fake load address, then one
; BASIC-program-style "line" per entry: link pointer, a line number
; doubling as the block count, null-terminated text), which this
; follows exactly.

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

dir_link_lo = $033c
dir_link_hi = $033d
dir_num_lo  = $033e
dir_num_hi  = $033f
dec_lo      = $0340
dec_hi      = $0341
dec_digit   = $0342
dec_started = $0343

        .basic start

        .include "lib/text.inc"

start:
        CLS
        PRINT title_msg

        lda #$01
        ldx #<dirname
        ldy #>dirname
        jsr SETNAM
        lda #$01                   ; logical file 1
        ldx #$08                   ; device 8
        ldy #$00                    ; secondary address -- the "$"
                                       ; directory request specifically
                                       ; needs 0 (matching BASIC's own
                                       ; LOAD"$",8), unlike an ordinary
                                       ; file read, where any value
                                       ; 2-14 works equally well; see
                                       ; dir_sa_test.asm and this
                                       ; project's own CHANGELOG.md for
                                       ; how this was actually confirmed,
                                       ; not assumed
        jsr SETLFS
        jsr OPEN
        bcs open_failed             ; real OPEN sets carry on failure --
                                       ; no drive, no disk, device not
                                       ; responding, and so on. Skipping
                                       ; this check is exactly how a
                                       ; failed OPEN turns into reading
                                       ; garbage or hanging instead of a
                                       ; clear error.

        ldx #$01
        jsr CHKIN

        jsr CHRIN                  ; skip the 2-byte fake load address
        jsr CHRIN

dir_loop:
        jsr CHRIN
        sta dir_link_lo
        jsr CHRIN
        sta dir_link_hi
        lda dir_link_lo
        ora dir_link_hi
        beq dir_done

        jsr CHRIN
        sta dir_num_lo
        jsr CHRIN
        sta dir_num_hi

        lda dir_num_lo
        ora dir_num_hi
        beq @text_only

        lda dir_num_lo
        sta dec_lo
        lda dir_num_hi
        sta dec_hi
        jsr print_decimal16
        lda #$20
        jsr CHROUT

@text_only:
@text_loop:
        jsr READST
        and #$40
        bne dir_done              ; the stream ended before a $00
                                      ; terminator showed up -- stop
                                      ; cleanly here instead of looping
                                      ; forever waiting for one
        jsr CHRIN
        beq @text_done
        jsr CHROUT
        jmp @text_loop
@text_done:
        lda #$92                  ; reverse off -- defensive: makes sure
                                      ; the disk name line's reverse-on
                                      ; code (see this file's own header
                                      ; comment) can never visually bleed
                                      ; into every line after it, whether
                                      ; or not a real drive already sends
                                      ; this itself; harmless on any line
                                      ; that wasn't in reverse video to
                                      ; begin with
        jsr CHROUT
        lda #13
        jsr CHROUT
        jmp dir_loop

dir_done:
        jsr CLRCHN
        lda #$01
        jsr CLOSE
        PRINT done_msg
        rts

open_failed:
        PRINT open_error_msg
        rts

; Prints the 16-bit value in dec_lo/dec_hi as decimal via CHROUT, with
; leading zeros suppressed but always at least one digit printed --
; see editor.asm's own copy of this routine for the full explanation;
; identical here, just duplicated so this file has no dependency on
; editor.asm itself.
print_decimal16:
        lda #$00
        sta dec_started
        ldx #$00
@digit_loop:
        lda #$00
        sta dec_digit
@subtract_loop:
        lda dec_hi
        cmp pow10_hi,x
        bcc @next_digit
        bne @do_subtract
        lda dec_lo
        cmp pow10_lo,x
        bcc @next_digit
@do_subtract:
        lda dec_lo
        sec
        sbc pow10_lo,x
        sta dec_lo
        lda dec_hi
        sbc pow10_hi,x
        sta dec_hi
        inc dec_digit
        jmp @subtract_loop
@next_digit:
        lda dec_digit
        bne @print_it
        lda dec_started
        bne @print_it
        cpx #4
        bne @skip_print
@print_it:
        lda #$01
        sta dec_started
        lda dec_digit
        clc
        adc #$30
        jsr CHROUT
@skip_print:
        inx
        cpx #5
        bne @digit_loop
        rts

pow10_lo: .byte <10000, <1000, <100, <10, <1
pow10_hi: .byte >10000, >1000, >100, >10, >1

title_msg:
        .text "DIRECTORY LISTING DEMO", 13, 13, 0
done_msg:
        .text 13, "(END OF LISTING)", 13, 0
open_error_msg:
        .text "COULDN'T OPEN THE DIRECTORY -- IS A DRIVE", 13
        .text "PRESENT AND A DISK ATTACHED?", 13, 0

dirname: .text "$"
