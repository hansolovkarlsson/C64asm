; illegal.asm - a short showcase of three illegal/undocumented 6502/6510
; opcodes (LAX, SAX, DCP), each used the way real C64 programs actually
; use them -- not just "look, it assembles" but "here's the instruction
; or two it's replacing, and why that's worth it." See
; c64asm-reference.md section 13 and c64asm-opcode-reference.md's
; "Illegal / Undocumented Opcodes" section for the full reference.
;
; IMPORTANT: these are not standard 6502 instructions. They rely on
; behavior MOS never documented or guaranteed, though it's well-known
; and completely reliable on the NMOS chip (6510/8500/8502) real C64s
; and C128s use -- see the ".cpu" directive right below, which is
; mandatory before any of this assembles at all.
;
; What it does: prints a short banner, uses LAX to paint a colorful
; 4-row block of letters at the top-left of the screen, uses SAX to
; set the border/background colors from bitmasks a few times, then
; settles into a slow border-color cycle (using an ordinary nested
; delay) to stay visibly alive. A brief, separate DCP loop demonstrates
; counting down to a chosen nonzero value in one instruction -- the one
; thing plain DEC+BNE can't do by itself.

        .cpu 6510x      ; REQUIRED: without this, every illegal opcode
                           ; below is a plain assembly error, the same
                           ; as any unrecognized mnemonic -- see
                           ; c64asm-reference.md section 13

CHROUT  = $FFD2
SCREEN  = $0400
COLOR   = $D800
BORDER  = $D020
BACKGR  = $D021

counter = $fb            ; zero-page scratch byte for the DCP/delay loops

; ---------------------------------------------------------------------
; FILL_ROW: paints 16 screen positions starting at screen_addr/color_addr
; using pattern_tbl/palette_tbl -- see the LAX section below for what
; each instruction is doing and why.
; ---------------------------------------------------------------------
.macro FILL_ROW screen_addr, color_addr
        ldy #0
@row_loop:
        lax pattern_tbl,y       ; A := X := pattern_tbl[Y] -- one
                                   ; instruction instead of the usual
                                   ; "lda pattern_tbl,y" + "tax"
        sta \screen_addr,y       ; A -> this screen cell (a letter, since
                                   ; pattern_tbl holds 0-15 and screen
                                   ; codes 0-15 are 'A'-'P')
        lda palette_tbl,x        ; X -- just loaded by LAX above -- indexes
                                   ; a SECOND, unrelated table. This is the
                                   ; real payoff: no extra register
                                   ; shuffling was needed to go from "the
                                   ; byte I read" to "using it as an index"
                                   ; in the very next instruction
        sta \color_addr,y
        iny
        cpy #16
        bne @row_loop
.endmacro

        .basic

start:
        jsr print_intro

        ; === LAX: load the same byte into A and X in one instruction ===
        ; Paint four 16-character rows at the top-left of the screen.
        ; Each byte read from pattern_tbl becomes both the screen
        ; character (via A) and, for free, the index used to look up
        ; that pattern's color in palette_tbl (via X) -- see FILL_ROW
        ; above for the instruction-by-instruction breakdown.
        FILL_ROW SCREEN,    COLOR
        FILL_ROW SCREEN+40, COLOR+40
        FILL_ROW SCREEN+80, COLOR+80
        FILL_ROW SCREEN+120,COLOR+120

        ; === SAX: store (A and X) in a single instruction ===
        ; On a real 6502, AND only ever works against a memory operand
        ; or an immediate value -- there's no instruction that ANDs two
        ; registers together. To store "A and X" the standard way costs
        ; three instructions and a scratch byte (STX temp / AND temp /
        ; STA dest), and permanently overwrites A in the process. SAX
        ; does the whole thing in one instruction, and leaves A and X
        ; exactly as they were.
        lda #%00001111
        ldx #%00000110
        sax BORDER              ; BORDER := A and X ($06, blue) -- A and X
                                   ; are both still %00001111/%00000110
                                   ; afterward, untouched
        jsr short_pause

        lda #%11110000
        ldx #%00011001
        sax BORDER              ; BORDER := A and X ($10 and $19 -> $10)
        jsr short_pause

        lda #%01010101
        ldx #%00001110
        sax BACKGR              ; BACKGR := A and X ($55 and $0E -> $04)
        jsr short_pause

        lda #$00
        ldx #$00
        sax BORDER              ; back to black for both, the ordinary way
        sax BACKGR

        jsr dcp_showcase

; Settle into a slow, ordinary border-color cycle so the demo stays
; visibly alive -- same nested-delay idiom as hello.asm, deliberately
; NOT built from illegal opcodes, since this part doesn't need them.
flash_loop:
        inc BORDER
        jsr short_pause
        jmp flash_loop

; ---------------------------------------------------------------------
; print_intro: ordinary CHROUT string printing, exactly like hello.asm.
; ---------------------------------------------------------------------
print_intro:
        ldx #$00
@print_loop:
        lda intro_msg,x
        beq @done
        jsr CHROUT
        inx
        jmp @print_loop
@done:
        rts

; ---------------------------------------------------------------------
; dcp_showcase: === DCP: decrement memory AND compare with A, in one
; instruction ===
; DEC alone only ever sets the Z flag when its result is exactly zero
; -- so counting down to any OTHER specific value with plain DEC+BNE
; needs an extra CMP #target after every single DEC. DCP folds that
; comparison into the same instruction as the decrement, so counting
; down to an arbitrary target costs nothing extra per iteration.
;
; This loop counts counter down from $FF to $80 -- a nonzero target --
; entirely with dcp+bne, no separate CMP anywhere.
; ---------------------------------------------------------------------
dcp_showcase:
        lda #$ff
        sta counter
        lda #$80                ; the target -- try changing this to any
                                   ; other value and the loop still works,
                                   ; which is exactly what plain DEC+BNE
                                   ; can't offer on its own
@dcp_loop:
        dcp counter              ; counter := counter-1; compare A ($80)
                                   ; against the freshly decremented value
        bne @dcp_loop              ; loop until counter == $80 (127 times)
        rts

; ---------------------------------------------------------------------
; short_pause: an ordinary nested delay (not built from illegal
; opcodes) -- long enough that each color change above is clearly
; visible, same idiom as hello.asm's color_loop.
; ---------------------------------------------------------------------
short_pause:
        ldx #$00
@outer:
        ldy #$00
@inner:
        iny
        bne @inner
        dex
        bne @outer
        rts

intro_msg:
        .text "ILLEGAL OPCODE SHOWCASE"
        .byte 13, 13
        .text "WATCH THE TOP ROWS AND BORDER..."
        .byte 13, 0

; pattern_tbl: 0-15, used both as a screen character code (0-15 = the
; letters A-P in the default charset) and, via LAX, as the index into
; palette_tbl right below -- see FILL_ROW above.
pattern_tbl:
        .byte 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15

; palette_tbl: a color for each possible pattern_tbl value -- deliberately
; NOT in the same order as pattern_tbl, so the LAX-loaded X value is
; genuinely looking up something different, not just re-reading itself.
palette_tbl:
        .byte 6,14,3,5,13,10,9,8,15,2,1,7,11,12,4,0
