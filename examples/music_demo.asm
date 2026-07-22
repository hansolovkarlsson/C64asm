; music_demo.asm - a two-voice SID music demo, playing the traditional
; melody "Twinkle Twinkle Little Star" (public domain; the tune is the
; 18th-century French folk song "Ah! vous dirai-je, maman") on voice 1,
; with a simple root-note bass line on voice 2 underneath.
;
; Demonstrates lib/music.inc (this project's own two-voice sequencer --
; see that file's own header comment for how it works) with real note
; data: two SoA byte tables (frequency low/high byte, duration in
; frames) per voice, walked forward by music_tick once per frame the
; same way this project's other demos drive their own per-frame work
; via wait_frame. The border color pulses once each time the melody
; starts a new note, so there's something to see as well as hear.
;
; Uses lib/text.inc and lib/keyboard.inc rather than reimplementing
; text output or key detection -- Q exits, silencing both voices
; cleanly (music_stop) rather than leaving a note ringing into its
; release phase after control has already moved on.

CHROUT  = $FFD2

; --- lib/text.inc's own required zero page ---
str_ptr = $fb
cmp_ptr = $fd
kw_ptr  = $02

; --- lib/input.inc's own required zero page (only for
; CIA_KEYBOARD_SETUP/READ_KEY here -- extract_word is never called) ---
word_dest_ptr = $fb

; --- lib/keyboard.inc's own required zero page ---
key_scratch = $0a

; --- lib/music.inc's own required state -- see that file's header
; comment for what each of these is. None of them need to be zero
; page (nothing here uses indirect addressing on them), so they live
; in the cassette-buffer area instead, keeping zero page free for the
; pointers above that actually do need it. ---
melody_index    = $033c
melody_timer    = $033d
bass_index      = $033e
bass_timer      = $033f
melody_waveform = $0340
bass_waveform   = $0341

MELODY_LEN = 48
BASS_LEN   = 6

        .basic start

        .include "lib/text.inc"
        .include "lib/input.inc"
        .include "lib/keyboard.inc"
        .include "lib/music.inc"

; wait_frame is graphics.inc's own (raster-sync for wait_frame is all
; this demo needs from that file) -- written fresh here instead of
; .include-ing graphics.inc just for this one four-line routine, which
; would otherwise mean satisfying nine unrelated zero-page/RAM
; requirements this demo has no other use for (gfx_ptr, sprite bounce
; state, and so on).
wait_frame:
        lda VIC_RASTER
        cmp #$fb
        bne wait_frame
        rts

start:
        CIA_KEYBOARD_SETUP
        CLS
        PRINT title_msg
        PRINT credit_msg
        PRINT instructions_msg
        PRINT continue_msg
        jsr wait_any_key

        CLS
        PRINT now_playing_msg

        ; Voice 1 (melody): sawtooth, quick attack, moderate decay,
        ; fairly loud sustain, short release -- a bright, snappy lead
        ; tone with each note clearly separated from the next.
        ; Voice 2 (bass): triangle, a touch slower attack for
        ; smoothness, higher sustain since each note is held much
        ; longer (a whole phrase, not a single beat).
        MUSIC_INIT %00100001, $08, $a3, %00010001, $28, $c4

main_loop:
        jsr wait_frame
        jsr music_tick

        ; pulse the border once per new melody note: right after
        ; music_tick returns, melody_timer holds a fresh note's full
        ; duration only on the exact frame a new note just started
        lda melody_timer
        cmp #15
        bne check_exit
        inc $d020

check_exit:
        READ_KEY KEY_Q_COL, KEY_Q_ROW
        bne do_exit
        jmp main_loop

do_exit:
        jsr music_stop
        CLS
        PRINT bye_msg
        rts

title_msg:
        .text "TWINKLE TWINKLE LITTLE STAR", 13, 0
credit_msg:
        .text "A TRADITIONAL FOLK MELODY (PUBLIC DOMAIN)", 13, 13, 0
instructions_msg:
        .text "TWO-VOICE SID MUSIC DEMO. PRESS Q TO EXIT.", 13, 0
continue_msg:
        .text "PRESS ANY KEY TO START...", 13, 0
now_playing_msg:
        .text "NOW PLAYING...", 13, 0
bye_msg:
        .text "GOODBYE", 13, 0

; Twinkle Twinkle Little Star, full first verse -- six 8-beat phrases
; (7 notes + a closing rest each), 48 entries total. A $0000 frequency
; ($00 in both the lo and hi table at the same index) means a rest.
;                       C4    C4    G4    G4    A4    A4    G4   rest
melody_freq_lo: .byte $67,  $67,  $13,  $13,  $45,  $45,  $13,  $00
;                       F4    F4    E4    E4    D4    D4    C4   rest
                .byte $3B,  $3B,  $ED,  $ED,  $89,  $89,  $67,  $00
;                       G4    G4    F4    F4    E4    E4    D4   rest
                .byte $13,  $13,  $3B,  $3B,  $ED,  $ED,  $89,  $00
;                      (repeat of the G4 F4 E4 D4 phrase above)
                .byte $13,  $13,  $3B,  $3B,  $ED,  $ED,  $89,  $00
;                      (repeat of the opening C4 G4 A4 G4 phrase)
                .byte $67,  $67,  $13,  $13,  $45,  $45,  $13,  $00
;                      (repeat of the F4 E4 D4 C4 phrase)
                .byte $3B,  $3B,  $ED,  $ED,  $89,  $89,  $67,  $00

melody_freq_hi: .byte $11,  $11,  $1A,  $1A,  $1D,  $1D,  $1A,  $00
                .byte $17,  $17,  $15,  $15,  $13,  $13,  $11,  $00
                .byte $1A,  $1A,  $17,  $17,  $15,  $15,  $13,  $00
                .byte $1A,  $1A,  $17,  $17,  $15,  $15,  $13,  $00
                .byte $11,  $11,  $1A,  $1A,  $1D,  $1D,  $1A,  $00
                .byte $17,  $17,  $15,  $15,  $13,  $13,  $11,  $00

; 15 frames (0.3s at 50Hz PAL) per entry, rests included -- a lively,
; even quarter-note tempo throughout.
melody_duration:
        .repeat 6
                .byte 15, 15, 15, 15, 15, 15, 15, 15
        .endrepeat

; One held note per phrase (root of the phrase's implied chord: C major
; under phrase 1, F major under phrase 2, G major under phrase 3),
; giving a simple I-IV-V-V-I-IV pattern underneath the melody.
;                        C3    F3    G3    G3    C3    F3
bass_freq_lo:   .byte  $B4,  $9D,  $0A,  $0A,  $B4,  $9D
bass_freq_hi:   .byte  $08,  $0B,  $0D,  $0D,  $08,  $0B
; 120 frames (8 melody-note-lengths -- a whole phrase, rest included)
; per bass note, so each one starts and ends exactly in step with the
; melody phrase it sits under.
bass_duration:
        .repeat 6
                .byte 120
        .endrepeat
