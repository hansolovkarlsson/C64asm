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
; A (thrust left), D (thrust right), Y (restart after landing/crashing).
;
; Uses lib/graphics.inc (BITMAP_MODE_ON/OFF, CLEAR_BITMAP,
; SET_SCREEN_COLOR, SPRITE_INIT, wait_frame, sprite0_explode),
; lib/input.inc (read_joy2, READ_KEY, CIA_KEYBOARD_SETUP), lib/sound.inc
; (SID_INIT, PLAY_SOUND, engine_sound_on/off), and lib/text.inc
; (print_msg/PRINT) rather than its own copies of these -- most were
; originally written here and generalized into the library (see each
; file's own header comment for exactly which demo each routine traces
; back to); update_engine_sound in particular was, word for word, where
; sound.inc's engine_sound_on/off themselves came from.
;
; Refactoring the joystick reading onto input.inc's read_joy2/READ_KEY
; also fixed a real bug, the same one found in pong.asm: this file used
; to leave CIA1 port A permanently configured as keyboard column-select
; output from setup onward, so JOY2 reads afterward silently read back
; stale column-select data instead of the joystick. The visible
; symptom here was worse than a stationary paddle -- fuel drained and
; the ship visibly drifted sideways with nothing held at all, exactly
; the "silently unwinnable" kind of bug simulation is good at catching
; in aggregate stats but bad at catching by inspection, since nothing
; about the drift looks obviously wrong frame to frame.

        .basic start   ; needed once library code (wait_frame,
                          ; sprite0_bounce_step, sprite0_explode,
                          ; read_joy2, READ_KEY, engine_sound_on/off,
                          ; print_msg, str_equal) sits between .basic
                          ; and start: -- see c64asm-reference.md §7

ptr             = $fb   ; temporary 16-bit pointer, used only during setup
ship_x          = $02
ship_y          = $03
vx_dir          = $04   ; 0 = moving left,  1 = moving right
vx_mag          = $05   ; 0-3
vy_dir          = $06   ; 0 = moving up,    1 = moving down
vy_mag          = $07   ; 0-3
fuel            = $08   ; 0-80
physics_delay   = $09
fuel_delay      = $0a
thrust_flag     = $0b
left_flag       = $0c
right_flag      = $0d
joy_state       = $0e
ground_row_here = $0f   ; scratch: terrain row under the ship, recomputed
                          ; each frame by check_landing; also reused (as
                          ; rows_remaining) by draw_terrain during setup,
                          ; since that always finishes long before
                          ; check_landing ever runs
rows_remaining  = $0f
ground_y_here   = $10
last_fuel_cols  = $11   ; scratch: fuel-bar column count as of the last
                          ; frame it changed, so draw_fuel_bar can detect
                          ; when (and only when) it needs to do anything
fuel_ptr        = $12   ; 2 bytes ($12/$13): bitmap address of the
                          ; current rightmost lit fuel-bar column
engine_playing  = $14   ; 0/1: is the thruster sound currently gated on?
                          ; ALSO sound.inc's own required flag for the
                          ; same purpose -- engine_sound_on/off (below)
                          ; are called directly now instead of this
                          ; file keeping its own copy of that logic

; text.inc's str_ptr/cmp_ptr/kw_ptr, input.inc's word_dest_ptr, and
; graphics.inc's gfx_ptr are all required (each file's code is
; assembled unconditionally on .include, whether or not this program
; calls everything in it -- see text.inc's header comment) but never
; actually needed at the same moment as ptr's own setup-time use --
; gfx_ptr genuinely is used, by CLEAR_BITMAP/SET_SCREEN_COLOR/
; sprite0_explode below, just never at the same time ptr itself is;
; str_ptr/word_dest_ptr/cmp_ptr/kw_ptr are dead-code requirements only
; (str_equal and extract_word are never actually called here), so
; those four are just as safe aliased onto real gameplay state instead
; -- see graphics.inc's header comment for why a routine that's never
; invoked can never conflict with anything, at any point in time.
str_ptr = ptr
word_dest_ptr = ptr
gfx_ptr = ptr
cmp_ptr = ship_x
kw_ptr = vx_dir

; graphics.inc's sprite0_bounce_step is never called here either (this
; ship's movement is its own accel_v/accel_h physics, not a wall
; bounce), so xpos/ypos/xdir/ydir are dead-code requirements too.
xpos = ship_x
ypos = ship_y
xdir = vx_dir
ydir = vy_dir

