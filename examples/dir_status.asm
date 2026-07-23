; dir_status.asm - reads and prints the drive's own status message
; off the command channel (secondary address 15), and nothing else.
;
; Built after dir_raw.asm showed something that looked like the
; KERNAL happily reading garbage from the data channel without ever
; reporting an error via READST: "41 00" instead of the expected
; "01 04" load address, then a tight repeating 4-byte pattern (15 FF
; FF 1F) for the full 128-byte dump, never once flagged as an error.
; That combination points away from a parsing bug in this project's
; own code and toward the drive itself, or how it's actually
; responding -- READST reflects what the KERNAL *believes* happened,
; not necessarily the truth of what the drive said.
;
; The command channel is the standard, direct way to check this,
; independent of any directory-specific logic: every real disk
; operation reports its own result there, whether or not you asked
; for it, and a healthy drive answers with something recognizable --
; typically "73,CBM DOS V2.6 1541,00,00" (or a similar version string)
; right after power-on/reset, before any command has been sent at
; all. Reading it needs no filename -- OPEN 15,8,15 with nothing after
; the 15 is exactly this.
;
; If this prints a sensible status message, the drive is there and
; responding, which would point back at this project's own directory-
; reading code (or how "$" specifically is being requested) as the
; actual problem. If this ALSO hangs, prints garbage, or reports OPEN
; failure, the issue is upstream of anything this project's code does
; at all -- the drive, the device number, or how it's attached.

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

        .basic start

        .include "lib/text.inc"

start:
        CLS
        PRINT title_msg

        lda #$00                   ; no filename needed for the
                                       ; command channel
        ldx #<empty_name
        ldy #>empty_name
        jsr SETNAM
        lda #$0f                   ; logical file 15
        ldx #$08                   ; device 8
        ldy #$0f                    ; secondary address 15 = command
                                       ; channel
        jsr SETLFS
        jsr OPEN
        bcc open_ok

        pha
        PRINT open_failed_msg
        pla
        jsr print_hex_byte
        lda #13
        jsr CHROUT
        rts

open_ok:
        PRINT open_ok_msg

        ldx #$0f
        jsr CHKIN

        lda #$00
        sta byte_count

read_loop:
        jsr READST
        beq @read_one
        jmp read_done

@read_one:
        jsr CHRIN
        jsr CHROUT              ; print the status message as text --
                                    ; it's ordinary PETSCII digits,
                                    ; commas, and letters, meant to be
                                    ; read directly

        inc byte_count
        lda byte_count
        cmp #64                  ; a real status message is short;
        bne read_loop              ; cap well above what's expected,
                                       ; same defensive reasoning as
                                       ; dir_raw.asm's own 128-byte cap

        PRINT stopped_cap_msg

read_done:
        jsr CLRCHN
        lda #$0f
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
        .text "DRIVE STATUS CHECK", 13, 13, 0
open_ok_msg:
        .text "OPEN (COMMAND CHANNEL): CARRY CLEAR (SUCCESS)", 13
        .text "STATUS MESSAGE FOLLOWS:", 13, 0
open_failed_msg:
        .text "OPEN (COMMAND CHANNEL): CARRY SET (FAILED)."
        .text " ERROR CODE: ", 0
stopped_cap_msg:
        .text 13, "STOPPED -- REACHED 64 BYTES", 13, 0
done_msg:
        .text "DONE", 13, 0

empty_name:
