; bounce.asm - a sprite bouncing around a bitmap graphics screen
;
; Turns on standard (hi-res) bitmap mode, fills it with a solid color,
; and animates one sprite bouncing off all four edges of the visible
; screen, synced to the display's raster for smooth, consistent motion.

BITMAP      = $2000        ; 8K bitmap data, 8-aligned within the VIC bank
SCREEN      = $0400        ; screen/color-nibble area (bitmap mode) + sprite pointers
SPRITE_DATA = $0900        ; 64-byte aligned; see note below on why $1000 won't work
SPRITE_PTR0 = SCREEN + $3f8   ; = $07F8, sprite 0's pointer slot

ptr  = $fb                 ; temporary 16-bit pointer, used only during setup
xpos = $02                 ; sprite X position (kept to a single byte -- see note)
ypos = $03                 ; sprite Y position
xdir = $04                 ; 1 = moving right, 0 = moving left
ydir = $05                 ; 1 = moving down,  0 = moving up

; Bounds are chosen to keep the whole demo within a single-byte X range
; (0-255) so no X-MSB handling ($D010) is needed -- see c64-memory-
; reference.md §4 for how to extend this to the full 320-pixel width.
XMIN = 24
XMAX = 250
YMIN = 50
YMAX = 220

        .basic

start:
        ; --- switch on standard bitmap mode ---
        lda $d011
        ora #%00100000        ; BMM
        sta $d011
        lda $d018
        and #%11110000        ; keep the screen-pointer bits (still $0400)
        ora #%00001000        ; bitmap pointer bit 3 set -> bitmap at $2000
        sta $d018

        ; --- clear the 8K bitmap (32 pages of 256 bytes) to all-background ---
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

        ; --- set the screen/color area: high nibble = fg, low nibble = bg ---
        ; (foreground is unused since every bitmap byte is 0, so only the
        ; low nibble -- the background color -- actually shows on screen)
        lda #<SCREEN
        sta ptr
        lda #>SCREEN
        sta ptr+1
        ldx #4
        lda #%00010110       ; fg = white(1), bg = blue(6)
fill_colors:
        ldy #$00
fill_colors_byte:
        sta (ptr),y
        iny
        bne fill_colors_byte
        inc ptr+1
        dex
        bne fill_colors

        lda #6               ; border matches the bitmap's background color
        sta $d020

        ; --- set up the sprite ---
        lda #(SPRITE_DATA / 64)
        sta SPRITE_PTR0
        lda #1               ; sprite color = white
        sta $d027
        lda #%00000001
        sta $d015            ; enable sprite 0 (only)

        lda #XMIN
        sta xpos
        lda #YMIN
        sta ypos
        lda #1
        sta xdir             ; start out moving right...
        sta ydir             ; ...and down

main_loop:
        jsr wait_frame
        jsr move_sprite
        jmp main_loop

; Busy-waits for a raster line near the bottom of the visible display --
; a simple polling way to sync the main loop to the screen's refresh rate
; (roughly 50/60 Hz) so the sprite moves at a smooth, consistent speed
; regardless of how many cycles the rest of the loop happens to take.
wait_frame:
        lda $d012
        cmp #$fb
        bne wait_frame
        rts

move_sprite:
        lda xdir
        beq move_left
        inc xpos
        lda xpos
        cmp #XMAX
        bne x_done
        lda #0
        sta xdir             ; hit the right edge -- reverse
        jmp x_done
move_left:
        dec xpos
        lda xpos
        cmp #XMIN
        bne x_done
        lda #1
        sta xdir             ; hit the left edge -- reverse
x_done:
        lda ydir
        beq move_up
        inc ypos
        lda ypos
        cmp #YMAX
        bne y_done
        lda #0
        sta ydir             ; hit the bottom edge -- reverse
        jmp y_done
move_up:
        dec ypos
        lda ypos
        cmp #YMIN
        bne y_done
        lda #1
        sta ydir             ; hit the top edge -- reverse
y_done:
        lda xpos
        sta $d000
        lda ypos
        sta $d001
        rts

        ; NOTE: sprite/bitmap/screen data must avoid $1000-$1FFF (and
        ; $9000-$9FFF) within the current VIC bank -- the VIC-II always
        ; substitutes the character ROM for its own reads in those ranges,
        ; regardless of what's actually stored there in RAM. $0900 is safe.
        * = SPRITE_DATA
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
