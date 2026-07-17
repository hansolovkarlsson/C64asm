; This file tests your ability to load data registers (LDA, LDX, LDY), 
; read a string array, and interface with the built-in C64 KERNAL subroutines.
; It prints text safely onto the text screen.

; How to execute: Assemble the file, load it, and execute with SYS 4096

; bug: prints out HELLO FROM ASSEMBLER!130

; Standard 6502 Assembly (ACME / KickASM)

*=$1000          ; Starting address in RAM (Decimal 4096)

CHROUT = $ffd2   ; C64 KERNAL routine to output a character to screen

init:
    ldx #$00     ; Clear X register (used as our string index pointer)

print_loop:
    lda message,x ; Load character from message + X offset
    beq done     ; If character is 0 (End of String), jump to 'done'
    jsr CHROUT   ; Call KERNAL to print the character in Acc
    inx          ; Increment X index
    jmp print_loop ; Repeat loop

done:
    rts          ; Return to BASIC

message:
    ; Text string in PETSCII screen codes, terminated with a 0
    .text "HELLO FROM ASSEMBLER!"
    .byte 13, 0 
