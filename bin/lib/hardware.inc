; hardware.inc - C64 hardware register constants: VIC-II, SID, CIA, and
; the KERNAL entry points this library set relies on.
;
; Requires: nothing. Unlike every other file in this library, this one
; defines constants only -- no code, no zero-page pointers, no flag
; bytes for the including program to reserve.
;
; This file defines constants only -- no code, no side effects -- so
; it's always safe to .include from anywhere, in any order, alongside
; any other file in this library set. Every other .inc file here
; .includes this one; since .include is automatically include-once
; (see c64asm-reference.md, "Includes"), a program that pulls in several of
; these library files together only pays for this file being processed
; a single time, with no "already defined" collisions.
;
; Only the registers actually used elsewhere in this library (or in
; this project's own demo programs) are listed -- this isn't a
; complete memory map. See c64-memory-reference.md for that.
;
; Constants that derive from another constant here (SPRITE_PTR0 from
; SCREEN) are deliberately ordered *after* the constant they depend on.
; This isn't just tidiness: a forward reference inside a plain "="
; constant assignment isn't flagged as an error by this assembler (only
; .if/.elif treat that as a hard error -- see c64asm-reference.md
; "Conditional assembly"), so it would silently compute using a
; placeholder value on whichever pass reaches it first. That value
; happens to still work out for SPRITE_PTR0 specifically, but it's not
; a coincidence worth relying on -- defining things in dependency order
; avoids the question entirely.

; --- KERNAL ---
CHROUT = $ffd2          ; output one PETSCII character (in A) to the
                          ; current output channel (the screen, by default)
CHRIN  = $ffcf           ; read one PETSCII character from the current
                          ; input channel, with the full screen-editor
                          ; line-input experience (cursor keys, DEL, etc.)

SCREEN = $0400           ; default screen (character/color-code) memory
BITMAP = $2000            ; a common, VIC-bank-aligned bitmap address this
                            ; library's graphics macros default to; pass a
                            ; different address explicitly if you need one

; --- VIC-II (video) ---
VIC_CTRL1   = $d011     ; control register 1: bit 5 = BMM (bitmap mode),
                          ; bits 0-2 = vertical fine scroll (leave as-is)
VIC_RASTER  = $d012     ; current raster line (bits 0-7; bit 8 lives in
                          ; VIC_CTRL1, not broken out here since none of
                          ; this project's own code needs lines 256-311)
VIC_CTRL2   = $d016     ; control register 2: bit 4 = MCM (multicolor mode)
VIC_MEMPTRS = $d018     ; screen/character (or bitmap) pointer register
VIC_BORDER  = $d020     ; border color
VIC_BG0     = $d021     ; background color 0
SPRITE0_X   = $d000     ; sprite 0 X position (low 8 bits)
SPRITE0_Y   = $d001     ; sprite 0 Y position
SPRITE_X_MSB = $d010    ; bit N = bit 8 of sprite N's X position, letting
                           ; a sprite's X reach the full 0-511 range instead
                           ; of just 0-255 -- needed for a sprite to reach
                           ; the right portion of the visible screen, which
                           ; starts around X=344
SPRITE_ENABLE = $d015   ; bit N = 1 enables sprite N
SPRITE_Y_EXPAND = $d017 ; bit N = 1 doubles sprite N's height
SPRITE0_COLOR = $d027   ; sprite 0's color
SPRITE_X_EXPAND = $d01d ; bit N = 1 doubles sprite N's width
SPRITE_PTR0 = SCREEN + $3f8   ; sprite 0's data pointer, in the sprite
                                ; pointer block at the end of screen memory
                                ; -- holds (data address / 64)

; --- SID (sound) ---
VOICE1_FREQ_LO = $d400
VOICE1_FREQ_HI = $d401
VOICE1_CTRL    = $d404   ; bits 4-7 = waveform select, bit 0 = gate
VOICE1_AD      = $d405   ; bits 4-7 = attack, bits 0-3 = decay
VOICE1_SR      = $d406   ; bits 4-7 = sustain, bits 0-3 = release
SID_VOLUME     = $d418   ; bits 0-3 = master volume (0-15)

; --- CIA #1 (joystick port 2 and keyboard matrix) ---
CIA1_PRA = $dc00        ; port A: joystick port 2 input, OR keyboard
                          ; column-select output -- see input.inc's header
                          ; comment for why those being the same register
                          ; matters
CIA1_PRB = $dc01        ; port B: keyboard row input
CIA1_DDRA = $dc02       ; port A data-direction register
CIA1_DDRB = $dc03       ; port B data-direction register
