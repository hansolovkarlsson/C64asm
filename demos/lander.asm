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
VOICE1_FREQ_LO = $d400
VOICE1_FREQ_HI = $d401
VOICE1_CTRL    = $d404
VOICE1_AD      = $d405
VOICE1_SR      = $d406
SID_VOLUME     = $d418
SCREEN = $0400
COLOR  = $d800
BITMAP = $2000   ; 8K-aligned bitmap data; same proven-safe address bounce.asm uses

SHIP_DATA = $0e00   ; 64-byte aligned; clear of code, and NOT in the VIC's
                      ; $1000-$1FFF character-ROM shadow range (see
                      ; c64-memory-reference.md sec4 -- this exact mistake
                      ; broke a sprite in an earlier demo)
SPRITE_PTR0 = SCREEN + $3f8

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

        lda #$0f
        sta SID_VOLUME            ; full volume, no filter
        lda #$00
        sta VOICE1_CTRL            ; make sure voice 1 starts silent
        sta engine_playing

        ; --- fill the screen memory nibble data (fg=high nibble, bg=low
        ; nibble, per 8x8 cell) with grey terrain on black sky. This is
        ; standard (non-multicolor) bitmap mode, where SCREEN holds this
        ; per-cell color data and COLOR RAM is not used at all. ---
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldx #4
        lda #$c0                ; grey terrain (high nibble) on black sky (low nibble)
clear_screen:
        ldy #$00
clear_screen_byte:
        sta (ptr),y
        iny
        bne clear_screen_byte
        inc ptr+1
        dex
        bne clear_screen

        lda #0
        sta $d020
        sta $d021

        ; --- switch on standard bitmap mode ---
        lda $d011
        ora #%00100000           ; BMM
        sta $d011
        lda $d018
        and #%11110000           ; keep the screen-pointer bits (still $0400)
        ora #%00001000           ; bitmap pointer bit 3 set -> bitmap at $2000
        sta $d018

        ; --- clear the 8K bitmap (32 pages of 256 bytes) to all-sky ---
        lda #<BITMAP
        sta ptr
        lda #>BITMAP
        sta ptr+1
        ldx #32
        lda #$00
clear_bitmap:
        ldy #$00
clear_bitmap_byte:
        sta (ptr),y
        iny
        bne clear_bitmap_byte
        inc ptr+1
        dex
        bne clear_bitmap

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
        jsr update_engine_sound
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

; Keeps the thruster engine sound gated on for as long as any thruster
; is active this frame, and gated off otherwise -- checked once per
; frame, but only actually *written* to the SID on the frame the state
; changes, so holding a thruster down doesn't repeatedly retrigger the
; envelope's attack phase (which would make it stutter instead of
; sounding like one continuous engine note). Uses a sustain-heavy
; envelope, unlike the fire-and-forget effects elsewhere in this
; project, since this needs to keep sounding for as long as the gate is
; held rather than naturally fade out on its own.
update_engine_sound:
        lda thrust_flag
        ora left_flag
        ora right_flag
        beq engine_should_stop

        lda engine_playing
        bne engine_sound_done      ; already playing -- don't retrigger
        lda #$00
        sta VOICE1_FREQ_LO
        lda #$08
        sta VOICE1_FREQ_HI
        lda #$00
        sta VOICE1_AD               ; attack 0, decay 0
        lda #$f8
        sta VOICE1_SR                ; sustain 15 (holds while gated), release 8
        lda #%10000001                 ; noise waveform, gate on
        sta VOICE1_CTRL
        lda #1
        sta engine_playing
        rts

engine_should_stop:
        lda engine_playing
        beq engine_sound_done
        lda #%10000000            ; noise waveform, gate off
        sta VOICE1_CTRL
        lda #0
        sta engine_playing
engine_sound_done:
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
        ; CHROUT's clear-screen and character output both assume SCREEN
        ; memory holds character codes; in bitmap mode it holds color
        ; nibbles instead, so "printing" a message there was really just
        ; overwriting each cell's color with the character code's numeric
        ; value, while the old terrain/ship pixels stayed in the bitmap
        ; underneath -- which is exactly the "mess of colored blobs".
        lda $d011
        and #%11011111           ; clear BMM
        sta $d011
        lda #$14                  ; screen at $0400, characters at $1000
        sta $d018                  ; (the standard default char ROM shadow)

        lda #$93                 ; clear screen
        jsr CHROUT
        lda #<msg_success1
        ldy #>msg_success1
        jsr print_msg
        lda #<msg_success2
        ldy #>msg_success2
        jsr print_msg

        lda #%10000000            ; make sure the engine sound is off --
        sta VOICE1_CTRL            ; thrust might have been held right up
        lda #0                      ; to touchdown
        sta engine_playing
        jsr play_success_melody

        jmp show_try_again

do_crash:
        jsr show_explosion

        lda $d011
        and #%11011111
        sta $d011
        lda #$14
        sta $d018

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

; A quick "boom": the sprite doubles in size and flashes through a few
; fire colors for about a quarter of a second (each color held 2 frames,
; synced via wait_frame same as everywhere else), then disappears
; entirely. Runs while still in bitmap mode, at the ship's actual crash
; position (already snapped to the ground by check_landing above), so it
; visibly happens right where the ship hit.
show_explosion:
        ; the engine sound might still be gated on if thrust was held
        ; right up to the moment of impact -- silence it before the
        ; crash sound so they don't overlap
        lda #%10000000
        sta VOICE1_CTRL
        lda #0
        sta engine_playing

        lda #$00
        sta VOICE1_CTRL           ; force gate off first (fresh retrigger)
        lda #$00
        sta VOICE1_FREQ_LO
        lda #$04
        sta VOICE1_FREQ_HI        ; low pitch for a deep boom
        lda #$0f
        sta VOICE1_AD              ; attack 0, decay 15 (long, rumbling decay)
        lda #$00
        sta VOICE1_SR               ; sustain 0, release 0
        lda #%10000001                ; noise waveform, gate on
        sta VOICE1_CTRL

        lda #%00000001
        sta $d01d                ; sprite 0 X-expand
        sta $d017                 ; sprite 0 Y-expand
        ldx #$00
explosion_loop:
        lda explosion_colors,x
        sta $d027
        jsr wait_frame
        jsr wait_frame
        inx
        cpx #8
        bne explosion_loop
        lda #0
        sta $d015                 ; hide the ship
        sta $d01d                  ; tidy up the expand bits (harmless either way now)
        sta $d017
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

        * = $0cf0
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
