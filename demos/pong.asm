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

BALL_DATA   = $0ac0   ; 64-byte aligned; NOT in the VIC's $1000-$1FFF
PADDLE_DATA = $0b00   ; character-ROM shadow range (see c64-memory-reference.md §4)

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
joy_state = $09         ; scratch: JOY2 captured before any keyboard scan
p1_score  = $0a         ; 0-9, wraps back to 0 (no multi-digit display)
p2_score  = $0b

P1_SCORE_POS = SCREEN + 5     ; row 0, column 5
P2_SCORE_POS = SCREEN + 23    ; row 0, column 23
P1_SCORE_COLOR = COLOR + 5
P2_SCORE_COLOR = COLOR + 23

; Paddle X positions, chosen to be visually symmetric around the net
; (see NET_COL below) while staying within a single byte (0-255) --
; c64-memory-reference.md §4 covers extending this to the full physical
; screen width (up to X=344) via the $D010 X-MSB register, which would
; let the paddles sit right at the true screen edges; this demo trades
; that extra width for the simplicity of not needing 9-bit arithmetic.
LEFT_PADDLE_X  = 24
RIGHT_PADDLE_X = 250

; Ball bounds (kept within a single byte, 0-255, so no X-MSB handling is
; needed -- see c64-memory-reference.md §4 for extending to full width).
YMIN = 50
YMAX = 220
BALL_XMIN = 34    ; where the ball tests against the left paddle
BALL_XMAX = 240   ; where the ball tests against the right paddle

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
        ; Explicitly configure CIA1's data-direction registers for
        ; keyboard scanning: port A ($DC00) as outputs (to drive column
        ; selects), port B ($DC01) as inputs (to read row states). Every
        ; working reference example of manual keyboard-matrix scanning
        ; does this explicitly rather than assuming the CIA is already
        ; in the right state -- this was missing before and is the most
        ; likely reason W/S weren't being detected.
        lda #%11111111
        sta $dc02              ; DDRA: all outputs
        lda #%00000000
        sta $dc03              ; DDRB: all inputs

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

        lda #$0f
        sta $d418              ; SID: full volume, no filter
        lda #$00
        sta $d404               ; voice 1: gate off

        ; --- draw a dashed net down the middle of the play field ---
        ; (column 14, not the screen's true center column 20 -- it lines
        ; up with the midpoint between the paddles above, not the
        ; midpoint of the physical screen; see the paddle-X comment)
        lda #<(SCREEN+14)
        sta ptr
        lda #>(SCREEN+14)
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

        lda #<(COLOR+14)
        sta ptr
        lda #>(COLOR+14)
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

        ; --- score display: white digits at top-left and top-right ---
        lda #1                  ; white
        sta P1_SCORE_COLOR
        sta P2_SCORE_COLOR
        lda #0
        sta p1_score
        sta p2_score
        jsr draw_p1_score
        jsr draw_p2_score

        ; --- initial game state: ball serves from the player's paddle ---
        lda #100
        sta p1_y
        sta p2_y
        lda #BALL_XMIN          ; = LEFT_PADDLE_X + 10, right at the paddle
        sta ball_x
        lda #108                 ; = p1_y (100) + 8, roughly paddle-center height
        sta ball_y
        lda #1
        sta ball_xdir             ; serve toward the computer paddle
        sta ball_ydir
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

; Reads joystick port 2 AND, as a fallback that needs no emulator/
; hardware joystick configuration at all, the W/S keys directly from the
; keyboard matrix (W = up, S = down). Either input moves the paddle.
;
; IMPORTANT: JOY2 and the keyboard column-select register are the exact
; same address ($DC00). The joystick reading is captured into joy_state
; ONCE, before any keyboard-scan write touches $DC00 -- reading JOY2
; again afterward would pick up the leftover column-select value instead
; of real joystick state (this was the actual bug: the down-check was
; reading $DC00 right after the up-check's keyboard scan had left it
; holding a value that happened to look exactly like "down is pressed",
; on every single frame, regardless of any real input).
;
; The keyboard read is wrapped in SEI/CLI: the KERNAL's own IRQ handler
; keeps scanning the keyboard in the background even after we've SYS'd
; out of BASIC, so without disabling interrupts around our own $DC00
; write + $DC01 read, that background scan could overwrite $DC00 in
; between and corrupt the read.
read_joystick:
        lda JOY2
        sta joy_state

        lda joy_state
        and #%00000001         ; joystick up
        beq do_up
        sei
        lda #%11111101         ; select keyboard column 1 (has W, A, S)
        sta $dc00
        lda $dc01
        cli
        and #%00000010         ; bit 1 = W
        bne no_up
