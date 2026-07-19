; demo.asm - exercises every library in lib/ together, including the
; diamond dependency where text.inc, input.inc, keyboard.inc,
; graphics.inc, and sound.inc all .include hardware.inc -- it should
; only be processed once (see c64asm-reference.md, "Includes").

        .basic start   ; auto-emits `jmp start` right after the loader
                          ; stub. This matters here because the
                          ; .include lines below emit real subroutine
                          ; code (print_msg, read_joy2, ...), not just
                          ; constants/macros -- without a jump straight
                          ; to start, SYS would land inside the FIRST
                          ; included subroutine instead, running it with
                          ; whatever garbage happens to be in A/Y at
                          ; power-on. This exact mistake, back when it
                          ; had to be written by hand as a separate `jmp
                          ; start` line that was easy to forget, is why
                          ; an earlier build of this demo printed
                          ; garbage and returned immediately instead of
                          ; running at all -- seeing it happen twice is
                          ; what prompted adding the `start` operand to
                          ; `.basic` itself.

str_ptr = $fb           ; text.inc's required zero-page scratch
cmp_ptr = $02            ; also required by text.inc -- its str_equal
kw_ptr  = $04             ; code is assembled unconditionally once
                            ; text.inc is included, even though this
                            ; demo never actually calls str_equal
word_dest_ptr = $fb      ; input.inc's required zero-page scratch (its
                            ; extract_word is likewise never actually
                            ; called here); safe to alias with str_ptr
key_scratch = $0a       ; keyboard.inc's required zero-page scratch for
                           ; wait_any_key -- never live across a call to
                           ; anything else here, so any free byte works
engine_playing = $09    ; sound.inc's required engine-state byte -- no
                           ; longer actually used (W used to trigger a
                           ; continuous engine tone; it's a movement key
                           ; now), but still required, since sound.inc's
                           ; own code references it unconditionally
                           ; regardless of whether anything here calls
                           ; engine_sound_on/off
gfx_ptr = $fd            ; graphics.inc's required zero-page scratch
xpos = $06               ; the star's actual current position now --
                            ; graphics.inc's own sprite0_bounce_step
                            ; never gets called here, but these are the
                            ; exact two symbols (X needs TWO bytes, low
                            ; and high, since the visible screen's right
                            ; edge is itself past 255) that routine's
                            ; unconditionally-assembled code requires
                            ; regardless, so main_loop below just reuses
                            ; them directly for its own WASD movement
ypos = $0b               ; one byte -- Y never needs a second byte,
                            ; since it never exceeds 255
xdir = $0c                 ; one byte -- genuinely unused here (WASD
                              ; movement doesn't need a "current
                              ; direction" flag the way autonomous
                              ; bouncing does), but still required by
                              ; sprite0_bounce_step's own code
ydir = $0e                  ; likewise unused but required
XMIN = 24                ; the star's real border-check bounds now,
XMAX = 320                 ; reused as-is from bounce.asm's own
YMIN = 50                    ; already-proven values -- both sprites
YMAX = 229                     ; are the same standard 24x21 hires size,
                                  ; so the same position bounds apply

        .include "lib/text.inc"
        .include "lib/input.inc"
        .include "lib/keyboard.inc"
        .include "lib/graphics.inc"
        .include "lib/sound.inc"

start:
        SID_INIT
        CIA_KEYBOARD_SETUP
        CLS
        PRINT welcome_msg
        PRINT instructions_msg
        PRINT continue_msg
        jsr wait_any_key        ; without this, BITMAP_MODE_ON below wipes
                                   ; the text above off the screen almost
                                   ; immediately -- there was no time to
                                   ; actually read it before this call was
                                   ; added; see lib/keyboard.inc

        BITMAP_MODE_ON BITMAP
        CLEAR_BITMAP BITMAP           ; without this, whatever garbage was
                                         ; already in RAM at BITMAP shows as
                                         ; scrambled pixels
        SET_SCREEN_COLOR %00010000     ; without this, the "WELCOME TO THE
                                         ; DEMO" text and CLS's fill just
                                         ; printed get reinterpreted as
                                         ; color data instead of being
                                         ; cleared to a plain background --
                                         ; exactly the bug this fixes
        SPRITE_INIT sprite_data, 1, 172, 140   ; roughly centered within
                                                  ; XMIN/XMAX/YMIN/YMAX below

        lda #172                ; xpos/ypos need their own separate
        sta xpos                  ; initialization to match -- SPRITE_INIT
        lda #0                     ; only sets the hardware registers
        sta xpos+1                   ; directly, it doesn't know xpos/ypos
        lda #140                       ; even exist
        sta ypos

main_loop:
        jsr wait_frame            ; paces movement to the screen's own
                                     ; refresh rate -- without this, the
                                     ; star would move as fast as the CPU
                                     ; can loop, far too fast to control

        jsr read_joy2
        sta joy_state

        and #%00010000          ; fire button
        beq no_fire
        PLAY_SOUND $30, $08, $00, %10000001
