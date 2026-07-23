; dir_raw.asm - dumps the raw bytes from opening "$" (the disk
; directory), in hex, with no interpretation of what they mean.
;
; Built specifically to debug dir_demo.asm not working correctly on
; real hardware/VICE: dir_demo.asm's own output ("8192" then two pi
; characters then a hang) doesn't match its expected error message
; OR a well-formed directory listing, which means either OPEN itself
; is failing in a way dir_demo.asm's carry check isn't catching, or
; CHRIN is returning data dir_demo.asm's parsing logic is
; misinterpreting -- and there's no way to tell which from dir_demo's
; own output alone, since it only ever prints its *interpretation* of
; the bytes, never the bytes themselves.
;
; This prints three things dir_demo.asm never showed directly:
;   - whether OPEN's own carry flag came back set (failure) or clear
;     (success), and the error code in A if it failed
;   - the READST value on the very first check, before any CHRIN
;   - every byte actually read, as two hex digits each, sixteen per
;     row, until either 128 bytes have been read or READST reports
;     something (EOF or an error)
;
; Whatever this prints is the actual, unfiltered truth about what the
; KERNAL is doing here -- compare it against what a well-formed
; listing should look like (see below) to see exactly where reality
; and expectation diverge.
;
; A well-formed directory listing starts: 01 04 (fake load address),
; then a nonzero 2-byte link pointer, then 00 00 (line number 0, the
; disk name line), then 12 22 (reverse-on, open quote) followed by the
; disk name's own characters, then more text, ending that line with
; 00. If what actually comes back looks nothing like this from the
; very first bytes, the problem is upstream of any parsing logic
; entirely -- in OPEN, SETNAM, SETLFS, or how the drive itself is set
; up. If it looks correct for a while and then goes wrong partway
; through, that narrows things down considerably.

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

byte_count = $033c
col_count  = $033d

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
        ldy #$00                    ; secondary address -- must be 0
                                       ; for the "$" directory request
                                       ; specifically; see
                                       ; dir_sa_test.asm and this
                                       ; project's own CHANGELOG.md
        jsr SETLFS
        jsr OPEN
        bcc open_ok

        ; OPEN failed -- show that plainly, plus the error code real
        ; OPEN leaves in A when carry is set
        pha
        PRINT open_failed_msg
        pla
        jsr print_hex_byte
        lda #13
        jsr CHROUT
        rts

open_ok:
        PRINT open_ok_msg

        ldx #$01
        jsr CHKIN

        ; the READST value right here, before any CHRIN at all --
        ; should be $00 on a healthy channel
        jsr READST
        PRINT first_readst_msg
        jsr print_hex_byte
        lda #13
        jsr CHROUT
        lda #13
        jsr CHROUT

        lda #$00
        sta byte_count
        sta col_count

read_loop:
        jsr READST
        beq @read_one
        pha
        PRINT stopped_msg
        pla
        jsr print_hex_byte
        jmp read_done

@read_one:
        jsr CHRIN
        jsr print_hex_byte
        lda #$20
        jsr CHROUT

        inc col_count
        lda col_count
        cmp #16
        bne @no_newline
        lda #$00
        sta col_count
        lda #13
        jsr CHROUT
@no_newline:
        inc byte_count
        lda byte_count
        cmp #128
        bne read_loop

        PRINT stopped_128_msg

read_done:
        jsr CLRCHN
        lda #$01
        jsr CLOSE
        lda #13
        jsr CHROUT
        PRINT done_msg
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
        .text "RAW DIRECTORY BYTE DUMP", 13, 13, 0
open_ok_msg:
        .text "OPEN: CARRY CLEAR (SUCCESS)", 13, 0
open_failed_msg:
        .text "OPEN: CARRY SET (FAILED). ERROR CODE: ", 0
first_readst_msg:
        .text "READST BEFORE ANY CHRIN: ", 0
stopped_msg:
        .text 13, "STOPPED -- READST NONZERO: ", 0
stopped_128_msg:
        .text 13, "STOPPED -- REACHED 128 BYTES", 13, 0
done_msg:
        .text "DONE", 13, 0

dirname: .text "$"