do_up:
        lda p1_y
        cmp #PADDLE_YMIN
        beq no_up
        dec p1_y
no_up:
        lda joy_state
        and #%00000010         ; joystick down
        beq do_down
        sei
        lda #%11111101
        sta $dc00
        lda $dc01
        cli
        and #%00100000         ; bit 5 = S
        bne no_down
do_down:
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
        jsr play_wall_bounce
        jmp ball_y_done
ball_move_up:
        dec ball_y
        lda ball_y
        cmp #YMIN
        bne ball_y_done
        lda #1
        sta ball_ydir
        jsr play_wall_bounce
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
        jsr play_paddle_hit
        rts
right_miss:
        jsr flash_border
        jsr play_score
        inc p1_score
        lda p1_score
        cmp #10
        bne p1_score_ok
        lda #0
        sta p1_score
p1_score_ok:
        jsr draw_p1_score
        ; serve from the left (player) paddle's current position -- it
        ; just won the point, so it serves next, toward the side that missed
        lda #BALL_XMIN
        sta ball_x
        lda p1_y
        clc
        adc #8                  ; roughly paddle-center height
        sta ball_y
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
        jsr play_paddle_hit
        rts
left_miss:
        jsr flash_border
        jsr play_score
        inc p2_score
        lda p2_score
        cmp #10
        bne p2_score_ok
        lda #0
        sta p2_score
p2_score_ok:
        jsr draw_p2_score
        lda #BALL_XMAX
        sta ball_x
        lda p2_y
        clc
        adc #8
        sta ball_y
        rts

; A miss: flash the border briefly as visual feedback. Position resetting
; is handled by the caller (right_miss/left_miss), which knows which
; paddle should serve next.
flash_border:
        lda #2                  ; red
        sta $d020
        ldx #8
flash_delay:
        jsr wait_frame
        dex
        bne flash_delay
        lda #0
        sta $d020
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

; Score digits are screen codes, not PETSCII -- but for the digit range
; ($30-$39) the two happen to have the same numeric value (see
; c64-memory-reference.md §2), so the same +$30 offset used for PETSCII
; text works directly here too.
draw_p1_score:
        lda p1_score
        clc
        adc #$30
        sta P1_SCORE_POS
        rts

draw_p2_score:
        lda p2_score
        clc
        adc #$30
        sta P2_SCORE_POS
        rts

; --- SID sound effects ---
; All three are fire-and-forget: set the frequency, force a fresh gate
; 0->1 transition (needed for the attack phase to actually retrigger --
; writing gate=1 when it's already 1 doesn't restart the envelope), and
; let a short decay with sustain=0 fade the note out on its own. Nothing
; in the main loop needs to track when to gate a voice back off.
VOICE1_FREQ_LO = $d400
VOICE1_FREQ_HI = $d401
VOICE1_CTRL    = $d404
VOICE1_AD      = $d405
VOICE1_SR      = $d406

play_paddle_hit:
        lda #$00
        sta VOICE1_CTRL          ; force gate off first
        lda #$00
        sta VOICE1_FREQ_LO
        lda #$30
        sta VOICE1_FREQ_HI       ; high pitch
        lda #$08
        sta VOICE1_AD             ; attack 0, decay 8
        lda #$00
        sta VOICE1_SR              ; sustain 0, release 0
        lda #%00010001             ; triangle wave, gate on
        sta VOICE1_CTRL
        rts

play_wall_bounce:
        lda #$00
        sta VOICE1_CTRL
        lda #$00
        sta VOICE1_FREQ_LO
        lda #$18
        sta VOICE1_FREQ_HI       ; medium pitch
        lda #$06
        sta VOICE1_AD             ; attack 0, decay 6 -- a bit crisper/shorter
        lda #$00
        sta VOICE1_SR
        lda #%00010001
        sta VOICE1_CTRL
        rts

play_score:
        lda #$00
        sta VOICE1_CTRL
        lda #$00
        sta VOICE1_FREQ_LO
        lda #$08
        sta VOICE1_FREQ_HI       ; low pitch
        lda #$0f
        sta VOICE1_AD             ; attack 0, decay 15 (longest -- a "boop" tail)
        lda #$00
        sta VOICE1_SR
        lda #%00010001
        sta VOICE1_CTRL
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
