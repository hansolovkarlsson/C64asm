; sprites.asm - loads a 4-frame sprite animation from star_anim.bin, an
; external binary asset, via .incbin -- instead of hand-transcribing
; sprite data as .byte lines the way demo.asm's own (single-frame,
; static) star does.
;
; star_anim.bin is a compact "sprite sheet": four 63-byte hires sprite
; frames packed back-to-back with no padding (252 bytes total). This
; is exactly the shape a real sprite editor (SpritePad and similar
; tools) exports a multi-frame animation as -- .incbin's offset/length
; arguments (c64asm-reference.md section 11) pull each 63-byte frame
; out of that one file, at assembly time, with no manual byte
; transcription and no need to keep a hand-copied .byte table in sync
; every time the sheet changes in the editor.
;
; Each frame still needs its own 64-byte-aligned address once
; assembled (see graphics.inc's own note on this), which .incbin
; doesn't handle by itself -- that's what the four ".align 64" lines
; below are for, one per frame. The source *file* itself needs no
; padding or alignment at all; that's purely an assembled-output
; requirement.
;
; Controls: W/A/S/D moves the star around the screen, with the same
; border-stop behavior demo.asm's own star has; Q exits. The star
; cycles through its four frames on its own the whole time, small ->
; medium -> large -> medium -> small ..., independent of whether it's
; being moved.

CHROUT  = $FFD2
str_ptr = $fb
cmp_ptr = $02
kw_ptr  = $04
word_dest_ptr = $fb      ; input.inc's required zero-page scratch (its
                            ; extract_word is never actually called
                            ; here); safe to alias with str_ptr
key_scratch = $0a        ; keyboard.inc's required zero-page scratch
gfx_ptr = $fd             ; graphics.inc's required zero-page scratch
xpos = $06                ; the star's actual current position -- see
                             ; demo.asm's own header comment for why
                             ; these particular symbols (xpos, ypos,
                             ; xdir, ydir, XMIN/XMAX/YMIN/YMAX) are
                             ; required at all: graphics.inc's
                             ; sprite0_bounce_step code is assembled
                             ; unconditionally once graphics.inc is
                             ; included, even though this demo never
                             ; calls it, and its own code references
                             ; all nine of these regardless
ypos = $0b
xdir = $0c                  ; unused here, but still required
ydir = $0e                    ; likewise unused but required
XMIN = 24                 ; the star's real border-check bounds,
XMAX = 320                  ; reused as-is from demo.asm/bounce.asm's
YMIN = 50                     ; own already-proven values -- all three
YMAX = 229                      ; sprites are the same standard 24x21
                                   ; hires size, so the same bounds apply

        .basic start

        .include "lib/text.inc"
        .include "lib/input.inc"
        .include "lib/keyboard.inc"
        .include "lib/graphics.inc"

start:
        CIA_KEYBOARD_SETUP
        CLS
        PRINT welcome_msg
        PRINT instructions_msg
        PRINT continue_msg
        jsr wait_any_key         ; see demo.asm's own header comment --
                                    ; without this, there'd be no time to
                                    ; actually read the text above before
                                    ; the sprite setup below starts
                                    ; drawing over the screen

        SPRITE_INIT frame0, 7, 172, 140   ; color 7 = yellow; roughly
                                             ; centered within
                                             ; XMIN/XMAX/YMIN/YMAX

        lda #172
        sta xpos
        lda #0
        sta xpos+1
        lda #140
        sta ypos

        lda #0
        sta anim_frame
        sta anim_timer

main_loop:
        jsr wait_frame

        ; --- animate: advance to the next frame every 8 ticks ---
        inc anim_timer
        lda anim_timer
        cmp #8
        bne check_w
        lda #0
        sta anim_timer
        ldx anim_frame
        inx
        cpx #4
        bne @store_frame
        ldx #0
@store_frame:
        stx anim_frame
        lda frame_ptrs,x
        sta SPRITE_PTR0

        ; --- W/A/S/D movement, same border-stop pattern as demo.asm ---
check_w:
        READ_KEY KEY_W_COL, KEY_W_ROW
        beq check_s
        lda ypos
        cmp #YMIN
        beq check_s
        jsr move_up
check_s:
        READ_KEY KEY_S_COL, KEY_S_ROW
        beq check_a
        lda ypos
        cmp #YMAX
        beq check_a
        jsr move_down
check_a:
        READ_KEY KEY_A_COL, KEY_A_ROW
        beq check_d
        lda xpos
        cmp #<XMIN
        bne @do_move
        lda xpos+1
        cmp #>XMIN
        beq check_d
@do_move:
        jsr move_left
check_d:
        READ_KEY KEY_D_COL, KEY_D_ROW
        beq check_exit
        lda xpos
        cmp #<XMAX
        bne @do_move
        lda xpos+1
        cmp #>XMAX
        beq check_exit
@do_move:
        jsr move_right
check_exit:
        READ_KEY KEY_Q_COL, KEY_Q_ROW
        bne do_exit
        jmp main_loop
do_exit:
        lda #0
        sta SPRITE_ENABLE
        CLS
        PRINT bye_msg
        rts

; --- sprite movement helpers (identical to demo.asm's own) ---
move_left:
        lda xpos
        bne @dec_done
        dec xpos+1
@dec_done:
        dec xpos
        jmp update_sprite_x
move_right:
        inc xpos
        bne update_sprite_x
        inc xpos+1
        ; falls through to update_sprite_x
update_sprite_x:
        lda xpos
        sta SPRITE0_X
        lda xpos+1
        beq @msb_clear
        lda SPRITE_X_MSB
        ora #%00000001
        sta SPRITE_X_MSB
        rts
@msb_clear:
        lda SPRITE_X_MSB
        and #%11111110
        sta SPRITE_X_MSB
        rts

move_up:
        dec ypos
        jmp update_sprite_y
move_down:
        inc ypos
        ; falls through to update_sprite_y
update_sprite_y:
        lda ypos
        sta SPRITE0_Y
        rts

anim_frame:
        .byte 0
anim_timer:
        .byte 0

; frame_ptrs: each byte is a sprite-pointer value (address/64) --
; exactly what SPRITE_PTR0 expects -- for one of the four frames
; below. Computed here as ordinary expressions on each frame's own
; label, not hardcoded, so this stays correct even if the frames'
; actual addresses ever shift.
frame_ptrs:
        .byte frame0/64, frame1/64, frame2/64, frame3/64

welcome_msg:
        .text "SPRITE ANIMATION FROM AN EXTERNAL .BIN FILE"
        .byte 13, 0
instructions_msg:
        .text "USE W,A,S,D TO MOVE THE STAR. PRESS Q TO EXIT."
        .byte 13, 0
continue_msg:
        .text "PRESS ANY KEY TO CONTINUE..."
        .byte 13, 0
bye_msg:
        .text "GOODBYE"
        .byte 13, 0

; Four 63-byte hires sprite frames pulled directly out of
; star_anim.bin -- a plain external binary file, not assembler source.
; See this file's own header comment for the full explanation, and
; c64asm-reference.md section 11 for .incbin's exact syntax and rules.
        .align 64
frame0:
        .incbin "star_anim.bin", 0, 63
        .align 64
frame1:
        .incbin "star_anim.bin", 63, 63
        .align 64
frame2:
        .incbin "star_anim.bin", 126, 63
        .align 64
frame3:
        .incbin "star_anim.bin", 189, 63
