; pong.asm - a small Pong-style demo: one human paddle (joystick port 2)
; against a simple computer paddle, and a ball that bounces between them.
;
; The game logic (paddle coverage, AI speed, ball bounds) was validated
; in a Python simulation before being written here, specifically to catch
; a subtle bug where the AI paddle's vertical range didn't actually
; cover the ball's full travel range even with hit-tolerance included --
; that bug would have made the right side of the game silently
; unwinnable no matter how good the AI logic was.

SCREEN = $0400
COLOR  = $d800
JOY2   = $dc00

BALL_DATA   = $0a00   ; 64-byte aligned; NOT in the VIC's $1000-$1FFF
PADDLE_DATA = $0a40   ; character-ROM shadow range (see c64-memory-reference.md §4)

SPRITE_PTR0 = SCREEN + $3f8   ; ball
SPRITE_PTR1 = SCREEN + $3f9   ; left paddle (player)
SPRITE_PTR2 = SCREEN + $3fa   ; right paddle (computer)

ptr       = $fb        ; temporary 16-bit pointer, used only during setup
ball_x    = $02
ball_y    = $03
ball_xdir = $04         ; 1 = moving right, 0 = moving left
ball_ydir = $05         ; 1 = moving down,  0 = moving up
p1_y      = $06         ; player paddle Y (top-left corner, like all sprites)
p2_y      = $07         ; computer paddle Y
ai_delay  = $08

LEFT_PADDLE_X  = 30
RIGHT_PADDLE_X = 220

; Ball bounds (kept within a single byte, 0-255, so no X-MSB handling is
; needed -- see c64-memory-reference.md §4 for extending to full width).
YMIN = 50
YMAX = 220
BALL_XMIN = 40    ; where the ball tests against the left paddle
BALL_XMAX = 210   ; where the ball tests against the right paddle

; Paddle Y bounds. These MUST satisfy PADDLE_YMIN <= YMIN+PADDLE_RANGE and
; PADDLE_YMAX >= YMAX-PADDLE_RANGE, or the paddle will have a dead zone it
; can never reach regardless of how well it's controlled -- this exact
; mistake (PADDLE_YMAX too small) was caught by simulation during
; development and is why the numbers below aren't rounder ones.
PADDLE_YMIN = 46
PADDLE_YMAX = 205
PADDLE_RANGE = 24