MAX_V         = 2      ; lowered from 3 -- a lower top speed means SAFE_V
                          ; (below) covers nearly the whole speed range,
                          ; instead of leaving only a 1-unit margin
PHYSICS_DELAY = 6      ; frames between physics ticks (was 4) -- slower
                          ; gravity and slower-feeling thrust response,
                          ; more time to react
FUEL_START    = 100    ; was 80; a bit more margin now that a full flight
                          ; takes longer in frames
FUEL_DELAY    = 3      ; frames between fuel decrements while thrusting

YMIN     = 50
XMIN     = 24
XMAX     = 250
YMAX     = 255   ; not otherwise used by this program -- only required
                    ; because graphics.inc's sprite0_bounce_step (never
                    ; actually called here) references it

PAD_ROW = 20            ; the terrain row value that marks the flat landing pad
SAFE_V  = 2              ; landing is safe if both vx_mag and vy_mag <= this
                          ; (loosened from 1 -- see the header comment)

; Hand-authored 40-column terrain height profile (character row 0-24,
; higher number = lower on screen), validated in a Python simulation
; before use: hills on both sides, an 11-column flat pad (columns 15-25)
; at PAD_ROW so the landing-success check (see check_landing) can just
; compare against a single row value rather than tracking a separate,
; independently-maintained pixel range that could drift out of sync with
; the drawn terrain.
H0 = 16
H1 = 15
H2 = 17
H3 = 19
H4 = 18
H5 = 16
H6 = 15
H7 = 17
H8 = 19
H9 = 21
H10 = 20
H11 = 18
H12 = 16
H13 = 15
H14 = 17
H15 = 20
H16 = 20
H17 = 20
H18 = 20
H19 = 20
H20 = 20
H21 = 20
H22 = 20
H23 = 20
H24 = 20
H25 = 20
H26 = 18
H27 = 16
H28 = 15
H29 = 17
H30 = 19
H31 = 21
H32 = 22
H33 = 20
H34 = 18
H35 = 17
H36 = 19
H37 = 21
H38 = 20
H39 = 18

        .include "lib/graphics.inc"
        .include "lib/input.inc"
        .include "lib/sound.inc"
        .include "lib/text.inc"

start:
        CIA_KEYBOARD_SETUP   ; sets CIA1 port B to all-input, for reading
                                ; keyboard row data; port A's direction is
                                ; managed per-call by read_joy2/READ_KEY
                                ; themselves, not set once here -- see the
                                ; header comment above for why that
                                ; per-call approach is the actual fix

        SID_INIT
        sta engine_playing        ; SID_INIT leaves A=$00 -- reuse it rather
                                     ; than an extra `lda #0`

        BITMAP_MODE_ON BITMAP
        CLEAR_BITMAP BITMAP

        ; --- fill the screen memory nibble data (fg=high nibble, bg=low
        ; nibble, per 8x8 cell) with grey terrain on black sky. This is
        ; standard (non-multicolor) bitmap mode, where SCREEN holds this
        ; per-cell color data and COLOR RAM is not used at all. ---
        SET_SCREEN_COLOR $c0   ; grey terrain (high nibble) on black sky
                                  ; (low nibble)

        lda #0
        sta VIC_BORDER
        sta VIC_BG0

        ; --- color the landing pad columns (15-25) green, all 25 rows;
        ; safe to do for every row even above the terrain line, since the
        ; sky rows there show black background regardless of this
        ; foreground nibble (no bits are set for the VIC to draw with it) ---
        lda #<(SCREEN + 15)
        sta ptr
        lda #>(SCREEN + 15)
        sta ptr+1
        ldx #25
draw_pad_color_row:
        ldy #$00
        lda #$50                 ; green terrain (high nibble) on black sky
draw_pad_color_byte:
        sta (ptr),y
        iny
        cpy #11
        bne draw_pad_color_byte
        clc
        lda ptr
        adc #40
        sta ptr
        lda ptr+1
        adc #0
        sta ptr+1
        dex
        bne draw_pad_color_row

        ; --- color row 0 (the fuel bar) yellow on black, once ---
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldy #$00
        lda #$70                 ; yellow fuel bar (high nibble) on black (low nibble)
color_fuel_row:
        sta (ptr),y
        iny
        cpy #40
        bne color_fuel_row

        jsr draw_terrain
        jsr init_fuel_bar

        ; --- set up the ship sprite ---
        SPRITE_INIT ship_data, 1, 128, YMIN   ; white

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
        jsr update_engine_sound
        jsr do_physics
        jsr update_sprite
        jsr draw_fuel_bar
        jsr check_landing
        jmp main_loop

