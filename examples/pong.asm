; pong.asm - a small Pong-style demo: one human paddle (joystick port 2)
; against a simple computer paddle, and a ball that bounces between them.
;
; The game logic (paddle coverage, AI speed, ball bounds) was validated
; in a Python simulation before being written here, specifically to catch
; a subtle bug where the AI paddle's vertical range didn't actually
; cover the ball's full travel range even with hit-tolerance included --
; that bug would have made the right side of the game silently
; unwinnable no matter how good the AI logic was.
;
; Uses lib/graphics.inc (wait_frame), lib/input.inc (read_joy2,
; READ_KEY, CIA_KEYBOARD_SETUP), and lib/sound.inc (SID_INIT,
; PLAY_SOUND) rather than its own copies of these -- they were
; originally written here (wait_frame independently duplicated again,
; byte-for-byte, in bounce.asm and lander.asm; the sound effects reused
; as-is by bounce.asm), then generalized into the library. Refactoring
; onto input.inc's read_joy2/READ_KEY also fixed a real, previously
; unnoticed bug: this file used to leave CIA1's port A permanently
; configured as output (for keyboard column-select) from setup onward,
; which meant reading JOY2 afterward never actually read the joystick
; at all -- it read back whatever column-select byte had last been
; written, misread as joystick input. The player paddle would drift
; downward on its own with nothing held or the joystick unplugged. See
; input.inc's own header comment for the general form of this bug;
; read_joy2/READ_KEY each fix it by setting the direction register to
; whatever they specifically need immediately before reading, rather
; than relying on a one-time setup to have left it in the right state.
;
; What's specific to *this* particular demo -- score display, the net,
; paddle/ball collision and AI logic, and the exact sound parameters
; for each effect -- stays here.

        .basic start   ; needed once library code (wait_frame,
                          ; sprite0_bounce_step, read_joy2, READ_KEY,
                          ; engine_sound_on/off) sits between .basic and
                          ; start: -- see c64asm-reference.md §7

SCREEN = $0400
COLOR  = $d800

ptr       = $fb        ; temporary 16-bit pointer, used only during setup
SPRITE_PTR1 = SCREEN + $3f9   ; left paddle (player)
SPRITE_PTR2 = SCREEN + $3fa   ; right paddle (computer)
ball_x    = $02         ; TWO bytes (ball_x, ball_x+1) -- the ball's X
                           ; position is a full 16-bit value now that
                           ; BALL_XMAX exceeds 255; see move_ball and
                           ; update_sprites below
ball_y    = $04
ball_xdir = $05         ; 1 = moving right, 0 = moving left
ball_ydir = $06         ; 1 = moving down,  0 = moving up
p1_y      = $07         ; player paddle Y (top-left corner, like all sprites)
p2_y      = $08         ; computer paddle Y
ai_delay  = $09
joy_state = $0a         ; scratch: read_joy2's result, captured once per
                           ; frame before any READ_KEY call -- see the
                           ; header comment above and read_joystick below
p1_score  = $0b         ; 0-9, wraps back to 0 (no multi-digit display)
p2_score  = $0c

; graphics.inc's gfx_ptr and input.inc's word_dest_ptr are both required
; (see each file's header comment: their code is assembled
; unconditionally on .include, whether or not this program actually
; calls anything that uses them) but never actually needed at the same
; time as ptr's own, real use during setup -- see text.inc's header
; comment for the general reasoning on why aliasing like this is safe.
gfx_ptr = ptr
word_dest_ptr = ptr

; graphics.inc's sprite0_bounce_step and sound.inc's engine_sound_on/off
; are likewise never actually called here (this file's ball bounces off
; paddles, not just walls, which is different enough game logic that
; it's kept as this file's own move_ball below; and every sound effect
; here is a one-shot PLAY_SOUND, not a held tone) but their code is
; still assembled unconditionally, so xpos/ypos/xdir/ydir/XMIN/XMAX/
; YMIN/YMAX/engine_playing all still need to be defined *something*.
; Since that code is dead -- never jsr'd to at all, not just inactive
; at a given moment -- aliasing these directly onto real gameplay state
; is completely safe, stronger even than gfx_ptr's aliasing above: dead
; code can never conflict with anything, at any point in time.
xpos = ball_x
ypos = ball_y
xdir = ball_xdir
ydir = ball_ydir
engine_playing = ai_delay
XMIN = 0
XMAX = 255
YMIN = 0
YMAX = 255

