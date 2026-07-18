; graphics.inc - standard bitmap mode and sprite setup.
;
; Provides:
;   BITMAP_MODE_ON bitmap_addr    macro
;   BITMAP_MODE_OFF                macro
;   CLEAR_BITMAP bitmap_addr       macro
;   SET_SCREEN_COLOR value         macro
;   SPRITE_INIT data, color, x, y  macro
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
