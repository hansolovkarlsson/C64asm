; music.inc - a small two-voice SID music player.
;
; Provides:
;   MUSIC_INIT melody_wave, melody_ad, melody_sr, bass_wave, bass_ad, bass_sr
;               macro: sets SID volume, silences both voices, latches
;               each voice's waveform/attack-decay/sustain-release for
;               the rest of the piece, and primes the sequencer so the
;               very first music_tick call starts note 0 of each voice
;   music_tick  subroutine: advances playback by one frame -- call this
;               once per frame (e.g. right after wait_frame), the same
;               way this project's other per-frame work is driven
;
; Requires -- define ALL of these before this file's own .include line
; (see graphics.inc's own header comment for why every one of them is
; needed regardless of which parts of this file your program actually
; uses -- the same reasoning applies here):
;
;   melody_index, melody_timer, bass_index, bass_timer
;               1 byte each, anywhere in RAM -- the sequencer's own
;               position and per-note frame countdown for each voice
;   melody_waveform, bass_waveform
;               1 byte each, anywhere in RAM -- MUSIC_INIT stores each
;               voice's waveform bits here once, so every later note
;               retrigger can read it back without needing it passed
;               in again
;   MELODY_LEN, BASS_LEN
;               compile-time constants (`=`) -- the number of entries
;               in your own melody_freq_lo/melody_freq_hi/
;               melody_duration and bass_freq_lo/bass_freq_hi/
;               bass_duration tables (see below), so the sequencer
;               knows where each voice's own part loops back to note 0
;
; This file does NOT provide the note tables themselves --
; melody_freq_lo, melody_freq_hi, melody_duration, bass_freq_lo,
; bass_freq_hi, bass_duration -- those are the actual song, which is
; the caller's own data, the same way SID_INIT/PLAY_SOUND (sound.inc)
; don't provide the sound effects themselves. Each is a plain byte
; array, MELODY_LEN (or BASS_LEN) entries long, indexed in lockstep:
; entry N's frequency is (melody_freq_lo[N], melody_freq_hi[N]), held
; for melody_duration[N] frames. A frequency of $0000 (both freq_lo and
; freq_hi zero -- not a real SID frequency any actual note would ever
; need) means a rest: that voice's gate turns off and stays off for
; that entry's own duration, instead of playing a note.
;
; Two voices only (SID's own third voice is untouched, in case your
; program wants it for something else -- a sound effect via sound.inc,
; say). Melody plays on voice 1, bass on voice 2.
;
; Every note retrigger writes the control register with gate off, then
; the new frequency, then gate on with the latched waveform -- the
; standard technique for cleanly restarting the envelope's attack
; phase on real SID hardware, rather than letting consecutive notes at
; the same pitch bleed into each other.

.ifndef melody_index
.error "music.inc requires melody_index (1 byte, anywhere in RAM) defined before this .include"
.endif
.ifndef melody_timer
.error "music.inc requires melody_timer (1 byte, anywhere in RAM) defined before this .include"
.endif
.ifndef bass_index
.error "music.inc requires bass_index (1 byte, anywhere in RAM) defined before this .include"
.endif
.ifndef bass_timer
.error "music.inc requires bass_timer (1 byte, anywhere in RAM) defined before this .include"
.endif
.ifndef melody_waveform
.error "music.inc requires melody_waveform (1 byte, anywhere in RAM) defined before this .include"
.endif
.ifndef bass_waveform
.error "music.inc requires bass_waveform (1 byte, anywhere in RAM) defined before this .include"
.endif
.ifndef MELODY_LEN
.error "music.inc requires MELODY_LEN (a compile-time constant) defined before this .include"
.endif
.ifndef BASS_LEN
.error "music.inc requires BASS_LEN (a compile-time constant) defined before this .include"
.endif

.include "hardware.inc"

.macro MUSIC_INIT melody_wave, melody_ad, melody_sr, bass_wave, bass_ad, bass_sr
        lda #$0f
        sta SID_VOLUME
        lda #$00
        sta VOICE1_CTRL
        sta VOICE2_CTRL
        lda #\melody_wave
        sta melody_waveform
        lda #\melody_ad
        sta VOICE1_AD
        lda #\melody_sr
        sta VOICE1_SR
        lda #\bass_wave
        sta bass_waveform
        lda #\bass_ad
        sta VOICE2_AD
        lda #\bass_sr
        sta VOICE2_SR
        lda #$ff
        sta melody_index
        sta bass_index
        lda #$01
        sta melody_timer
        sta bass_timer
.endmacro

; Call once per frame. Advances each voice's own countdown timer, and
; moves to that voice's next note whenever its timer reaches zero.
music_tick:
        dec melody_timer
        bne check_bass_tick
        jsr advance_melody_note
check_bass_tick:
        dec bass_timer
        bne music_tick_done
        jsr advance_bass_note
music_tick_done:
        rts

; Silences both voices immediately (gate off on each) without
; disturbing the sequencer's own position -- calling music_tick again
; later resumes exactly where playback left off. Useful for a clean
; stop when the player exits, rather than leaving a note ringing into
; its release phase after the program's own control flow has moved on.
music_stop:
        lda #$00
        sta VOICE1_CTRL
        sta VOICE2_CTRL
        rts

advance_melody_note:
        inc melody_index
        lda melody_index
        cmp #MELODY_LEN
        bne @in_range
        lda #$00
        sta melody_index
@in_range:
        ldx melody_index
        lda melody_duration,x
        sta melody_timer
        lda melody_freq_hi,x
        ora melody_freq_lo,x
        beq @rest
        lda #$00
        sta VOICE1_CTRL
        lda melody_freq_lo,x
        sta VOICE1_FREQ_LO
        lda melody_freq_hi,x
        sta VOICE1_FREQ_HI
        lda melody_waveform
        sta VOICE1_CTRL
        rts
@rest:
        lda #$00
        sta VOICE1_CTRL
        rts

advance_bass_note:
        inc bass_index
        lda bass_index
        cmp #BASS_LEN
        bne @in_range
        lda #$00
        sta bass_index
@in_range:
        ldx bass_index
        lda bass_duration,x
        sta bass_timer
        lda bass_freq_hi,x
        ora bass_freq_lo,x
        beq @rest
        lda #$00
        sta VOICE2_CTRL
        lda bass_freq_lo,x
        sta VOICE2_FREQ_LO
        lda bass_freq_hi,x
        sta VOICE2_FREQ_HI
        lda bass_waveform
        sta VOICE2_CTRL
        rts
@rest:
        lda #$00
        sta VOICE2_CTRL
        rts