P1_SCORE_POS = SCREEN + 5     ; row 0, column 5 -- 5 columns right of the
                                 ; left paddle (column 0)
P2_SCORE_POS = SCREEN + 32    ; row 0, column 32 -- 5 columns left of the
                                 ; right paddle (column 37), the same
                                 ; symmetric offset as P1_SCORE_POS above
P1_SCORE_COLOR = COLOR + 5
P2_SCORE_COLOR = COLOR + 32

; Paddle X positions. RIGHT_PADDLE_X's far edge (RIGHT_PADDLE_X+24, the
; paddle sprite's own width) touches the true screen right edge (X=344)
; exactly, mirroring how LEFT_PADDLE_X already sits exactly at the true
; left edge (X=24) -- see c64-memory-reference.md §4 for the visible-
; screen sprite-coordinate mapping this is calibrated against. Like
; bounce.asm's ball, RIGHT_PADDLE_X exceeds 255, so it needs the X-MSB
; register ($D010) -- but unlike the ball, the paddle never moves in X,
; so that bit only needs setting once, during setup, not every frame.
LEFT_PADDLE_X  = 24
RIGHT_PADDLE_X = 320

; Ball bounds. YMAX_WALL is calibrated the same way bounce.asm's YMAX
; is: TRUE_BOTTOM_EDGE(250) - BALL_HEIGHT(21) = 229, so the ball's own
; bottom edge touches the true bottom edge rather than stopping short of
; it. BALL_XMAX, at RIGHT_PADDLE_X-10 = 310, is past 255 the same way
; RIGHT_PADDLE_X is -- but the ball *does* move every frame, so unlike
; the paddle, its X position needs to be a full 16-bit value (ball_x /
; ball_x+1) with the X-MSB bit updated every frame move_ball runs, not
; just once -- see move_ball and update_sprites below.
YMIN_WALL = 50
YMAX_WALL = 229
BALL_XMIN = 34    ; where the ball tests against the left paddle
BALL_XMAX = 310   ; where the ball tests against the right paddle

; Paddle Y bounds. The paddle is the same 21px height as the ball, so
; these are calibrated the same way YMIN_WALL/YMAX_WALL are: PADDLE_YMIN
; is the true top edge itself (a sprite's Y position IS its own top
; edge, no subtraction needed), and PADDLE_YMAX is TRUE_BOTTOM_EDGE(250)
; - PADDLE_HEIGHT(21) = 229, so the paddle's own bottom edge reaches the
; true bottom rather than stopping short of it, the same gap bounce.asm's
; ball had before its own fix.
;
; These MUST also satisfy PADDLE_YMIN <= YMIN_WALL+PADDLE_RANGE and
; PADDLE_YMAX >= YMAX_WALL-PADDLE_RANGE, or the paddle will have a dead
; zone it can never reach regardless of how well it's controlled -- this
; exact mistake (PADDLE_YMAX too small) was caught by simulation during
; original development, and re-verified with a Python simulation
; checking every ball_y/paddle_y combination directly after the values
; below changed (there's comfortable slack now, unlike the previous
; values, which sat exactly on this boundary with none).
PADDLE_YMIN = 50
PADDLE_YMAX = 229
PADDLE_RANGE = 24

