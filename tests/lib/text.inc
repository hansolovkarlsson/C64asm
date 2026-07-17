; text.inc - PETSCII text output.
;
; Provides:
;   print_msg   subroutine: prints a null-terminated PETSCII string
;   PRINT msg   macro: convenience wrapper around print_msg
;   CLS         macro: clears the screen
;
; Requires a 2-byte zero-page scratch location named str_ptr, defined
; by the including program (this library doesn't claim any zero-page
; addresses on its own, since which addresses are safe to use depends
; on what else the program has already claimed -- see
; c64-memory-reference.md's zero-page notes for the community-documented
; safe range this project's own demos use, $02-$0F and $FB-$FE).
;
; Text must already be in PETSCII form to print correctly -- use this
; assembler's own .text/.byte directives (which PETSCII-encode quoted
; strings automatically) to define the messages this prints, not raw
; ASCII from some other source.

.include "hardware.inc"

; Prints the null-terminated PETSCII string at the address given in A
; (low byte) / Y (high byte). Uses @-prefixed local labels (see
; c64asm-reference.md, "Local labels"), so this subroutine's own loop
; labels can never collide with anything elsewhere in the including
; program, no matter how many other things are also called "loop" or
; "done".
print_msg:
        sta str_ptr
        sty str_ptr+1
        ldy #$00
@loop:
        lda (str_ptr),y
        beq @done
        jsr CHROUT
        iny
        bne @loop
@done:
        rts

; PRINT msg -- expands to the load-address-into-A/Y-then-call pattern
; print_msg needs. msg must be a label (typically one defined with
; .text/.byte ending in a $00 terminator byte).
.macro PRINT msg
        lda #<\msg
        ldy #>\msg
        jsr print_msg
.endmacro

; CLS -- clears the screen. $93 is the PETSCII "clear screen" control
; code; CHROUT interprets it as a screen-editor command rather than
; printing a visible character.
.macro CLS
        lda #$93
        jsr CHROUT
.endmacro
