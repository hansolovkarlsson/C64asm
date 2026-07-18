; graphics.inc - standard bitmap mode, sprite setup, and the raster-sync/
; bounce-movement routines this project's own bitmap/sprite demos each
; ended up needing.
;
; Provides:
;   BITMAP_MODE_ON bitmap_addr    macro
;   BITMAP_MODE_OFF                macro
;   CLEAR_BITMAP bitmap_addr       macro
;   SET_SCREEN_COLOR value         macro
;   SPRITE_INIT data, color, x, y  macro
;   wait_frame                     subroutine
;   sprite0_bounce_step            subroutine
;   sprite0_explode                subroutine
;
; bitmap_addr must be one of the 8 addresses the VIC-II can use for
; bitmap data within its current 16K bank: a multiple of $2000 relative
; to the bank's own base (this library assumes the default bank at
; $0000, matching how this project's own demos leave VIC_BANK
; unconfigured -- see c64-memory-reference.md if you need a different
; bank). hardware.inc's BITMAP ($2000) is a ready-to-use default.
;
; IMPORTANT: BITMAP_MODE_ON only flips the VIC-II into bitmap mode --
; it does not clear anything. Immediately afterward, always call both
; CLEAR_BITMAP (zeroes the actual pixel data) and SET_SCREEN_COLOR (see
; below for why this one specifically is easy to forget and looks
; broken in a very particular way if you do). CLEAR_BITMAP/
; SET_SCREEN_COLOR need a 2-byte zero-page scratch location named
; gfx_ptr, defined by the including program (this library doesn't claim
; a zero-page address of its own -- see text.inc's header comment for
; the same reasoning applied to str_ptr).
;
; sprite0_bounce_step needs eight more, unrelated to gfx_ptr: xpos
; (TWO bytes -- xpos and xpos+1, the low/high byte of a 16-bit X
; position, since the visible screen's right edge is itself past 255),
; ypos, xdir, ydir (one byte each -- see sprite0_bounce_step's own
; comment below for exactly what they're for and why, unlike gfx_ptr,
; none of these have to be zero page at all), and XMIN, XMAX, YMIN,
; YMAX (compile-time constants, the bounds to bounce within).
;
; gfx_ptr and all eight of sprite0_bounce_step's symbols are required
; as soon as you .include this file, EVEN IF your program never calls
; CLEAR_BITMAP/SET_SCREEN_COLOR or sprite0_bounce_step: like every file
; in this library, .include splices in graphics.inc's code
; unconditionally, and that code references these symbols regardless
; of whether anything ever jsr's to it or expands the relevant macro.
; A program that only wants BITMAP_MODE_ON/BITMAP_MODE_OFF still needs
; all nine defined (xpos+1 included, ten addresses total), or assembly
; fails on an undefined symbol inside code it never actually uses.
;
; SPRITE_INIT's `data` argument must itself be 64-byte aligned (see
; this assembler's .align directive) and, like any sprite data on this
; machine, must NOT fall in $1000-$1FFF or $9000-$9FFF within the
; current VIC bank -- the VIC-II substitutes character ROM for any read
; in that range regardless of what's actually in RAM there, which
; turns sprite (or bitmap) data placed there into scrambled character
; shapes instead. This exact bug showed up more than once across this
; project's own demos before the pattern below (a comfortably-clear
; address like $0d00 or $0e00) became the standard.

.include "hardware.inc"

; BITMAP_MODE_ON bitmap_addr -- switches on standard (non-multicolor)
; bitmap mode, with bitmap data at bitmap_addr and screen (color) data
; left at its default location (SCREEN, $0400). Does not clear
; anything -- see CLEAR_BITMAP and SET_SCREEN_COLOR below, which you
; should call immediately after this, essentially always.
.macro BITMAP_MODE_ON bitmap_addr
        lda VIC_CTRL1
        ora #%00100000        ; BMM
        sta VIC_CTRL1
        lda VIC_MEMPTRS
        and #%11110000        ; keep the screen-pointer bits
        ora #((\bitmap_addr) / 1024)   ; bitmap mode has only 2 possible
                                          ; locations within the bank, so
                                          ; this lands directly on bit 3:
                                          ; 0 for offset $0000, 8 for $2000
        sta VIC_MEMPTRS
.endmacro

; CLEAR_BITMAP bitmap_addr -- fills all 8000 bytes of bitmap data at
; bitmap_addr with zero (every pixel becomes "background color").
; bitmap_addr must be page-aligned (a multiple of 256) for this loop's
; addressing to work correctly -- every legal VIC-II bitmap address
; already satisfies this, since bitmap mode's only two possible
; locations ($0000 and $2000, bank-relative) both are.
.macro CLEAR_BITMAP bitmap_addr
        lda #<(\bitmap_addr)
        sta gfx_ptr
        lda #>(\bitmap_addr)
        sta gfx_ptr+1
        ldx #32
        lda #$00
@clear_loop:
        ldy #$00
@clear_byte:
        sta (gfx_ptr),y
        iny
        bne @clear_byte
        inc gfx_ptr+1
        dex
        bne @clear_loop
.endmacro