AI_SPEED = 2      ; computer paddle moves once every this-many frames
                    ; (the player, via joystick, moves every frame it's
                    ; held -- this asymmetry is the game's difficulty)

        .include "lib/graphics.inc"
        .include "lib/input.inc"
        .include "lib/sound.inc"

start:
        CIA_KEYBOARD_SETUP   ; sets CIA1 port B to all-input, for reading
                                ; keyboard row data; port A's direction is
                                ; managed per-call by read_joy2/READ_KEY
                                ; themselves, not set once here -- see the
                                ; header comment above for why that
                                ; per-call approach is the actual fix

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
        sta VIC_BORDER
        sta VIC_BG0

        SID_INIT

        ; --- draw a dashed net down the middle of the play field ---
        ; column 20 -- the screen's true center, which now also happens
        ; to be the midpoint between the paddles (column 0 and column
        ; 37), now that both paddles are properly calibrated against the
        ; true screen edges rather than stopping short of them -- see
        ; the paddle-X comment above
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
        ; ball (sprite 0) via SPRITE_INIT; the other two are set up
        ; directly afterward, exactly as SPRITE_INIT's own header
        ; comment describes for a program using more than one sprite.
        SPRITE_INIT ball_data, 1, BALL_XMIN, 108   ; white; BALL_XMIN =
                                                      ; LEFT_PADDLE_X+10,
                                                      ; right at the
                                                      ; paddle; 108 =
                                                      ; p1_y(100)+8,
                                                      ; roughly
                                                      ; paddle-center
                                                      ; height
        lda SPRITE_X_MSB
        and #%11111110       ; ball starts under 256 (BALL_XMIN=34), so
        sta SPRITE_X_MSB       ; its MSB bit should start cleared too

        lda #(paddle_data / 64)
        sta SPRITE_PTR1
        lda #(paddle_data / 64)
        sta SPRITE_PTR2

        lda #3                 ; left paddle: cyan
        sta $d028
        lda #7                 ; right paddle: yellow
        sta $d029

        lda #LEFT_PADDLE_X
        sta $d002
        lda #100
        sta $d003              ; = initial p1_y, set below
        lda #<RIGHT_PADDLE_X
        sta $d004
        lda SPRITE_X_MSB
        ora #%00000100         ; sprite 2's X-MSB bit -- RIGHT_PADDLE_X
        sta SPRITE_X_MSB         ; is past 255; see the paddle-X comment
                                    ; above. Set once here, not every
                                    ; frame like the ball's own MSB bit
                                    ; in update_sprites below, since the
                                    ; paddle never moves in X
        lda #100
        sta $d005              ; = initial p2_y, set below

        lda SPRITE_ENABLE
        ora #%00000110         ; also enable sprites 1 and 2 (the ball,
        sta SPRITE_ENABLE         ; sprite 0, was already enabled by
                                     ; SPRITE_INIT above)

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
        lda #<BALL_XMIN
        sta ball_x
        lda #>BALL_XMIN
        sta ball_x+1
        lda #108
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

; Reads joystick port 2 AND, as a fallback that needs no emulator/
; hardware joystick configuration at all, the W/S keys directly from the
; keyboard matrix (W = up, S = down). Either input moves the paddle.
read_joystick:
        jsr read_joy2
        sta joy_state           ; active-HIGH (1=pressed) -- read_joy2's
                                   ; own convention, see input.inc

        lda joy_state
        and #%00000001         ; joystick up
        bne do_up
        READ_KEY %11111101, %00000010   ; W key (matrix column 1, bit 1)
        beq no_up
do_up:
        lda p1_y
        cmp #PADDLE_YMIN
        beq no_up
        dec p1_y
no_up:
        lda joy_state
        and #%00000010         ; joystick down
        bne do_down
        READ_KEY %11111101, %00100000   ; S key (matrix column 1, bit 5)
        beq no_down
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
        cmp #YMAX_WALL
        bne ball_y_done
        lda #0
        sta ball_ydir
        PLAY_SOUND $18, $06, $00, %00010001   ; medium pitch, crisp/short
        jmp ball_y_done
ball_move_up:
        dec ball_y
        lda ball_y
        cmp #YMIN_WALL
        bne ball_y_done
        lda #1
        sta ball_ydir
        PLAY_SOUND $18, $06, $00, %00010001
ball_y_done:

        ; --- X axis: move, then test against whichever paddle zone we hit ---
        ; ball_x is 16-bit now (BALL_XMAX exceeds 255) -- same increment/
        ; decrement/compare pattern as graphics.inc's sprite0_bounce_step,
        ; not reused directly from there since this ball tests against
        ; paddles at BALL_XMIN/BALL_XMAX, not a simple 4-wall bounce.
        lda ball_xdir
        beq ball_move_left
        inc ball_x              ; 16-bit increment: bump the low byte, and
        bne ball_x_inc_done       ; only the high byte too if that wrapped
        inc ball_x+1
