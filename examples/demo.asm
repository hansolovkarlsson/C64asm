; demo.asm - exercises every library in lib/ together, including the
; diamond dependency where text.inc, input.inc, graphics.inc, and
; sound.inc all .include hardware.inc -- it should only be processed
; once (see c64asm-reference.md, "Includes").

        .basic
        jmp start   ; .basic's SYS stub runs whatever comes immediately
                      ; after it -- and the .include lines below emit real
                      ; subroutine code (print_msg, read_joy2, ...), not
                      ; just constants/macros. Without this jmp, SYS would
                      ; land inside the FIRST included subroutine instead
                      ; of at the real entry point below, running it with
                      ; whatever garbage happens to be in A/Y at power-on.
                      ; This exact mistake is why an earlier build of this
                      ; demo printed garbage and returned immediately
                      ; instead of running at all -- see lib-reference.md's
                      ; note on this for the general pattern to follow in
                      ; your own programs.

str_ptr = $fb           ; text.inc's required zero-page scratch
cmp_ptr = $02            ; also required by text.inc -- its str_equal
kw_ptr  = $04             ; code is assembled unconditionally once
                            ; text.inc is included, even though this
                            ; demo never actually calls str_equal
word_dest_ptr = $fb      ; input.inc's required zero-page scratch (its
                            ; extract_word is likewise never actually
                            ; called here); safe to alias with str_ptr
engine_playing = $09    ; sound.inc's required engine-state byte
gfx_ptr = $fd            ; graphics.inc's required zero-page scratch

        .include "lib/text.inc"
        .include "lib/input.inc"
        .include "lib/graphics.inc"
        .include "lib/sound.inc"

start:
        SID_INIT
        CIA_KEYBOARD_SETUP
        CLS
        PRINT welcome_msg

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
        SPRITE_INIT sprite_data, 1, 100, 100

main_loop:
        jsr read_joy2
        sta joy_state

        and #%00010000          ; fire button
        beq no_fire
        PLAY_SOUND $30, $08, $00, %10000001
no_fire:

        READ_KEY %11111101, %00000010    ; W
        bne w_held
        jsr engine_sound_off
        jmp check_restart
w_held:
        jsr engine_sound_on

check_restart:
        READ_KEY %11111011, %00000001    ; Y (restart)
        bne do_restart
        jmp main_loop
do_restart:
        BITMAP_MODE_OFF
        CLS
        PRINT bye_msg
        rts

joy_state:
        .byte 0

welcome_msg:
        .text "WELCOME TO THE DEMO"
        .byte 13, 0
bye_msg:
        .text "GOODBYE"
        .byte 13, 0

        .align 64
sprite_data:            ; real, .align-placed sprite data -- SPRITE_INIT
        .fill 63, $00    ; above forward-references this label, which is
                           ; fine: it's an ordinary expression, not a .if
                           ; condition, so normal two-pass forward-
                           ; reference resolution applies
