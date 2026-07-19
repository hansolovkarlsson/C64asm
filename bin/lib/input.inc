; input.inc - joystick, keyboard matrix, and typed-line input.
;
; Provides:
;   CIA_KEYBOARD_SETUP   macro: one-time CIA setup
;   read_joy2            subroutine: reads joystick port 2
;   READ_KEY column, mask   macro: checks one specific key
;   read_line            subroutine: reads a typed line via CHRIN
;   extract_word          subroutine: pulls one word out of that line
;   input_buf             the buffer read_line/extract_word share
;
; Requires -- define this with `=` before this file's own .include
; line, even if your program only ever calls read_joy2/READ_KEY (see
; below for why):
;   word_dest_ptr   2-byte zero page -- extract_word's pointer to
;                   wherever it should copy the word it's extracting;
;                   set this yourself (via A/Y) each time you call it
;
; This library doesn't claim a zero-page address of its own -- see
; text.inc's header comment for the same reasoning applied to its own
; required pointers, including when it's safe to have word_dest_ptr
; share an address with one of those.
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
;
; read_line/extract_word are unrelated to the joystick/keyboard-matrix
; code above -- they read typed input via the KERNAL's CHRIN, the same
; safe, well-tested line-editing mechanism BASIC's own INPUT statement
; uses (see this project's own adventure.asm, which is where these two
; were extracted from).
;
; word_dest_ptr is required as soon as you .include this file, EVEN IF
; your program never calls extract_word: .include splices in its code
; unconditionally like everything else here, and that code references
; word_dest_ptr regardless of whether anything ever jsr's to it. A
; program that only wants read_joy2/READ_KEY still needs to define it,
; or assembly fails on an undefined symbol inside extract_word's body.
;
; The check right below turns that into one clear message instead.

.ifndef word_dest_ptr
.error "input.inc requires word_dest_ptr (2-byte zero page) defined before this .include"
.endif

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

; --- typed-line input ---

MAX_INPUT_LEN = 40   ; includes the null terminator -- a full line can
                        ; hold up to MAX_INPUT_LEN-1 real characters
MAX_WORD_LEN  = 15   ; likewise includes the terminator; a word longer
                        ; than this is silently truncated by extract_word,
                        ; not treated as an error

; The buffer read_line fills and extract_word reads from. Owned by
; this library (rather than taking a buffer address as a parameter)
; because every real use of these two is "read a line, then pull words
; out of THAT line" -- there's no reason to plumb a buffer address
; through both calls when they're always used as a pair. If your
; program needs more than one independent line buffer at once, copy
; input_buf's contents elsewhere between calls rather than trying to
; parameterize these.
input_buf: .fill MAX_INPUT_LEN, 0

; Reads one line from the keyboard into input_buf, null-terminated.
; CHRIN, when reading from the keyboard, transparently invokes the
; KERNAL's full screen-editor line input (cursor, typing, backspace,
; echo) the first time it's called with an empty buffer, then dispenses
; the typed line one character at a time -- including the terminating
; RETURN -- on each subsequent call. This is the same mechanism BASIC's
; own INPUT statement uses internally.
read_line:
        ldx #$00
@loop:
        jsr CHRIN
        cmp #$0d
        beq @done
        sta input_buf,x
        inx
        cpx #MAX_INPUT_LEN-1
        bne @loop
@done:
        lda #$00
        sta input_buf,x
        rts

; Copies one space-delimited word out of input_buf, starting at index
; X, into the buffer whose address is passed in A (low byte) / Y (high
; byte). Advances X past the word and its trailing delimiter, so
; repeated calls (starting with X=0 the first time) pull successive
; words out of the same line -- see adventure.asm's own tokenize for
; an example composing this into "read exactly two words, called verb
; and noun". Skips any leading spaces first, so multiple consecutive
; spaces between words don't produce a spurious empty word. A word
; longer than MAX_WORD_LEN-1 characters is truncated, not treated as
; an error -- the rest of it is simply skipped over along with its
; trailing delimiter, same as any other word.
extract_word:
        sta word_dest_ptr
        sty word_dest_ptr+1
@skip_spaces:
        lda input_buf,x
        cmp #$20
        bne @start
        inx
        jmp @skip_spaces
@start:
        ldy #$00
@loop:
        lda input_buf,x
        beq @end
        cmp #$20
        beq @space
        sta (word_dest_ptr),y
        inx
        iny
        cpy #MAX_WORD_LEN-1
        bne @loop
        beq @terminate
@space:
        inx
@terminate:
@end:
        lda #$00
        sta (word_dest_ptr),y
        rts