ball_x_inc_done:
        lda ball_x               ; 16-bit compare against BALL_XMAX
        cmp #<BALL_XMAX
        bne ball_x_done
        lda ball_x+1
        cmp #>BALL_XMAX
        bne ball_x_done
        jsr test_right_paddle
        jmp ball_x_done
ball_move_left:
        lda ball_x               ; 16-bit decrement: only borrow into the
        bne ball_x_dec_done        ; high byte if the low byte was already 0
        dec ball_x+1
ball_x_dec_done:
        dec ball_x
        lda ball_x                ; 16-bit compare against BALL_XMIN (which
        cmp #<BALL_XMIN              ; is ordinarily under 256, so ball_x+1
        bne ball_x_done                ; will ordinarily be checked against 0)
        lda ball_x+1
        cmp #>BALL_XMIN
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
        PLAY_SOUND $30, $08, $00, %00010001   ; high pitch
        rts
right_miss:
        jsr flash_border
        PLAY_SOUND $08, $0f, $00, %00010001   ; low pitch, longest decay
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
        lda #<BALL_XMIN
        sta ball_x
        lda #>BALL_XMIN
        sta ball_x+1
        lda p1_y
        clc
        adc #8                  ; roughly paddle-center height -- clamped
        cmp #YMAX_WALL             ; below to stay STRICTLY less than
        bcc serve_right_y_ok         ; YMAX_WALL, not just <= it: p1_y can
        lda #YMAX_WALL-1               ; now reach values (e.g. 221) where
serve_right_y_ok:                        ; p1_y+8 lands EXACTLY on YMAX_WALL,
        sta ball_y                         ; and if ball_ydir is still "down"
        rts                                   ; at that moment, move_ball's
                                                ; own wall-bounce check (which
                                                ; only fires the instant
                                                ; ball_y becomes YMAX_WALL via
                                                ; increment) never triggers --
                                                ; the ball just keeps
                                                ; incrementing past 255 and
                                                ; wrapping. This isn't just a
                                                ; clamping edge case; it can
                                                ; happen on entirely ordinary,
                                                ; non-clamped serves too.

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
        PLAY_SOUND $30, $08, $00, %00010001
        rts
left_miss:
        jsr flash_border
        PLAY_SOUND $08, $0f, $00, %00010001
        inc p2_score
        lda p2_score
        cmp #10
        bne p2_score_ok
        lda #0
        sta p2_score
p2_score_ok:
        jsr draw_p2_score
        lda #<BALL_XMAX
        sta ball_x
        lda #>BALL_XMAX
        sta ball_x+1
        lda p2_y
        clc
        adc #8                  ; roughly paddle-center height -- clamped
        cmp #YMAX_WALL             ; the same way right_miss's own serve is,
        bcc serve_left_y_ok          ; and for the identical reason -- see
        lda #YMAX_WALL-1               ; that comment above
serve_left_y_ok:
        sta ball_y
        rts

; A miss: flash the border briefly as visual feedback. Position resetting
; is handled by the caller (right_miss/left_miss), which knows which
; paddle should serve next.
flash_border:
        lda #2                  ; red
        sta VIC_BORDER
        ldx #8
flash_delay:
        jsr wait_frame
        dex
        bne flash_delay
        lda #0
        sta VIC_BORDER
        rts

update_sprites:
        lda ball_x
        sta SPRITE0_X
        lda ball_x+1
        beq ball_msb_clear
        lda SPRITE_X_MSB
        ora #%00000001       ; sprite 0's bit only -- sprite 2's own MSB
        sta SPRITE_X_MSB       ; bit (set once at startup) is untouched
        jmp ball_msb_done
ball_msb_clear:
        lda SPRITE_X_MSB
        and #%11111110
        sta SPRITE_X_MSB
ball_msb_done:
        lda ball_y
        sta SPRITE0_Y
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

        ; NOTE: sprite/bitmap/screen data must avoid $1000-$1FFF (and
        ; $9000-$9FFF) within the current VIC bank -- the VIC-II always
        ; substitutes the character ROM for its own reads in those ranges,
        ; regardless of what's actually stored there in RAM. .align (rather
        ; than a fixed address) means this always lands correctly right
        ; after the code above, whatever that code's exact size happens to
        ; be -- see c64asm-reference.md §7 for the directive itself.
        .align 64
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

        .align 64
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
