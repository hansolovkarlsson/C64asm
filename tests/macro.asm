; testing new feature: macro

        .basic              ; emit "10 SYS ..." loader stub

str_ptr       = $fb   ; print_msg's string pointer

CHROUT  = $FFD2

.macro PRINT msg
        lda #<\msg
        ldy #>\msg
        jsr print_msg
.endmacro

        PRINT hello_msg
        PRINT goodbye_msg

done:   rts

; Prints the null-terminated string whose address is passed in A (low
; byte) / Y (high byte).
print_msg:
        sta str_ptr
        sty str_ptr+1
        ldy #$00
print_msg_loop:
        lda (str_ptr),y
        beq print_msg_done
        jsr CHROUT
        iny
        bne print_msg_loop
print_msg_done:
        rts


hello_msg:   .text "HELLO!"
        .byte 0

goodbye_msg:   .text "BYE!"
        .byte 0
