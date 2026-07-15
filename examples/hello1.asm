; hello.asm - prints a message using the KERNAL CHROUT routine.
; Assemble with:  bin/c64asm examples/hello.asm -o bin/hello.prg --listing bin/hello.lst

        .basic              ; emit "10 SYS ..." loader stub

CHROUT  = $FFD2

start:  ldx #0
loop:   lda text,x
        beq done
        jsr CHROUT
        inx
        jmp loop
done:   rts

text:   .text "HELLO, WORLD!"
        .byte 0
