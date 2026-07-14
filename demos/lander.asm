; lander.asm - a small Lunar-Lander-style demo. Gravity constantly
; accelerates the ship downward; the main thruster (up) and side
; thrusters (left/right) fight that, at the cost of a limited fuel
; supply. Land within the marked pad, slow enough, to win.
;
; The physics (acceleration/deceleration modeled as direction+magnitude,
; not true signed arithmetic -- the same proven pattern as bounce.asm's
; and pong.asm's ball) and the fuel budget were both validated in a
; Python simulation before any assembly was written. That simulation
; caught a real balance problem: a naive fuel budget let the ship burn
; through its entire supply just drifting sideways toward the pad,
; leaving nothing for the final braking burn -- every flight crashed at
; maximum speed no matter how much fuel it started with, until the
; *policy* used less of it earlier. FUEL_START below is the smallest
; round number with a comfortable margin above the minimum a careful,
; simulated pilot actually needs.
;
; Controls: joystick port 2, or the keyboard fallback W (thrust up),
; A (thrust left), D (thrust right) -- reusing pong.asm's fixed
; input-reading pattern: JOY2 is captured into a saved copy ONCE before
; any keyboard column-select write touches $DC00, since JOY2 and the
; keyboard column register are the same address (this was a real,
; confirmed bug in an earlier version of pong.asm). W/A are in keyboard
; matrix column 1, D is in column 2, and Y (used to restart after landing
; or crashing) is in column 3 -- all positions taken directly from the
; C64-Wiki's keyboard matrix reference table, not from memory.

CHROUT = $ffd2
JOY2   = $dc00
SCREEN = $0400
COLOR  = $d800

SHIP_DATA = $0d00   ; 64-byte aligned; clear of code, and NOT in the VIC's
                      ; $1000-$1FFF character-ROM shadow range (see
                      ; c64-memory-reference.md sec4 -- this exact mistake
                      ; broke a sprite in an earlier demo)
SPRITE_PTR0 = SCREEN + $3f8

ptr           = $fb   ; temporary 16-bit pointer, used only during setup
ship_x        = $02
ship_y        = $03
vx_dir        = $04   ; 0 = moving left,  1 = moving right
vx_mag        = $05   ; 0-3
vy_dir        = $06   ; 0 = moving up,    1 = moving down
vy_mag        = $07   ; 0-3
fuel          = $08   ; 0-80
physics_delay = $09
fuel_delay    = $0a
thrust_flag   = $0b
left_flag     = $0c
right_flag    = $0d
joy_state     = $0e

MAX_V         = 3
PHYSICS_DELAY = 4      ; frames between physics ticks
FUEL_DELAY    = 3      ; frames between fuel decrements while thrusting
FUEL_START    = 80

YMIN     = 50
XMIN     = 24
XMAX     = 250
GROUND_Y = 210          ; sprite Y at which the ship has touched down

GROUND_ROW    = 20      ; character row the ground line is drawn on
PAD_COL_START = 15
PAD_COL_END   = 25       ; inclusive

PAD_XMIN = 140           ; sprite-X range over the pad, for the landing check
PAD_XMAX = 224
SAFE_V   = 1              ; landing is safe if both vx_mag and vy_mag <= this

        .basic

start:
        ; CIA1 data-direction setup for keyboard scanning: port A
        ; outputs (column select), port B inputs (row read). Every
        ; working reference for manual matrix scanning sets this
        ; explicitly rather than assuming it's already correct.
        lda #%11111111
        sta $dc02
        lda #%00000000
        sta $dc03

        ; --- clear the screen to spaces, black background ---
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldx #4
        lda #$20
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
        lda #1                 ; white (matches the ground/pad characters)
clear_color:
        ldy #$00
clear_color_byte:
        sta (ptr),y
        iny
        bne clear_color_byte
        inc ptr+1
        dex
        bne clear_color

        lda #0
        sta $d020
        sta $d021

        ; --- draw the ground line, with a differently-colored landing pad ---
        lda #<(SCREEN + GROUND_ROW*40)
        sta ptr
        lda #>(SCREEN + GROUND_ROW*40)
        sta ptr+1
        ldy #$00
        lda #$2a                ; screen code '*'
draw_ground:
        sta (ptr),y
        iny
        cpy #40
        bne draw_ground

        lda #<(COLOR + GROUND_ROW*40 + PAD_COL_START)
        sta ptr
        lda #>(COLOR + GROUND_ROW*40 + PAD_COL_START)
        sta ptr+1
        ldy #$00
        lda #5                   ; green -- the landing pad
draw_pad_color:
        sta (ptr),y
        iny
        cpy #(PAD_COL_END - PAD_COL_START + 1)
        bne draw_pad_color

        ; --- set up the ship sprite ---
        lda #(SHIP_DATA / 64)
        sta SPRITE_PTR0
        lda #1                    ; white
        sta $d027
        lda #%00000001
        sta $d015

        ; --- initial state ---
        lda #128
        sta ship_x
        lda #YMIN
        sta ship_y
        lda #1
        sta vx_dir
        lda #0
        sta vx_mag
        lda #1
        sta vy_dir
        lda #0
        sta vy_mag
        lda #FUEL_START
        sta fuel
        lda #PHYSICS_DELAY
        sta physics_delay
        lda #FUEL_DELAY
        sta fuel_delay

main_loop:
        jsr wait_frame
        jsr read_input
        jsr consume_fuel
        jsr do_physics
        jsr update_sprite
        jsr draw_fuel_bar
        jsr check_landing
        jmp main_loop

; Busy-waits for a raster line near the bottom of the display -- syncs
; the main loop to the screen refresh rate for smooth, consistent speed.
wait_frame:
        lda $d012
        cmp #$fb
        bne wait_frame
        rts

; Reads joystick port 2 and the W/A/S keyboard fallback into
; thrust_flag/left_flag/right_flag. JOY2 is saved to joy_state before any
; keyboard scan touches $DC00 (see the header comment).
read_input:
        lda JOY2
        sta joy_state

        lda #0
        sta thrust_flag
        lda joy_state
        and #%00000001
        beq set_thrust
        sei
        lda #%11111101
        sta $dc00
        lda $dc01
        cli
        and #%00000010          ; W
        bne no_thrust_key
set_thrust:
        lda #1
        sta thrust_flag
no_thrust_key:

        lda #0
        sta left_flag
        lda joy_state
        and #%00000100
        beq set_left
        sei
        lda #%11111101
        sta $dc00
        lda $dc01
        cli
        and #%00000100          ; A
        bne no_left_key
set_left:
        lda #1
        sta left_flag
no_left_key:

        lda #0
        sta right_flag
        lda joy_state
        and #%00001000
        beq set_right
        sei
        lda #%11111011           ; select keyboard column 2 (has D)
        sta $dc00
        lda $dc01
        cli
        and #%00000100           ; D
        bne no_right_key
set_right:
        lda #1
        sta right_flag
no_right_key:
        rts

; If fuel is empty, no thruster has any effect regardless of input.
; Otherwise, consume one fuel unit every FUEL_DELAY frames that any
; thruster is active.
consume_fuel:
        lda fuel
        bne fuel_not_empty
        lda #0
        sta thrust_flag
        sta left_flag
        sta right_flag
        rts
fuel_not_empty:
        lda thrust_flag
        ora left_flag
        ora right_flag
        beq consume_fuel_done
        dec fuel_delay
        bne consume_fuel_done
        lda #FUEL_DELAY
        sta fuel_delay
        dec fuel
consume_fuel_done:
        rts

; One tick of acceleration toward accel_want_dir (0 or 1), applied to a
; direction/magnitude pair. accel_v handles vy_dir/vy_mag, accel_h
; handles vx_dir/vx_mag -- separate near-identical routines rather than
; one parameterized one, matching this project's usual style of
; preferring explicit duplication over cleverness on 6502. Accelerating
; in the current direction of travel increases magnitude (up to MAX_V);
; accelerating opposite either decreases magnitude, or -- once magnitude
; hits zero -- flips the direction and starts building speed the other way.
accel_v:
        ldx vy_dir
        cpx accel_want_dir
        beq accel_v_same
        lda vy_mag
        bne accel_v_decrement
        lda accel_want_dir
        sta vy_dir
        lda #1
        sta vy_mag
        rts
accel_v_decrement:
        dec vy_mag
        rts
accel_v_same:
        lda vy_mag
        cmp #MAX_V
        beq accel_v_done
        inc vy_mag
accel_v_done:
        rts

accel_h:
        ldx vx_dir
        cpx accel_want_dir
        beq accel_h_same
        lda vx_mag
        bne accel_h_decrement
        lda accel_want_dir
        sta vx_dir
        lda #1
        sta vx_mag
        rts
accel_h_decrement:
        dec vx_mag
        rts
accel_h_same:
        lda vx_mag
        cmp #MAX_V
        beq accel_h_done
        inc vx_mag
accel_h_done:
        rts

do_physics:
        dec physics_delay
        beq physics_tick
        jmp physics_done
physics_tick:
        lda #PHYSICS_DELAY
        sta physics_delay

        ; --- vertical: thrust accelerates up (0), else gravity (1) ---
        lda thrust_flag
        bne v_want_up
        lda #1
        sta accel_want_dir
        jsr accel_v
        jmp v_move
v_want_up:
        lda #0
        sta accel_want_dir
        jsr accel_v
v_move:
        lda vy_dir
        beq v_moving_up
        lda ship_y
        clc
        adc vy_mag
        sta ship_y
        jmp h_accel
v_moving_up:
        lda ship_y
        sec
        sbc vy_mag
        sta ship_y
        cmp #YMIN
        bcs h_accel
        lda #YMIN
        sta ship_y
        lda #0
        sta vy_mag

h_accel:
        lda left_flag
        bne h_want_left
        lda right_flag
        bne h_want_right
        ; neither thruster held -- damping, no direction change
        lda vx_mag
        beq h_move
        dec vx_mag
        jmp h_move
h_want_left:
        lda #0
        sta accel_want_dir
        jsr accel_h
        jmp h_move
h_want_right:
        lda #1
        sta accel_want_dir
        jsr accel_h
h_move:
        lda vx_dir
        beq h_moving_left
        lda ship_x
        clc
        adc vx_mag
        sta ship_x
        cmp #XMAX
        bcc physics_done
        lda #XMAX
        sta ship_x
        jmp physics_done
h_moving_left:
        lda ship_x
        sec
        sbc vx_mag
        sta ship_x
        cmp #XMIN
        bcs physics_done
        lda #XMIN
        sta ship_x
physics_done:
        rts

update_sprite:
        lda ship_x
        sta $d000
        lda ship_y
        sta $d001
        rts

; Draws the fuel gauge as a bar of '*' characters on the top row, one
; character per 2 fuel units (a plain LSR, since fuel's 0-80 range and
; the screen's 40-column width happen to divide evenly -- much simpler
; than a general decimal conversion routine, which the 6502 has no
; hardware support for anyway).
draw_fuel_bar:
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldy #$00
        lda #$20
clear_fuel_row:
        sta (ptr),y
        iny
        cpy #40
        bne clear_fuel_row

        lda fuel
        lsr a
        beq draw_fuel_done
        tax
        ldy #$00
        lda #$2a
draw_fuel_loop:
        sta (ptr),y
        iny
        dex
        bne draw_fuel_loop
draw_fuel_done:
        rts

check_landing:
        lda ship_y
        cmp #GROUND_Y
        bcc check_landing_done

        lda ship_x
        cmp #PAD_XMIN
        bcc do_crash
        lda ship_x
        cmp #PAD_XMAX+1
        bcs do_crash
        lda vy_mag
        cmp #SAFE_V+1
        bcs do_crash
        lda vx_mag
        cmp #SAFE_V+1
        bcs do_crash

        lda #$93                 ; clear screen
        jsr CHROUT
        lda #<msg_success1
        ldy #>msg_success1
        jsr print_msg
        lda #<msg_success2
        ldy #>msg_success2
        jsr print_msg
        jmp show_try_again

do_crash:
        lda #$93
        jsr CHROUT
        lda #<msg_crash1
        ldy #>msg_crash1
        jsr print_msg
        lda #<msg_crash2
        ldy #>msg_crash2
        jsr print_msg

show_try_again:
        lda #<msg_try_again
        ldy #>msg_try_again
        jsr print_msg
        jmp wait_for_restart

check_landing_done:
        rts

; Polls the keyboard for Y (matrix column 3, bit 1) and, once pressed,
; jumps back to start: which reinitializes and redraws everything and
; runs the game again. Re-running the CIA setup on every restart is
; harmless (it's the same idempotent two writes each time), so there's
; no need for a separate, trimmed entry point.
wait_for_restart:
        sei
        lda #%11110111           ; select keyboard column 3 (has Y)
        sta $dc00
        lda $dc01
        cli
        and #%00000010           ; Y
        bne wait_for_restart
        jmp start

print_msg:
        sta ptr
        sty ptr+1
        ldy #$00
print_msg_loop:
        lda (ptr),y
        beq print_msg_done
        jsr CHROUT
        iny
        bne print_msg_loop
print_msg_done:
        rts

        * = $0c00
accel_want_dir: .byte 0

msg_success1:
        .text "TOUCHDOWN! YOU LANDED SAFELY."
        .byte $0d,0
msg_success2:
        .text "GREAT PILOTING."
        .byte $0d,0
msg_crash1:
        .text "YOU CRASHED!"
        .byte $0d,0
msg_crash2:
        .text "TOO FAST, OR MISSED THE PAD."
        .byte $0d,0
msg_try_again:
        .text "PRESS Y TO TRY AGAIN."
        .byte $0d,0

        * = SHIP_DATA
ship_data:
        ; a small rocket/lander silhouette, tapering to a point at top
        .byte %00000011,%11000000,%00000000
        .byte %00000111,%11100000,%00000000
        .byte %00000111,%11100000,%00000000
        .byte %00001111,%11110000,%00000000
        .byte %00001111,%11110000,%00000000
        .byte %00011111,%11111000,%00000000
        .byte %00011111,%11111000,%00000000
        .byte %00111111,%11111100,%00000000
        .byte %00111111,%11111100,%00000000
        .byte %01111111,%11111110,%00000000
        .byte %01111111,%11111110,%00000000
        .byte %11111111,%11111111,%00000000
        .byte %11111111,%11111111,%00000000
        .byte %01100000,%00000110,%00000000
        .byte %01100000,%00000110,%00000000
        .byte %11100000,%00000111,%00000000
        .byte %11100000,%00000111,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte $00
