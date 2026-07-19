; text.inc - PETSCII text output and string comparison.
;
; Provides:
;   print_msg   subroutine: prints a null-terminated PETSCII string
;   PRINT msg   macro: convenience wrapper around print_msg
;   CLS         macro: clears the screen
;   NEWLINE     macro: moves the cursor to the start of the next line
;   SET_LOWERCASE_CHARSET  macro: switches the C64's *runtime* character
;               set to lowercase/uppercase, so text assembled with this
;               assembler's '.charset lower' directive actually displays
;               with real lowercase letters -- see the note below
;   SET_UPPERCASE_CHARSET  macro: switches back to the default
;               uppercase/graphics character set
;   DISABLE_CHARSET_SWITCH / ENABLE_CHARSET_SWITCH  macros: stop/allow
;               the user's own CBM+SHIFT keypress from changing the
;               character set out from under a running program
;   str_equal   subroutine: compares two null-terminated PETSCII strings
;
; A note on '.charset' and these macros -- two different things with
; similar names: this assembler's own '.charset upper'/'.charset lower'
; directive (see c64asm-reference.md, "Text and PETSCII") is an
; ASSEMBLY-TIME setting that only controls which PETSCII bytes .text/
; .asc/.byte string literals turn into. It has no effect on the C64's
; actual character set at runtime -- that's separate hardware state,
; and switching it is what SET_LOWERCASE_CHARSET/SET_UPPERCASE_CHARSET
; below actually do. Using '.charset lower' without ever calling
; SET_LOWERCASE_CHARSET assembles perfectly correct bytes that just sit
; there displaying as uppercase, same as before, because the character
; ROM showing them was never switched -- the two need to be used
; together, and the assembler has no way to enforce that automatically.
;
; Requires -- define ALL of these with `=` before this file's own
; .include line, even if your program only ever calls some of the
; routines above (see below for why):
;   str_ptr   2-byte zero page -- print_msg's working pointer to
;             whatever string is currently being printed
;   cmp_ptr   2-byte zero page -- str_equal's pointer to the string on
;             the "known" side of a comparison; set this yourself
;             before calling str_equal
;   kw_ptr    2-byte zero page -- str_equal's pointer to the string on
;             the other side; str_equal sets this itself from the A/Y
;             you pass it, so you never touch kw_ptr directly
;
; None of the three have to be permanently dedicated -- see below for
; when it's safe to have them share addresses with each other or with
; other libraries' own required pointers.
;
; This library doesn't claim any zero-page addresses on its own, since
; which addresses are safe to use depends on what else the program has
; already claimed -- see c64-memory-reference.md's zero-page notes for
; the community-documented safe range this project's own demos use,
; $02-$0F and $FB-$FE.
;
; All three are required as soon as you .include this file, EVEN IF
; your program never actually calls str_equal: .include splices in
; str_equal's code unconditionally, the same as everything else in the
; file, and that code references cmp_ptr/kw_ptr regardless of whether
; anything ever jsr's to it. A program that only wants print_msg/PRINT
; still needs to define all three, or assembly fails on an undefined
; symbol inside str_equal's own body.
;
; str_ptr and cmp_ptr/kw_ptr are never needed at the same time in a
; typical program (you're either printing or comparing, not both in the
; same instant), so it's safe to have cmp_ptr/kw_ptr share addresses
; with str_ptr if you're tight on zero page -- this project's own
; adventure.asm does exactly that; see its header comment for the full
; reasoning.
;
; Text must already be in PETSCII form to print correctly -- use this
; assembler's own .text/.byte directives (which PETSCII-encode quoted
; strings automatically) to define the messages this prints, not raw
; ASCII from some other source.

.include "hardware.inc"

; Prints the null-terminated PETSCII string at the address given in A
; (low byte) / Y (high byte). Uses @-prefixed local labels (see
; c64asm-reference.md, "Local labels"), so this subroutine's own loop
; labels can never collide with anything elsewhere in the including
; program, no matter how many other things are also called "loop" or
; "done".
print_msg:
        sta str_ptr
        sty str_ptr+1
        ldy #$00
