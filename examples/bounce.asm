; bounce.asm - a sprite bouncing around a bitmap graphics screen
;
; Turns on standard (hi-res) bitmap mode, fills it with a solid color,
; and animates one sprite bouncing off all four edges of the visible
; screen, synced to the display's raster for smooth, consistent motion.
; Plays a short sound effect on each bounce.
;
; Uses lib/graphics.inc (BITMAP_MODE_ON, CLEAR_BITMAP, SET_SCREEN_COLOR,
; SPRITE_INIT, wait_frame, sprite0_bounce_step) rather than its own
; copies of these -- they were originally written here (and, for
; wait_frame, independently duplicated again in pong.asm and
; lander.asm), then generalized into the library. What's specific to
; *this* particular demo -- the actual sprite image, and the exact
; bounds it bounces within -- stays here. The bounce sound itself is
; lib/sound.inc's PLAY_SOUND, with the same frequency/envelope/waveform
; as pong.asm's own already-proven play_wall_bounce effect.

        .basic start   ; needed once library code (graphics.inc's
                          ; wait_frame/sprite0_bounce_step, sound.inc's
                          ; engine_sound_on/off) sits between .basic and
                          ; start: -- see c64asm-reference.md §7

gfx_ptr = $fb               ; graphics.inc's required zero-page scratch
xpos = $02                 ; sprite X position -- TWO bytes (xpos, xpos+1:
                              ; low/high byte of a 16-bit value), since the
                              ; screen's right edge is itself past 255 --
                              ; see graphics.inc's own note on this
ypos = $04                 ; sprite Y position (one byte -- Y never needs
                              ; a second byte the way X does)
xdir = $05                 ; 1 = moving right, 0 = moving left
ydir = $06                 ; 1 = moving down,  0 = moving up
engine_playing = $07     ; sound.inc's required flag byte -- its
                            ; engine_sound_on/off code is assembled
                            ; unconditionally once sound.inc is
                            ; included, even though this demo never
                            ; actually calls either

; The ball should visually touch each edge of the visible screen, not
; stop noticeably short of it -- so these aren't just "a comfortable
; margin", they're the actual documented visible-screen edges (left
; X=24, top Y=50, right X=344, bottom Y=250 -- see c64-memory-
; reference.md's sprite positioning section) with the sprite's own
; width (24px) and height (21px) subtracted from the far bounds, so
; it's the sprite's OWN far edge that reaches the screen edge, not its
; near one running past it. XMAX (320) is past 255, which is exactly
; why sprite0_bounce_step needs the two-byte xpos above -- see its own
; comment in graphics.inc for the X-MSB handling this requires and
; c64-memory-reference.md's "Positioning a sprite beyond X=255" for the
; general technique.
XMIN = 24
XMAX = 320
YMIN = 50
YMAX = 229

        .include "lib/graphics.inc"
        .include "lib/sound.inc"

start:
        BITMAP_MODE_ON BITMAP
        CLEAR_BITMAP BITMAP
        SET_SCREEN_COLOR %00010110   ; fg = white(1), bg = blue(6) -- fg is
                                        ; unused since every bitmap byte is
                                        ; 0, so only the low nibble (bg)
                                        ; actually shows on screen

        lda #6               ; border matches the bitmap's background color
        sta VIC_BORDER

        SID_INIT

        lda #<XMIN
        sta xpos
        lda #>XMIN
        sta xpos+1           ; XMIN is well under 256, but xpos+1 still
                               ; needs an explicit, correct starting value
                               ; -- see graphics.inc's sprite0_bounce_step
        lda #YMIN
        sta ypos
        lda #1
        sta xdir             ; start out moving right...
        sta ydir             ; ...and down

        lda SPRITE_X_MSB
        and #%11111110       ; sprite 0's X starts under 256 (XMIN=24),
        sta SPRITE_X_MSB       ; so its MSB bit should start cleared too

        SPRITE_INIT sprite_data, 1, XMIN, YMIN   ; color = white

main_loop:
        jsr wait_frame
        jsr sprite0_bounce_step
        cpx #0               ; sprite0_bounce_step (graphics.inc) returns
        bne bounced           ; whether X and/or Y bounced this frame in
        cpy #0                ; the X/Y registers themselves -- see its
        beq no_bounce         ; own comment for why
bounced:
        PLAY_SOUND $18, $06, $00, %00010001   ; same effect as pong.asm's
                                                 ; own proven play_wall_bounce
no_bounce:
        jmp main_loop

        ; NOTE: sprite/bitmap/screen data must avoid $1000-$1FFF (and
        ; $9000-$9FFF) within the current VIC bank -- the VIC-II always
        ; substitutes the character ROM for its own reads in those ranges,
        ; regardless of what's actually stored there in RAM. .align (rather
        ; than a fixed address like the earlier $0900) means this always
        ; lands correctly right after the code above, whatever that code's
        ; exact size happens to be, without needing to hand-pick and
        ; re-check an address by hand each time it changes -- see
        ; c64asm-reference.md §7 for the directive itself.
        .align 64
sprite_data:
        ; a simple 24x21 filled circle ("ball")
        .byte %00111100,%00000000,%00000000
        .byte %01111110,%00000000,%00000000
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
        .byte %01111110,%00000000,%00000000
        .byte %00111100,%00000000,%00000000
        .byte %00000000,%00000000,%00000000
        .byte $00                              ; pad to a 64-byte block