AI_SPEED = 2      ; computer paddle moves once every this-many frames
                    ; (the player, via joystick, moves every frame it's
                    ; held -- this asymmetry is the game's difficulty)

        .basic

start:
        ; --- clear the screen to spaces and set colors ---
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldx #4
        lda #$20              ; space
clear_screen:
        ldy #$00
clear_screen_byte:
        sta (ptr),y
        iny
        bne clear_screen_byte
        inc ptr+1
        dex
        bne clear_screen

        lda #<COLOR
        sta ptr
        lda #>COLOR
        sta ptr+1
        ldx #4
        lda #$00              ; color doesn't matter for space characters,
clear_color:                   ; but leave it in a known state anyway
        ldy #$00
clear_color_byte:
        sta (ptr),y
        iny
        bne clear_color_byte
        inc ptr+1
        dex
        bne clear_color

        lda #0
        sta $d020             ; black border
        sta $d021             ; black background

        ; --- draw a dashed net down the middle of the screen ---
        lda #<(SCREEN+20)
        sta ptr
        lda #>(SCREEN+20)
        sta ptr+1
        ldx #13
draw_net_screen:
        ldy #$00
        lda #$2a              ; screen code '*' (same value as PETSCII here)
        sta (ptr),y
        clc
        lda ptr
        adc #80                ; skip a row (draws every other row => dashed)
        sta ptr
        lda ptr+1
        adc #0
        sta ptr+1
        dex
        bne draw_net_screen

        lda #<(COLOR+20)
        sta ptr
        lda #>(COLOR+20)
        sta ptr+1
        ldx #13
draw_net_color:
        ldy #$00
        lda #1                 ; white
        sta (ptr),y
        clc
        lda ptr
        adc #80
        sta ptr
        lda ptr+1
        adc #0
        sta ptr+1
        dex
        bne draw_net_color

        ; --- set up the three sprites ---
        lda #(BALL_DATA / 64)
        sta SPRITE_PTR0
        lda #(PADDLE_DATA / 64)
        sta SPRITE_PTR1
        lda #(PADDLE_DATA / 64)
        sta SPRITE_PTR2

        lda #1                 ; ball: white
        sta $d027
        lda #3                 ; left paddle: cyan
        sta $d028
        lda #7                 ; right paddle: yellow
        sta $d029

        lda #%00000111
        sta $d015              ; enable sprites 0, 1, 2

        lda #LEFT_PADDLE_X
        sta $d002
        lda #RIGHT_PADDLE_X
        sta $d004

        ; --- initial game state ---
        lda #128
        sta ball_x
        lda #135
        sta ball_y
        lda #1
        sta ball_xdir
        sta ball_ydir
        lda #100
        sta p1_y
        sta p2_y
        lda #AI_SPEED
        sta ai_delay

main_loop:
        jsr wait_frame
        jsr read_joystick
        jsr move_ai
        jsr move_ball
        jsr update_sprites
        jmp main_loop

; Busy-waits for a raster line near the bottom of the display -- syncs
; the main loop to the screen's refresh rate for smooth, consistent speed.
wait_frame:
        lda $d012
        cmp #$fb
        bne wait_frame
        rts

read_joystick:
        lda JOY2
        and #%00000001         ; up
        bne no_up
        lda p1_y
        cmp #PADDLE_YMIN
        beq no_up
        dec p1_y
no_up:
        lda JOY2
        and #%00000010         ; down
        bne no_down
        lda p1_y
        cmp #PADDLE_YMAX
        beq no_down
        inc p1_y
no_down:
        rts

move_ai:
        dec ai_delay
        bne ai_done
        lda #AI_SPEED
        sta ai_delay

        lda p2_y
        cmp ball_y
        beq ai_done
        bcc ai_need_down       ; p2_y < ball_y -> move down

        lda p2_y
        cmp #PADDLE_YMIN
        beq ai_done
        dec p2_y
        jmp ai_done

ai_need_down:
        lda p2_y
        cmp #PADDLE_YMAX
        beq ai_done
        inc p2_y
ai_done:
        rts

move_ball:
        ; --- Y axis: bounce off the top/bottom walls ---
        lda ball_ydir
        beq ball_move_up
        inc ball_y
        lda ball_y
        cmp #YMAX
        bne ball_y_done
        lda #0
        sta ball_ydir
        jmp ball_y_done
ball_move_up:
        dec ball_y
        lda ball_y
        cmp #YMIN
        bne ball_y_done
        lda #1
        sta ball_ydir
ball_y_done:

        ; --- X axis: move, then test against whichever paddle zone we hit ---
        lda ball_xdir
        beq ball_move_left
        inc ball_x
        lda ball_x
        cmp #BALL_XMAX
        bne ball_x_done
        jsr test_right_paddle
        jmp ball_x_done
ball_move_left:
        dec ball_x
        lda ball_x
        cmp #BALL_XMIN
        bne ball_x_done
        jsr test_left_paddle
ball_x_done:
        rts

; Hit test: is ball_y within PADDLE_RANGE of paddle_y? Implemented as two
; one-sided unsigned checks (never subtracts, so there's no underflow
; risk) rather than a single |a-b|<=range comparison.
test_right_paddle:
        lda ball_y
        clc
        adc #PADDLE_RANGE
        cmp p2_y
        bcc right_miss          ; ball_y+RANGE < p2_y -> ball is above the paddle
        lda p2_y
        clc
        adc #PADDLE_RANGE
        cmp ball_y
        bcc right_miss          ; p2_y+RANGE < ball_y -> ball is below the paddle
        lda #0
        sta ball_xdir            ; hit -- bounce back left
        rts
right_miss:
        jsr flash_and_reset
        rts

test_left_paddle:
        lda ball_y
        clc
        adc #PADDLE_RANGE
        cmp p1_y
        bcc left_miss
        lda p1_y
        clc
        adc #PADDLE_RANGE
        cmp ball_y
        bcc left_miss
        lda #1
        sta ball_xdir             ; hit -- bounce back right
        rts
left_miss:
        jsr flash_and_reset
        rts

; A miss: flash the border briefly, then respawn the ball at center.
; ball_xdir/ball_ydir are deliberately left untouched, so the ball serves
; back out in the same direction it was going -- toward whichever side
; just missed it.
flash_and_reset:
        lda #2                  ; red
        sta $d020
        ldx #8
flash_delay:
        jsr wait_frame
        dex
        bne flash_delay
        lda #0
        sta $d020

        lda #128
        sta ball_x
        lda #135
        sta ball_y
        rts

update_sprites:
        lda ball_x
        sta $d000
        lda ball_y
        sta $d001
        lda p1_y
        sta $d003
        lda p2_y
        sta $d005
        rts

        * = BALL_DATA
ball_data:
        ; a small filled square, roughly centered
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte $00

        * = PADDLE_DATA
paddle_data:
        ; a solid vertical bar, full height
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte %11111111,%00000000,%00000000
        .byte $00
