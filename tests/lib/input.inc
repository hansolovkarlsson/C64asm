; input.inc - joystick and keyboard input.
;
; Provides:
;   CIA_KEYBOARD_SETUP   macro: one-time CIA setup
;   read_joy2            subroutine: reads joystick port 2
;   READ_KEY column, mask   macro: checks one specific key
;
; IMPORTANT: joystick port 2 and the keyboard column-select register
; are the exact same hardware address (CIA1_PRA, $DC00), and this is
; more than a naming coincidence to be careful about -- it constrains
; how the port's data-DIRECTION register (DDRA) has to be used.
; Reading a joystick switch needs that pin configured as an INPUT;
; writing a column-select byte needs it configured as an OUTPUT. A
; real 6526 CIA, when a pin is configured as output, reads back
; whatever was last WRITTEN to it -- not the actual external switch
; state -- so if DDRA is left permanently in "output" mode (needed for
; keyboard scanning), reading $DC00 afterward for "joystick input"
; doesn't read the joystick at all: every bit reads back the last
; column-select byte written, misinterpreted as joystick directions
; and fire. This can manifest as a phantom fire-button press (a stray
; sound effect firing with nothing held) or phantom directional input,
; depending entirely on which column was last selected -- which is
; exactly the kind of intermittent, seems-almost-random bug this
; produced during this project's own testing.
;
; The fix below: read_joy2 and READ_KEY each set DDRA to whatever mode
; *they* need immediately before using the port, rather than relying
; on a one-time setup to have left it in the right state. This also
; means, unlike an earlier version of this library, the two can now be
; called in either order within the same frame without corrupting each
; other -- there's no longer an ordering requirement to remember.
;
; READ_KEY additionally wraps its scan in SEI/CLI: the KERNAL's own
; IRQ handler keeps scanning the keyboard in the background even after
; a program has SYS'd out of BASIC, and without disabling interrupts
; around the $DC00 write + $DC01 read, that background scan can
; corrupt the read.

.include "hardware.inc"

; Call once: sets port B to all inputs (needed for reading keyboard row
; data, and never changed by anything else in this library). Port A's
; direction is managed per-call by read_joy2/READ_KEY themselves (see
; above), not set once here.
.macro CIA_KEYBOARD_SETUP
        lda #%00000000
        sta CIA1_DDRB
.endmacro

; Reads joystick port 2 into A, active-HIGH (1 = pressed) -- the raw
; hardware register is active-LOW (0 = pressed), which this inverts so
; callers don't have to remember that. Bit layout, matching the
; hardware: 0=up 1=down 2=left 3=right 4=fire.
;
; Sets DDRA to all-input first (see the header comment above for why
; this is required, not optional, if READ_KEY might have run since the
; last call to this).
read_joy2:
        lda #%00000000
        sta CIA1_DDRA
        lda CIA1_PRA
        eor #$ff
        rts

; READ_KEY column, mask -- checks whether a specific keyboard key is
; currently held. `column` is the column-select byte to write to
; CIA1_PRA (see the C64 keyboard matrix table in c64-memory-reference.md
; for which byte selects which column); `mask` is the bit within the
; resulting CIA1_PRB read that corresponds to the specific key. After
; this macro, the Z flag reflects whether the key is held, the
; intuitive way round: BNE means the key IS pressed, BEQ means it is
; NOT -- matching read_joy2's own active-HIGH convention above, and
; the opposite of the CIA hardware's own native polarity (a held key
; pulls its matrix bit LOW, not high), which this macro inverts
; internally specifically so callers don't have to remember that.
;
; Sets DDRA to all-output first (see the header comment above for why
; this is required, not optional, if read_joy2 might have run since
; the last call to this).
;
; Example (W key, matrix column 1, bit 1 -- see pong.asm/lander.asm for
; the full matrix table this project has already verified on real
; hardware):
;       READ_KEY %11111101, %00000010
;       bne key_w_is_pressed
.macro READ_KEY column, mask
        sei
        lda #%11111111
        sta CIA1_DDRA
        lda #\column
        sta CIA1_PRA
        lda CIA1_PRB
        cli
        eor #$ff          ; invert to active-HIGH before testing the bit
        and #\mask
.endmacro
