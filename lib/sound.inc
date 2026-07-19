; sound.inc - SID sound effects, voice 1.
;
; Provides:
;   SID_INIT                              macro
;   PLAY_SOUND freq_hi, ad, sr, waveform   macro: one-shot sound effect
;   engine_sound_on / engine_sound_off     subroutines: continuous tone
;
; Requires -- define this with `=` before this file's own .include
; line, even if your program only ever calls SID_INIT/PLAY_SOUND (see
; below for why):
;   engine_playing   1 byte, anywhere in RAM (doesn't need to be zero
;                    page) -- engine_sound_on/off's flag for whether
;                    the continuous tone is currently gated on; you
;                    never set this yourself, just reserve the byte
;
; PLAY_SOUND is for short, fire-and-forget effects (a hit, a bounce, an
; explosion) -- each call fully retriggers the envelope, so calling it
; again while a previous sound is still sounding cuts that one off and
; starts the new one. pong.asm and lander.asm both use this for their
; own one-shot effects (wall bounce/paddle hit/score, and a crash boom,
; respectively).
;
; engine_sound_on/off are for a continuous tone that starts and stops
; based on held input (a thruster, an engine) -- unlike PLAY_SOUND,
; engine_sound_on checks whether the sound is already playing before
; touching the SID at all, so calling it every frame while a key is
; held doesn't repeatedly retrigger the envelope's attack phase (which
; would make it stutter instead of sounding like one continuous note).
; This library doesn't claim engine_playing's address on its own -- see
; text.inc's header comment for the same reasoning applied to its own
; required pointers.
;
; engine_playing is required as soon as you .include this file, EVEN IF
; your program never calls engine_sound_on/off: .include splices in
; that code unconditionally like everything else here, and it
; references engine_playing regardless of whether anything ever jsr's
; to it. A program that only wants SID_INIT/PLAY_SOUND still needs to
; define it, or assembly fails on an undefined symbol inside
; engine_sound_on/off's own bodies.
;
; The check right below turns that into one clear message instead.

.ifndef engine_playing
.error "sound.inc requires engine_playing (1 byte, anywhere in RAM) defined before this .include"
.endif

.include "hardware.inc"

; Sets the master volume to maximum and makes sure voice 1 starts
; silent. Call this once, early in your program.
.macro SID_INIT
        lda #$0f
        sta SID_VOLUME
        lda #$00
        sta VOICE1_CTRL
.endmacro

; PLAY_SOUND freq_hi, ad, sr, waveform -- plays a one-shot sound on
; voice 1. freq_hi sets the frequency's high byte (low byte is always
; 0 here, which is precise enough for sound effects that don't need an
; exact musical pitch); ad packs attack (high nibble) and decay (low
; nibble); sr packs sustain (high nibble) and release (low nibble);
; waveform is the full $d404 control-register byte, gate bit included
; (e.g. %10000001 for noise+gate-on, %00010001 for triangle+gate-on).
;
; Example -- a short, low-pitched noise "hit" sound:
;       PLAY_SOUND $30, $08, $00, %10000001
.macro PLAY_SOUND freq_hi, ad, sr, waveform
        lda #$00
        sta VOICE1_CTRL
        lda #$00
        sta VOICE1_FREQ_LO
        lda #\freq_hi
        sta VOICE1_FREQ_HI
        lda #\ad
        sta VOICE1_AD
        lda #\sr
        sta VOICE1_SR
        lda #\waveform
        sta VOICE1_CTRL
.endmacro

; Starts a continuous low noise tone on voice 1, if not already
; playing. Suitable for an engine/thruster sound held for as long as a
; key is down -- call this every frame the key is held.
engine_sound_on:
        lda engine_playing
        bne @already_on
        lda #$00
        sta VOICE1_FREQ_LO
        lda #$08
        sta VOICE1_FREQ_HI
        lda #$00
        sta VOICE1_AD
        lda #$f8
        sta VOICE1_SR
        lda #%10000001          ; noise waveform, gate on
        sta VOICE1_CTRL
        lda #1
        sta engine_playing
@already_on:
        rts

; Stops the continuous tone started by engine_sound_on, if playing.
; Call this every frame the key is NOT held.
engine_sound_off:
        lda engine_playing
        beq @already_off
        lda #%10000000          ; noise waveform, gate off
        sta VOICE1_CTRL
        lda #0
        sta engine_playing
@already_off:
        rts