@loop:
        lda (str_ptr),y
        beq @done
        jsr CHROUT
        iny
        bne @loop
@done:
        rts

; PRINT msg -- expands to the load-address-into-A/Y-then-call pattern
; print_msg needs. msg must be a label (typically one defined with
; .text/.byte ending in a $00 terminator byte).
.macro PRINT msg
        lda #<\msg
        ldy #>\msg
        jsr print_msg
.endmacro

; CLS -- clears the screen. $93 is the PETSCII "clear screen" control
; code; CHROUT interprets it as a screen-editor command rather than
; printing a visible character.
.macro CLS
        lda #$93
        jsr CHROUT
.endmacro

; NEWLINE -- moves the cursor to the start of the next screen line. $0d
; is PETSCII carriage return; CHROUT interprets it as a screen-editor
; command, the same way pressing RETURN at the keyboard does.
;
; Useful right after a read_line/CHRIN call (see input.inc): printing a
; response immediately after reading a typed line, with nothing between
; them, was found to leave the response running onto the same screen
; line as what was just typed -- see adventure.asm's own comment at the
; call site for the specific symptom this fixes and where it was found.
; Calling NEWLINE first guarantees a fresh line for the response,
; regardless of exactly where the cursor was left after the read.
.macro NEWLINE
        lda #$0d
        jsr CHROUT
.endmacro

; SET_LOWERCASE_CHARSET -- switches the C64's runtime character set to
; lowercase/uppercase, so PETSCII bytes produced by this assembler's
; '.charset lower' directive display with real lowercase letters
; instead of the default charset's graphics symbols. $0e is the
; PETSCII "switch to lowercase/uppercase" control code; CHROUT
; interprets it as a screen-editor command, exactly like CLS/NEWLINE
; above, not a printable character.
;
; Text assembled under '.charset upper' (the default) still displays
; correctly as uppercase after this -- '.charset upper' only stops
; being reliable once you ALSO print something assembled under
; '.charset lower' text in between; see this file's header note above.
.macro SET_LOWERCASE_CHARSET
        lda #$0e
        jsr CHROUT
.endmacro

; SET_UPPERCASE_CHARSET -- switches back to the default
; uppercase/graphics character set. $8e is the PETSCII "switch to
; uppercase/graphics" control code -- note this is $0e's own value
; with bit 7 set, not a coincidence: many PETSCII control-code pairs
; that toggle a setting on/off work this way.
.macro SET_UPPERCASE_CHARSET
        lda #$8e
        jsr CHROUT
.endmacro

; DISABLE_CHARSET_SWITCH -- stops the user's own CBM+SHIFT keypress
; from changing the character set. Useful right after
; SET_LOWERCASE_CHARSET in a program that wants to guarantee its
; chosen character set stays put for the rest of the run, rather than
; risk the player accidentally toggling it back with a keypress the
; program never asked for.
.macro DISABLE_CHARSET_SWITCH
        lda #$08
        jsr CHROUT
.endmacro

; ENABLE_CHARSET_SWITCH -- re-allows CBM+SHIFT to change the character
; set, undoing DISABLE_CHARSET_SWITCH above.
.macro ENABLE_CHARSET_SWITCH
        lda #$09
        jsr CHROUT
.endmacro

; Compares two null-terminated PETSCII strings for exact equality.
; Call with cmp_ptr already pointing at the first string, and the
; second string's address in A (low byte) / Y (high byte). Returns 1
; in A if the strings are equal, 0 if not. Useful for matching typed
; input (see input.inc's read_line/extract_word) against a fixed set
; of known keywords, e.g.:
;
;       lda #<verb_buf
;       sta cmp_ptr
;       lda #>verb_buf
;       sta cmp_ptr+1
;       lda #<kw_look
;       ldy #>kw_look
;       jsr str_equal
;       cmp #1
;       beq handle_look
str_equal:
        sta kw_ptr
        sty kw_ptr+1
        ldy #$00
@loop:
        lda (cmp_ptr),y
        cmp (kw_ptr),y
        bne @no
        cmp #$00
        beq @yes
        iny
        bne @loop
@no:
        lda #$00
        rts
@yes:
        lda #$01
        rts
