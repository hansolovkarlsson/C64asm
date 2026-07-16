; For ACME / DASM / Kick Assembler
; Compiles to a standard .prg file
; How to execute: Assemble the file, load the .prg into your emulator or real machine, and type SYS 49152

*=$c000          ; Starting address in RAM (Hex C000 = Decimal 49152)

BORDER = $d020   ; Memory address for the VIC-II border color

start:
    inc BORDER   ; Increment the border color value
    jmp start    ; Infinite loop back to 'start'