; SET_SCREEN_COLOR value -- fills the screen/color area at SCREEN
; ($0400) with `value` (conventionally a foreground/background color
; pair packed into one byte: high nibble = foreground, low nibble =
; background). This is the step that's easiest to forget, and the bug
; it causes looks nothing like a graphics problem at first glance: in
; bitmap mode, this exact memory is what CHROUT would otherwise write
; character codes into for ordinary text -- so any *leftover* character
; codes still sitting there from text printed before switching to
; bitmap mode (or CLS's own screen-clearing fill) get reinterpreted as
; color data instead, showing up as scrambled colored pixels in
; whatever pattern that old text happened to form, with a solid stripe
; of one leftover color (usually from CLS's fill character) across the
; untouched rest of the screen. Calling this after CLEAR_BITMAP
; -- even if you don't care what color things are yet -- avoids that
; entirely. This fills slightly more than the visible 1000-byte screen
; (1024 bytes, matching bounce.asm's own proven approach), reaching
; into the sprite-pointer bytes at the very end of that range; call
; SPRITE_INIT (below) afterward, not before, so its own pointer write
; isn't overwritten by this.
.macro SET_SCREEN_COLOR value
        lda #<SCREEN
        sta gfx_ptr
        lda #>SCREEN
        sta gfx_ptr+1
        ldx #4
        lda #\value
@fill_loop:
        ldy #$00
@fill_byte:
        sta (gfx_ptr),y
        iny
        bne @fill_byte
        inc gfx_ptr+1
        dex
        bne @fill_loop
.endmacro

; BITMAP_MODE_OFF -- switches back to standard text mode, with the
; screen at SCREEN ($0400) and characters at the default character ROM
; shadow ($1000). Needed before printing any text (PRINT/CLS, text.inc)
; once bitmap mode has been used -- CHROUT writes character codes into
; screen memory, but in bitmap mode that memory holds color nibbles
; instead, so "printing" without switching back first produces a mess
; of colored blobs rather than legible text. This exact bug is what
; motivated this macro; see lander.asm's show_explosion-adjacent code
; for the original fix.
.macro BITMAP_MODE_OFF
        lda VIC_CTRL1
        and #%11011111         ; clear BMM
        sta VIC_CTRL1
        lda #$14                ; screen at $0400, characters at $1000
        sta VIC_MEMPTRS
.endmacro

; SPRITE_INIT data, color, x, y -- points sprite 0 at `data` (must be
; 64-byte aligned), sets its color and initial X/Y position, and
; enables it (and only it -- sprite 0 alone). For a program using more
; than one sprite, set SPRITE_ENABLE and the other sprites' own pointer
; bytes ($07f9 for sprite 1, and so on) directly afterward instead of
; calling this again, since this macro always enables sprite 0 only.
.macro SPRITE_INIT data, color, x, y
        lda #((\data) / 64)
        sta SPRITE_PTR0
        lda #\color
        sta SPRITE0_COLOR
        lda #\x
        sta SPRITE0_X
        lda #\y
        sta SPRITE0_Y
        lda #%00000001
        sta SPRITE_ENABLE
.endmacro

; wait_frame -- busy-waits for a raster line near the bottom of the
; visible display, then returns. A simple polling way to sync a main
; loop to the screen's refresh rate (roughly 50/60 Hz) so animation
; moves at a smooth, consistent speed regardless of how many cycles the
; rest of the loop happens to take -- call this once per frame, first
; thing in the loop, before moving anything. Proven identically across
; three of this project's own demos (bounce.asm, pong.asm, lander.asm)
; before being pulled out here.
wait_frame:
        lda VIC_RASTER
        cmp #$fb
        bne wait_frame
        rts

; sprite0_bounce_step -- advances sprite 0 by one pixel on each axis,
; reversing direction whenever it reaches a caller-defined bound, and
; writes the result to SPRITE0_X/SPRITE0_Y (and SPRITE_X_MSB, as
; needed -- see below). Call this once per frame (typically right
; after wait_frame) to animate a sprite bouncing around inside a
; rectangular area, the way bounce.asm's ball does.
;
; Needs six things defined by the including program, none of which
; this library claims on your behalf:
;   xpos   current X position, TWO bytes (xpos and xpos+1, low byte
;          and high byte of a 16-bit value) -- unlike a single-byte
;          position, this can represent the full 0-511 range sprite X
;          actually has, needed because the visible screen's right
;          edge (around X=344) is itself past 255. xpos+1 will only
;          ever hold 0 or 1 in practice, but is handled as a normal
;          16-bit high byte throughout.
;   ypos   current Y position, one byte -- sprite Y never exceeds 255,
;          so no second byte is needed here the way X needs one.
;   xdir, ydir   direction flags, one byte each: 1 means increasing
;          (right/down), 0 means decreasing (left/up).
;   XMIN, XMAX, YMIN, YMAX   compile-time constants (`=`), the
;          rectangular bounds to bounce within. XMAX/YMAX should leave
;          room for the sprite's own width/height so it visually
;          bounces off the edge rather than running partway past it --
;          see bounce.asm for a worked example sized for its
;          particular sprite. XMAX may be (and, for a sprite that
;          should reach the right portion of the screen, normally
;          needs to be) greater than 255.
;
; None of xpos/ypos/xdir/ydir need to be zero page -- they're read and
; written with plain (non-indirect) addressing here, so any RAM
; location works. Zero page is still a reasonable choice, purely
; because it makes the including program's own code a byte shorter
; wherever it touches them too, not because this routine requires it.
;
; Returns which axis (if either) bounced *this* frame in the CPU's own
; X and Y index registers -- 1 if that axis hit one of its bounds and
; reversed direction just now, 0 if it didn't (moved normally). Neither
; register is used for anything else in this routine, so this doesn't
; cost the caller anything it wasn't already free to use; a caller that
; doesn't care can simply ignore both, exactly as if this returned
; nothing at all. Typical use is triggering a sound effect only on the
; frames where a bounce actually happened, rather than a hit test of
; your own:
;
;       jsr sprite0_bounce_step
;       cpx #0
;       bne bounced
;       cpy #0
;       beq no_bounce
;   bounced:
;       PLAY_SOUND $18, $06, $00, %00010001   ; sound.inc
;   no_bounce:
;
; (Not folded into this routine directly, so graphics.inc has no
; dependency on sound.inc -- what to do on a bounce, if anything, is
; entirely up to the caller. See bounce.asm for this exact pattern.)
sprite0_bounce_step:
        ldx #0                ; returned: did X bounce this frame?
        ldy #0                ; returned: did Y bounce this frame?
        lda xdir
        beq @move_left
        inc xpos              ; 16-bit increment: bump the low byte, and
        bne @x_inc_done        ; only the high byte too if that wrapped
        inc xpos+1
@x_inc_done:
        lda xpos              ; 16-bit compare against XMAX
        cmp #<XMAX
        bne @x_done
        lda xpos+1
        cmp #>XMAX
        bne @x_done
        lda #0
        sta xdir             ; hit the right edge -- reverse
        ldx #1
        jmp @x_done
@move_left:
        lda xpos              ; 16-bit decrement: only borrow into the
        bne @x_dec_done         ; high byte if the low byte was already 0
        dec xpos+1
@x_dec_done:
        dec xpos
        lda xpos              ; 16-bit compare against XMIN (XMIN is
        cmp #<XMIN              ; ordinarily under 256, so xpos+1 will
        bne @x_done               ; ordinarily be checked against 0 here,
        lda xpos+1                  ; but this works for any XMIN value)
        cmp #>XMIN
        bne @x_done
        lda #1
        sta xdir             ; hit the left edge -- reverse
        ldx #1
@x_done:
        lda ydir
        beq @move_up
        inc ypos
        lda ypos
        cmp #YMAX
        bne @y_done
        lda #0
        sta ydir             ; hit the bottom edge -- reverse
        ldy #1
        jmp @y_done
@move_up:
        dec ypos
        lda ypos
        cmp #YMIN
        bne @y_done
        lda #1
        sta ydir             ; hit the top edge -- reverse
        ldy #1
@y_done:
        lda xpos
        sta SPRITE0_X
        lda xpos+1
        beq @msb_clear
        lda SPRITE_X_MSB
        ora #%00000001       ; set sprite 0's bit only -- other sprites'
        sta SPRITE_X_MSB       ; MSB bits, if any, are left untouched
        jmp @msb_done
@msb_clear:
        lda SPRITE_X_MSB
        and #%11111110
        sta SPRITE_X_MSB
@msb_done:
        lda ypos
        sta SPRITE0_Y
        rts

; sprite0_explode -- a quick "boom" effect: doubles sprite 0's size,
; flashes it through a caller-provided sequence of colors (two frames
; each, synced via wait_frame -- roughly a quarter second per color at
; a 50-60Hz refresh rate), then hides the sprite entirely. Sprite 0
; must already be positioned wherever the explosion should appear, and
; enabled, before calling this -- it doesn't touch position, only size,
; color, and visibility.
;
; Call with the color sequence's address in A (low byte) / Y (high
; byte), and how many colors it holds in X:
;
;       ldx #8
;       lda #<my_boom_colors
;       ldy #>my_boom_colors
;       jsr sprite0_explode
;   my_boom_colors: .byte 7,8,2,1,7,8,2,1   ; yellow,orange,red,white x2
;
; Reuses gfx_ptr for the color table (see CLEAR_BITMAP/SET_SCREEN_COLOR
; above for the same symbol) -- safe because an explosion never happens
; at the same moment as a bitmap-clear or screen-color fill, the same
; "never simultaneously active" reasoning this library uses for its
; other shared pointers.
sprite0_explode:
        sta gfx_ptr
        sty gfx_ptr+1
        lda #%00000001
        sta SPRITE_X_EXPAND
        sta SPRITE_Y_EXPAND
        ldy #$00
@loop:
        lda (gfx_ptr),y
        sta SPRITE0_COLOR
        jsr wait_frame
        jsr wait_frame
        iny
        dex
        bne @loop
        lda #0
        sta SPRITE_ENABLE
        sta SPRITE_X_EXPAND
        sta SPRITE_Y_EXPAND
        rts