; Reads joystick port 2 (via input.inc's read_joy2) and the W/A/D
; keyboard fallback (via READ_KEY) into thrust_flag/left_flag/
; right_flag. joy_state is captured once per frame, before any
; READ_KEY call -- not because of any ordering requirement between the
; two anymore (read_joy2/READ_KEY each fix up CIA1's direction register
; themselves before they touch it, so they can run in either order
; safely -- see input.inc's header comment), just so joy_state's value
; stays consistent across all three checks below within the same frame.
read_input:
        jsr read_joy2
        sta joy_state           ; active-HIGH (1=pressed) -- read_joy2's
                                   ; own convention, see input.inc

        lda #0
        sta thrust_flag
        lda joy_state
        and #%00000001
        bne set_thrust
        READ_KEY %11111101, %00000010   ; W key (matrix column 1, bit 1)
        beq no_thrust_key
set_thrust:
        lda #1
        sta thrust_flag
no_thrust_key:

        lda #0
        sta left_flag
        lda joy_state
        and #%00000100
        bne set_left
        READ_KEY %11111101, %00000100   ; A key (matrix column 1, bit 2)
        beq no_left_key
set_left:
        lda #1
        sta left_flag
no_left_key:

        lda #0
        sta right_flag
        lda joy_state
        and #%00001000
        bne set_right
        READ_KEY %11111011, %00000100   ; D key (matrix column 2, bit 2)
        beq no_right_key
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

; Dispatches to sound.inc's engine_sound_on/off based on whether any
; thruster is active this frame -- see that file for the actual
; gate-retriggering logic (this file's own copy of it, word for word,
; is where sound.inc's version originally came from).
update_engine_sound:
        lda thrust_flag
        ora left_flag
        ora right_flag
        beq engine_should_stop
        jmp engine_sound_on
engine_should_stop:
        jmp engine_sound_off

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
        sta SPRITE0_X
        lda ship_y
        sta SPRITE0_Y
        rts

; Fills each of the 40 columns' bitmap cells solid from its terrain row
; down to the bottom of the screen (row 24), using the precomputed
; terrain_addr table so no runtime multiplication is needed.
draw_terrain:
        ldx #$00
draw_terrain_loop:
        txa
        asl a
        tay
        lda terrain_addr,y
        sta ptr
        lda terrain_addr+1,y
        sta ptr+1

        lda #25
        sec
        sbc terrain_row,x
        sta rows_remaining

        lda #$ff
draw_terrain_row:
        ldy #$00
draw_terrain_byte:
        sta (ptr),y
        iny
        cpy #8
        bne draw_terrain_byte
        pha                       ; save $ff -- about to clobber A for the 16-bit add
        clc
        lda ptr
        adc #<320                 ; advance one full row down (40 cols * 8 bytes)
        sta ptr
        lda ptr+1
        adc #>320
        sta ptr+1
        pla
        dec rows_remaining
        bne draw_terrain_row

        inx
        cpx #40
        bne draw_terrain_loop
        rts

; Fills the initial fuel bar (called once at start/restart) and sets up
; fuel_ptr/last_fuel_cols for draw_fuel_bar's per-frame delta updates.
; The column count is clamped to 40 (the screen width): FUEL_START can be
; larger than 2*40, and without this clamp a full-fuel bar would try to
; draw past the last column and corrupt row 1 of the terrain bitmap, one
; column for every 2 fuel units over 80 -- exactly what was happening
; with FUEL_START=100 and no clamp, and the likely cause of the reported
; "flickering yellow mess" (real terrain data getting overwritten every
; single frame near the left edge).
init_fuel_bar:
        lda #<BITMAP
        sta ptr
        lda #>BITMAP
        sta ptr+1

        lda #(FUEL_START/2)
        cmp #41
        bcc init_fuel_cols_ok
        lda #40
init_fuel_cols_ok:
        sta last_fuel_cols
        tax
init_fuel_col:
        ldy #$00
        lda #$ff
init_fuel_byte:
        sta (ptr),y
        iny
        cpy #8
        bne init_fuel_byte
        clc
        lda ptr
        adc #8
        sta ptr
        lda ptr+1
        adc #0
        sta ptr+1
        dex
        bne init_fuel_col

        ; ptr now points just past the last lit column; step back 8 for
        ; that column's own address, which becomes fuel_ptr
        sec
        lda ptr
        sbc #8
        sta fuel_ptr
        lda ptr+1
        sbc #0
        sta fuel_ptr+1
        rts

; Per-frame: if the fuel-bar column count (clamped the same way as
; init_fuel_bar) has decreased since the last change, erases exactly the
; one column that dropped off and steps fuel_ptr one column to the left.
; Not a full redraw -- this does nothing at all on the great majority of
; frames where fuel hasn't changed, which avoids both wasted CPU time and
; the visible flicker that came from clearing and redrawing the whole
; 320-byte row every single frame regardless of whether anything had
; actually changed.
draw_fuel_bar:
        lda fuel
        lsr a
        cmp #41
        bcc fuel_cols_check_ok
        lda #40
fuel_cols_check_ok:
        cmp last_fuel_cols
        beq draw_fuel_done
        sta last_fuel_cols

        ldy #$00
        lda #$00
clear_one_fuel_col:
        sta (fuel_ptr),y
        iny
        cpy #8
        bne clear_one_fuel_col

        sec
        lda fuel_ptr
        sbc #8
        sta fuel_ptr
        lda fuel_ptr+1
        sbc #0
        sta fuel_ptr+1
draw_fuel_done:
        rts

; Looks up the terrain height at the ship's current column and compares
; against it instead of a single flat ground level. "On the pad" now
; just means the terrain row there equals PAD_ROW -- tying the landing
; check directly to the same data the terrain was drawn from, rather
; than maintaining a separate pixel range that could drift out of sync.
check_landing:
        lda ship_x
        sec
        sbc #XMIN
        lsr a
        lsr a
        lsr a
        tax
        lda terrain_row,x
        sta ground_row_here
        asl a
        asl a
        asl a
        clc
        adc #YMIN
        sta ground_y_here

        lda ship_y
        cmp ground_y_here
        bcc check_landing_done

        ; Touched down -- snap to the exact ground surface and do one
        ; final sprite update, on both outcomes, so the ship visually
        ; rests right at the terrain line instead of appearing to
        ; overlap however many pixels it overshot by on the final move.
        lda ground_y_here
        sta ship_y
        jsr update_sprite

        lda ground_row_here
        cmp #PAD_ROW
        bne do_crash
        lda vy_mag
        cmp #SAFE_V+1
        bcs do_crash
        lda vx_mag
        cmp #SAFE_V+1
        bcs do_crash

        ; Switch back to standard text mode before printing anything --
        ; bitmap mode has no mechanism to render PETSCII text at all.
        ; See graphics.inc's own comment on BITMAP_MODE_OFF for why
        ; this step specifically is easy to forget and what it looks
        ; like when you do (this exact bug, right here, is where that
        ; macro's warning comes from).
        BITMAP_MODE_OFF

        CLS
        PRINT msg_success1
        PRINT msg_success2

        lda #%10000000            ; make sure the engine sound is off --
        sta VOICE1_CTRL            ; thrust might have been held right up
        lda #0                      ; to touchdown
        sta engine_playing
        jsr play_success_melody

        jmp show_try_again

do_crash:
        jsr show_explosion

        BITMAP_MODE_OFF

        CLS
        PRINT msg_crash1
        PRINT msg_crash2

show_try_again:
        PRINT msg_try_again
        jmp wait_for_restart

check_landing_done:
        rts

; A quick "boom": the sprite doubles in size and flashes through a few
; fire colors for about a quarter of a second, then disappears
; entirely. Runs while still in bitmap mode, at the ship's actual crash
; position (already snapped to the ground by check_landing above), so it
; visibly happens right where the ship hit. The visual effect itself is
; graphics.inc's sprite0_explode -- this file is where that routine was
; generalized from -- with this program's own fire-colored sequence.
show_explosion:
        ; the engine sound might still be gated on if thrust was held
        ; right up to the moment of impact -- silence it before the
        ; crash sound so they don't overlap
        jsr engine_sound_off

        PLAY_SOUND $04, $0f, $00, %10000001   ; low pitch, long rumbling
                                                 ; decay, noise waveform
        ldx #8
        lda #<explosion_colors
        ldy #>explosion_colors
        jsr sprite0_explode
        rts

; A short 5-note ascending arpeggio played once on a successful landing.
; Each note is gated on, held briefly, then gated off with a short gap
; before the next -- unlike the engine sound, these are meant to be
; individually audible notes, not one continuous tone, so each one gets
; its own fresh gate 0->1 transition (needed to retrigger the envelope's
; attack) rather than just changing frequency while gated on.
play_success_melody:
        ldx #$00
melody_loop:
        lda #$00
        sta VOICE1_CTRL             ; gate off first (fresh retrigger)
        lda melody_freq_lo,x
        sta VOICE1_FREQ_LO
        lda melody_freq_hi,x
        sta VOICE1_FREQ_HI
        lda #$00
        sta VOICE1_AD                 ; attack 0, decay 0 -- snappy onset
        lda #$a0
        sta VOICE1_SR                  ; sustain 10 (holds while gated), release 0
        lda #%00010001                  ; triangle waveform, gate on
        sta VOICE1_CTRL

        jsr wait_frame
        jsr wait_frame
        jsr wait_frame
        jsr wait_frame
        jsr wait_frame
        jsr wait_frame

        lda #%00010000              ; triangle waveform, gate off
        sta VOICE1_CTRL
        jsr wait_frame
        jsr wait_frame

        inx
        cpx #5
        bne melody_loop
        rts

; Polls the keyboard for Y and, once pressed, jumps back to start: which
; reinitializes and redraws everything and runs the game again.
; Re-running CIA_KEYBOARD_SETUP on every restart is harmless (it's the
; same idempotent write each time), so there's no need for a separate,
; trimmed entry point.
wait_for_restart:
        READ_KEY %11110111, %00000010   ; Y key (matrix column 3, bit 1)
        beq wait_for_restart
        jmp start

; terrain_row[col] is the character row (0-24) the ground starts at for
; that column. terrain_addr[col] is the corresponding starting bitmap
; address for that column's fill (row*320 + col*8 bytes into the 8K
; bitmap) -- computed here at assemble time from the same H0-H39
; constants terrain_row uses, rather than at runtime, since the 6502 has
; no multiply instruction and this only needs computing once anyway.
terrain_row:
        .byte H0,H1,H2,H3,H4,H5,H6,H7,H8,H9
        .byte H10,H11,H12,H13,H14,H15,H16,H17,H18,H19
        .byte H20,H21,H22,H23,H24,H25,H26,H27,H28,H29
        .byte H30,H31,H32,H33,H34,H35,H36,H37,H38,H39

terrain_addr:
        .word BITMAP+H0*320+0*8,   BITMAP+H1*320+1*8,   BITMAP+H2*320+2*8,   BITMAP+H3*320+3*8
        .word BITMAP+H4*320+4*8,   BITMAP+H5*320+5*8,   BITMAP+H6*320+6*8,   BITMAP+H7*320+7*8
        .word BITMAP+H8*320+8*8,   BITMAP+H9*320+9*8,   BITMAP+H10*320+10*8, BITMAP+H11*320+11*8
        .word BITMAP+H12*320+12*8, BITMAP+H13*320+13*8, BITMAP+H14*320+14*8, BITMAP+H15*320+15*8
        .word BITMAP+H16*320+16*8, BITMAP+H17*320+17*8, BITMAP+H18*320+18*8, BITMAP+H19*320+19*8
        .word BITMAP+H20*320+20*8, BITMAP+H21*320+21*8, BITMAP+H22*320+22*8, BITMAP+H23*320+23*8
        .word BITMAP+H24*320+24*8, BITMAP+H25*320+25*8, BITMAP+H26*320+26*8, BITMAP+H27*320+27*8
        .word BITMAP+H28*320+28*8, BITMAP+H29*320+29*8, BITMAP+H30*320+30*8, BITMAP+H31*320+31*8
        .word BITMAP+H32*320+32*8, BITMAP+H33*320+33*8, BITMAP+H34*320+34*8, BITMAP+H35*320+35*8
        .word BITMAP+H36*320+36*8, BITMAP+H37*320+37*8, BITMAP+H38*320+38*8, BITMAP+H39*320+39*8

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

; yellow, orange, red, white, repeated twice -- a quick fire-colored flash
explosion_colors:
        .byte 7,8,2,1,7,8,2,1

; an ascending 5-note arpeggio for the success melody
melody_freq_lo:
        .byte $00,$00,$00,$00,$00
melody_freq_hi:
        .byte $0c,$10,$14,$18,$20

        ; NOTE: sprite/bitmap/screen data must avoid $1000-$1FFF (and
        ; $9000-$9FFF) within the current VIC bank -- the VIC-II always
        ; substitutes the character ROM for its own reads in those ranges,
        ; regardless of what's actually stored there in RAM. .align (rather
        ; than a fixed address) means this always lands correctly right
        ; after the code above, whatever that code's exact size happens to
        ; be -- see c64asm-reference.md §7 for the directive itself.
        .align 64
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
