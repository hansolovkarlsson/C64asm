; editor.asm - a simple, one-screen text editor.
;
; Fills the whole 40x25 text screen as the document -- no scrolling,
; no file larger than one screenful, deliberately, as a first step:
; this is meant as the foundation load/save/directory listing get
; built on next, not the finished thing. Once those exist, saving is
; expected to mean "copy screen memory to a file, converting each
; screen code back to PETSCII," and loading the reverse -- which is
; exactly why this version writes directly to screen memory as the
; one and only copy of the document, rather than keeping a separate
; buffer that would need its own conversion step later.
;
; Controls:
;   any typable key       insert it at the cursor, advance right
;   RETURN                move to the start of the next line
;   DEL                    erase the character behind the cursor
;   cursor up/down/left/right   move without changing anything
;   F1                     quit
;
; Cursor keys and RETURN/DEL are read as their own single PETSCII
; control codes ($11/$91/$1D/$9D, $0D, $14) straight from the KERNAL's
; own keyboard buffer via GETIN ($FFE4) -- the same buffer the
; default keyboard IRQ handler already fills with properly debounced,
; repeat-handled characters, so this needs no keyboard scanning of its
; own the way lib/keyboard.inc's WASD-style game input does. GETIN
; returns 0 immediately if no key is waiting, which is what makes the
; plain busy-loop in main_loop's own wait-for-key step correct: there's
; nothing else this program needs to do between keypresses.
;
; A typed character arrives as PETSCII, but screen memory expects
; *screen codes* -- a different encoding for the same characters (see
; https://sta.c64.org/cbm64pettoscr.html for the complete mapping).
; For the printable range this editor actually accepts (PETSCII $20
; through $5F -- space, digits, common punctuation, '@', A-Z, and a
; few symbols), the rule is exactly two cases: $20-$3F needs no
; change at all, and $40-$5F becomes screen code $00-$1F by
; subtracting $40 -- see insert_char below.
;
; The cursor itself is drawn by setting bit 7 of whatever screen code
; is already at the cursor's position (screen codes 128-255 are the
; same characters as 0-127, just in reverse video), and cleared the
; same way before the cursor moves anywhere -- no color RAM involved,
; and no separate "what was really there" storage needed, since
; clearing bit 7 always recovers the original character exactly.
;
; Row-to-address lookup (screen_row_lo/hi below) avoids needing any
; multiplication at all for cursor positioning: computing row*40 at
; runtime isn't a small power-of-two shift lib/math.inc's own macros
; could do, and 25 rows is a small, fixed, known set worth just
; precomputing once instead.

CHROUT  = $FFD2
GETIN   = $FFE4

; --- lib/text.inc's own required zero page ---
str_ptr = $fb
cmp_ptr = $fd
kw_ptr  = $02

; screen_ptr always points at the cursor's own current screen memory
; cell -- recomputed by recompute_screen_ptr every time cursor_x/
; cursor_y change, from screen_row_lo/hi plus cursor_x.
screen_ptr = $f7

; cursor_x (0-39), cursor_y (0-24) -- plain RAM, not zero page, since
; nothing here needs indirect addressing on them specifically.
cursor_x = $033c
cursor_y = $033d

        .basic start

        .include "lib/text.inc"

start:
        CLS
        PRINT title_msg
        PRINT instructions_msg
        PRINT continue_msg
        jsr wait_for_key

        CLS
        lda #$00
        sta cursor_x
        sta cursor_y
        jsr recompute_screen_ptr

main_loop:
        jsr show_cursor
wait_key:
        jsr GETIN
        beq wait_key
        pha
        jsr hide_cursor
        pla

        cmp #$85                ; F1
        beq do_quit
        cmp #$0d                ; RETURN
        beq @do_return
        cmp #$14                ; DEL
        beq @do_delete
        cmp #$11                ; cursor down
        beq @do_down
        cmp #$91                ; cursor up
        beq @do_up
        cmp #$1d                ; cursor right
        beq @do_right
        cmp #$9d                ; cursor left
        beq @do_left
        cmp #$20
        bcc main_loop            ; < $20: an unhandled control code -- ignore
        cmp #$60
        bcs main_loop            ; >= $60: not in the range this editor accepts -- ignore
        jsr insert_char
        jmp main_loop

@do_return:
        jsr handle_return
        jmp main_loop
@do_delete:
        jsr handle_delete
        jmp main_loop
@do_down:
        jsr move_cursor_down
        jmp main_loop
@do_up:
        jsr move_cursor_up
        jmp main_loop
@do_right:
        jsr move_cursor_right
        jmp main_loop
@do_left:
        jsr move_cursor_left
        jmp main_loop