no_fire:

        lda #0
        sta moved_flag

        READ_KEY KEY_W_COL, KEY_W_ROW
        beq check_s
        lda ypos
        cmp #YMIN
        beq check_s              ; already at the top edge -- stop here
        jsr move_up
        inc moved_flag
check_s:
        READ_KEY KEY_S_COL, KEY_S_ROW
        beq check_a
        lda ypos
        cmp #YMAX
        beq check_a              ; already at the bottom edge
        jsr move_down
        inc moved_flag
check_a:
        READ_KEY KEY_A_COL, KEY_A_ROW
        beq check_d
        lda xpos                 ; 16-bit compare against XMIN (X needs
        cmp #<XMIN                 ; two bytes -- the visible screen's
        bne @do_move                 ; right edge is itself past 255)
        lda xpos+1
        cmp #>XMIN
        beq check_d               ; already at the left edge
@do_move:
        jsr move_left
        inc moved_flag
check_d:
        READ_KEY KEY_D_COL, KEY_D_ROW
        beq check_sound
        lda xpos
        cmp #<XMAX
        bne @do_move
        lda xpos+1
        cmp #>XMAX
        beq check_sound            ; already at the right edge
@do_move:
        jsr move_right
        inc moved_flag
check_sound:
        lda moved_flag
        beq check_exit
        PLAY_SOUND $60, $02, $02, %00010001   ; a short, quiet blip --
                                                 ; triangle wave, not the
                                                 ; fire button's harsher
                                                 ; noise -- once per frame
                                                 ; any movement actually
                                                 ; happened, so holding a
                                                 ; direction key against
                                                 ; the border correctly
                                                 ; falls silent again
check_exit:
        READ_KEY KEY_Q_COL, KEY_Q_ROW
        bne do_restart
        jmp main_loop
do_restart:
        BITMAP_MODE_OFF
        CLS
        PRINT bye_msg
        rts

; --- sprite movement helpers ---
;
; Each pair below handles one axis: the "move_*" half adjusts xpos/ypos
; (the 16-bit-X/8-bit-Y position this file tracks for itself -- see the
; header comment on xpos above) by one pixel, then falls into (or
; jumps to) the shared "update_sprite_*" half, which writes that
; position out to the actual VIC-II hardware registers and returns to
; WHICHEVER of the two callers jumped/fell in -- a JMP doesn't push its
; own return address, so the RTS at the end still pops the address the
; original JSR (from main_loop, above) pushed. Border checking happens
; in main_loop before either half of a pair is ever called -- these
; assume the move is already known to be safe.
move_left:
        lda xpos                 ; 16-bit decrement: only borrow into
        bne @dec_done               ; the high byte if the low byte was
        dec xpos+1                    ; already 0
@dec_done:
        dec xpos
        jmp update_sprite_x
move_right:
        inc xpos                 ; 16-bit increment: bump the low byte,
        bne update_sprite_x         ; and only the high byte too if that
        inc xpos+1                    ; wrapped
        ; falls through to update_sprite_x
update_sprite_x:
        lda xpos
        sta SPRITE0_X
        lda xpos+1
        beq @msb_clear
        lda SPRITE_X_MSB
        ora #%00000001           ; set sprite 0's bit only -- other
        sta SPRITE_X_MSB           ; sprites' MSB bits, if any, are left
        rts                          ; untouched
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

joy_state:
        .byte 0
moved_flag:
        .byte 0

welcome_msg:
        .text "WELCOME TO THE DEMO"
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

        .align 64
sprite_data:            ; a simple 4-pointed star/sparkle, drawn directly
                           ; as %binary literals so the shape is visible
                           ; right here in the source -- previously this
                           ; was just ".fill 63, $00" (all zero bytes),
                           ; which meant nothing was ever visible on
                           ; screen despite SPRITE_INIT/SPRITE_ENABLE
                           ; correctly turning the sprite hardware on.
                           ; Standard hires sprite format: 24x21 pixels,
                           ; 3 bytes per row, MSB = leftmost pixel. This
                           ; label is forward-referenced above by
                           ; SPRITE_INIT, which is fine: it's an ordinary
                           ; expression, not a .if condition, so normal
                           ; two-pass forward-reference resolution
                           ; applies.
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00011000, %00000000
        .byte %00000000, %00011000, %00000000
        .byte %00000000, %00111100, %00000000
        .byte %00000000, %01111110, %00000000
        .byte %00000000, %11111111, %00000000
        .byte %00000011, %11111111, %11000000
        .byte %00111111, %11111111, %11111100
        .byte %00000011, %11111111, %11000000
        .byte %00000000, %11111111, %00000000
        .byte %00000000, %01111110, %00000000
        .byte %00000000, %00111100, %00000000
        .byte %00000000, %00011000, %00000000
        .byte %00000000, %00011000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
