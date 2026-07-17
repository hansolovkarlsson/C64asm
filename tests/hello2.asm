; hello2.asm
;
; test print and input
;

        .basic              ; emit "10 SYS ..." loader stub
        jmp start           ; .basic's SYS stub jumps right here.
                            ; .include files will load code after this point.

str_ptr = $fb           ; text.inc's required zero-page scratch

        .include "lib/text.inc"
        .include "lib/input.inc"


start:  
        PRINT text
        CIA_KEYBOARD_SETUP
        
        rts

text:   .text "WHAT'S YOUR NAME? "
        .byte 0