do_quit:
        CLS
        PRINT bye_msg
        rts

; Busy-waits for any key via GETIN, discarding which one it was --
; used only for "press any key to continue" at startup, not the main
; editing loop, which needs to know exactly which key arrived.
wait_for_key:
        jsr GETIN
        beq wait_for_key
        rts

; Recomputes screen_ptr from cursor_x/cursor_y. Call this after
; changing either one -- every cursor-movement routine below does.
recompute_screen_ptr:
        ldx cursor_y
        lda screen_row_lo,x
        clc
        adc cursor_x
        sta screen_ptr
        lda screen_row_hi,x
        adc #$00
        sta screen_ptr+1
        rts

show_cursor:
        ldy #$00
        lda (screen_ptr),y
        ora #$80
        sta (screen_ptr),y
        rts

hide_cursor:
        ldy #$00
        lda (screen_ptr),y
        and #$7f
        sta (screen_ptr),y
        rts

; A holds a PETSCII code already confirmed to be in $20-$5F (main_loop
; filters everything else out before calling this). Converts it to
; the matching screen code (see this file's own header comment for
; the two-case rule) and writes it at the cursor, then advances right.
insert_char:
        cmp #$40
        bcc @unchanged
        sec
        sbc #$40
@unchanged:
        ldy #$00
        sta (screen_ptr),y
        jsr move_cursor_right
        rts

; Moves to the start of the next line, unless already on the last
; row, in which case only the column resets -- there's no scrolling
; in this version (see this file's own header comment).
handle_return:
        lda #$00
        sta cursor_x
        lda cursor_y
        cmp #24
        beq @recompute
        inc cursor_y
@recompute:
        jmp recompute_screen_ptr

; Erases the character behind the cursor (wrapping to the end of the
; previous line if the cursor is at column 0) -- but only if there's
; actually something behind it; at the very first cell (0,0), DEL
; does nothing, matching ordinary backspace behavior rather than
; erasing the character the cursor happens to be sitting on.
handle_delete:
        lda cursor_x
        bne @can_delete
        lda cursor_y
        beq @nothing_to_delete
@can_delete:
        jsr move_cursor_left
        lda #$20
        ldy #$00
        sta (screen_ptr),y
@nothing_to_delete:
        rts

; Each move_cursor_* routine leaves screen_ptr correctly pointing at
; the (possibly unchanged) cursor position either way -- no separate
; recompute needed afterward.
move_cursor_right:
        lda cursor_x
        cmp #39
        bne @same_row
        lda cursor_y
        cmp #24
        bne @next_row
        rts                      ; already at the very last cell -- stay put
@next_row:
        lda #$00
        sta cursor_x
        inc cursor_y
        jmp recompute_screen_ptr
@same_row:
        inc cursor_x
        jmp recompute_screen_ptr

move_cursor_left:
        lda cursor_x
        bne @same_row
        lda cursor_y
        beq @at_origin           ; already at (0,0) -- stay put
        dec cursor_y
        lda #39
        sta cursor_x
        jmp recompute_screen_ptr
@at_origin:
        rts
@same_row:
        dec cursor_x
        jmp recompute_screen_ptr

move_cursor_down:
        lda cursor_y
        cmp #24
        beq @no_move
        inc cursor_y
        jmp recompute_screen_ptr
@no_move:
        rts

move_cursor_up:
        lda cursor_y
        beq @no_move
        dec cursor_y
        jmp recompute_screen_ptr
@no_move:
        rts

title_msg:
        .text "SIMPLE TEXT EDITOR", 13, 0
instructions_msg:
        .text "CURSOR KEYS TO MOVE, DEL TO ERASE, F1 TO QUIT.", 13, 0
continue_msg:
        .text "PRESS ANY KEY TO START...", 13, 0
bye_msg:
        .text "GOODBYE", 13, 0

; Row N's screen memory starts at $0400 + N*40 -- precomputed here
; rather than multiplying by 40 at runtime (not a power of two, and
; not one of lib/math.inc's own small non-power-of-two multipliers
; either), since there are only ever 25 possible rows to begin with.
screen_row_lo:
        .byte $00, $28, $50, $78, $A0, $C8, $F0, $18
        .byte $40, $68, $90, $B8, $E0, $08, $30, $58
        .byte $80, $A8, $D0, $F8, $20, $48, $70, $98
        .byte $C0
screen_row_hi:
        .byte $04, $04, $04, $04, $04, $04, $04, $05
        .byte $05, $05, $05, $05, $05, $06, $06, $06
        .byte $06, $06, $06, $06, $07, $07, $07, $07
        .byte $07
