; hello1.asm - prints a message using the library
; Assemble with:  bin/c64asm examples/hello.asm -o examples/hello.prg --listing examples/hello.lst

        .basic              ; emit "10 SYS ..." loader stub
        jmp start           ; .basic's SYS stub jumps right here.
                            ; .include files will load code after this point.

str_ptr = $fb           ; text.inc's required zero-page scratch

        .include "lib/text.inc"


start:  
        CLS
        PRINT text
        rts

text:   .text "HELLO, WORLD!"
        .byte 0

