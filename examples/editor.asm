; editor.asm - a simple text editor with load, save, and directory
; listing.
;
; The editable area is rows 0-23 (was all 25 rows in this file's first
; version) -- row 24 is now a status/prompt line, showing either brief
; help text, a "SAVE AS:"/"LOAD:" filename prompt, or a one-line result
; message ("SAVED.", "FILE NOT FOUND", and so on) after an operation.
; This is a deliberate change from the first version, not an
; afterthought: load/save/directory are inherently UI-having
; operations, and there's nowhere else on a one-screen editor to put
; that UI.
;
; Controls:
;   any typable key       insert it at the cursor, advance right
;   RETURN                 move to the start of the next line
;   DEL                    erase the character behind the cursor
;   cursor up/down/left/right   move without changing anything
;   F1                     quit
;   F2                      new file -- clears the document, no
;                              confirmation prompt (matches F5/load,
;                              which already overwrites without asking)
;   F3                      save (prompts for a filename)
;   F4                      delete a file (prompts for a filename,
;                              then asks Y/N to confirm -- unlike F2/F5,
;                              since there's no "just reload it" way
;                              back from deleting a file on disk)
;   F5                      load (prompts for a filename)
;   F7                      show the disk directory
;
; Any of F3/F4/F5's own filename prompts (and F4's Y/N confirmation)
; can be backed out of without side effects: pressing RETURN with
; nothing typed cancels, and so does RUN/STOP -- the C64 keyboard has
; no key labeled ESC, and RUN/STOP (also reachable as Ctrl+C) is the
; conventional C64 equivalent for "abort this."
;
; Saving/loading uses SEQ (sequential) files -- the natural fit for
; plain text, unlike PRG, which implies a 2-byte load-address header
; that has nothing to do with a text document. A filename typed as
; "NOTES" becomes "NOTES,S,W" (write) or "NOTES,S,R" (read) before
; being passed to SETNAM, following ordinary CBM DOS filename-suffix
; convention.
;
; Saving over an existing file works by deleting it first (SCRATCH,
; the same mechanism F4 uses directly) and then writing fresh, rather
; than the "@0:name,S,W" save-and-replace shortcut CBM DOS also
; offers. That shortcut has a well-documented data-corruption bug on
; original 1541 firmware (fixed only in later revisions -- the 1541-II
; and 1571), and it's not something a program can detect or work
; around at the KERNAL call level; scratch-then-write uses only two
; plain, well-understood operations instead, at the cost of a file
; briefly not existing between the two (an interruption there loses
; the old copy without the new one landing -- an inherent tradeoff of
; avoiding the buggy shortcut, not a bug in this approach itself, and
; the one every serious CBM DOS reference recommends over "@" anyway).
;
; Screen memory is still the one and only copy of the document (see
; this file's own original header note on why) -- save converts each
; screen code back to PETSCII while writing; load does the reverse
; while reading, filling any row shorter than 40 columns with spaces.
;
; The KERNAL calls used here (SETLFS, SETNAM, OPEN, CHKIN, CHKOUT,
; CLRCHN, CLOSE, READST, and CHRIN/CHROUT's file-redirected behavior)
; follow the standard, documented calling conventions every C64
; program's disk I/O uses -- this project has no way to test against a
; real IEC bus or drive, so mini6502.py's own virtual disk (see that
; file's own header comment) verifies this code's KERNAL call sequence
; and byte-for-byte file contents match those documented conventions.
; That is a real, useful check, but it is not the same as testing
; against a real 1541 or VICE, which is still worth doing before
; trusting this with anything you'd mind losing.
;
; The directory listing (F7) parses the same byte format a real 1541
; sends for LOAD"$",8 -- see https://www.pagetable.com/?p=273 for the
; exact structure this follows: a 2-byte fake load address, then one
; BASIC-program-style "line" per entry (a link pointer, a line number
; doubling as the block count, null-terminated text), ending in a
; 2-byte $0000 link pointer. Printed via ordinary CHROUT rather than
; direct screen writes, so line wrapping and scrolling are the real
; screen editor's own problem, not this program's -- the current
; screen contents are saved to a scratch buffer at $C000 first and
; restored afterward, the same trick this editor already leans on
; elsewhere: don't build a second bespoke mechanism when the existing
; one already does the job.

CHROUT  = $FFD2
GETIN   = $FFE4
SETLFS  = $FFBA
SETNAM  = $FFBD
OPEN    = $FFC0
CLOSE   = $FFC3
CHKIN   = $FFC6
CHKOUT  = $FFC9
CLRCHN  = $FFCC
CHRIN   = $FFCF
READST  = $FFB7

; --- lib/text.inc's own required zero page ---
str_ptr = $fb
cmp_ptr = $fd
kw_ptr  = $02

; screen_ptr always points at the cursor's own current screen memory
; cell -- recomputed by recompute_screen_ptr every time cursor_x/
; cursor_y change, from screen_row_lo/hi plus cursor_x.
screen_ptr = $f7

; msg_ptr: a second, separate zero-page pointer for print_status --
; deliberately not shared with screen_ptr, since a status message can
; be shown mid-operation (a "SAVING..." message while screen_ptr is
; busy pointing at the cursor's own cell) without the two stepping on
; each other.
msg_ptr = $f9

; copy_ptr: a third zero-page pointer, used only while copying the
; whole screen to or from a file (write_screen_to_file/
; read_file_to_screen) or to/from the $C000 backup buffer
; (save_screen_backup/restore_screen_backup) -- again kept separate
; from screen_ptr/msg_ptr so none of the three ever collide.
copy_ptr = $f5

; cursor_x (0-39), cursor_y (0-23) -- plain RAM, not zero page, since
; nothing here needs indirect addressing on them specifically.
cursor_x = $033c
cursor_y = $033d

; filename_buf holds a typed filename as PETSCII (not screen codes --
; this is what SETNAM itself needs), up to 16 characters plus up to 4
; more for a ",S,W"/",S,R" suffix appended before SETNAM is called.
; filename_len is the typed portion's own length, before the suffix;
; filename_total_len includes the suffix, and is what actually gets
; passed to SETNAM.
filename_buf = $0342     ; 24 bytes: $0342-$0359
filename_len = $035a
filename_total_len = $035b

; Scratch state for the directory listing parser and print_decimal16 --
; see those routines below.
dir_link_lo = $035c
dir_link_hi = $035d
dir_num_lo  = $035e
dir_num_hi  = $035f
dec_lo      = $0360
dec_hi      = $0361
dec_digit   = $0362
dec_started = $0363

; print_status's own scratch: how many characters of the status
; message it actually printed before padding the rest of the row with
; spaces -- prompt_filename uses this to know which screen column to
; start echoing typed characters at.
status_msg_len = $0364

; prompt_filename's own scratch: a typed character's screen-code form,
; held here briefly between converting it and writing it to the
; screen, so the conversion doesn't need to fight over A with the
; column-position arithmetic happening at the same time.
prompt_scratch = $0365

; scratch_cmd_buf holds the SCRATCH (delete) command actually sent to
; the drive -- "S0:" followed by the target filename, built from
; filename_buf by build_scratch_cmd. 20 bytes covers the 3-byte "S0:"
; prefix plus the 16-character filename limit already enforced
; elsewhere, with a byte to spare. scratch_cmd_len is its own length,
; what's actually passed to SETNAM.
scratch_cmd_buf = $0366    ; 20 bytes: $0366-$0379
scratch_cmd_len = $037a

; A 1000-byte scratch copy of screen memory, used only while the
; directory listing (F7) is on screen -- see this file's own header
; comment for why. $C000-$C3E7 is ordinary free RAM between BASIC ROM
; ($A000-$BFFF) and the VIC/SID/CIA I/O area ($D000+), unused by this
; program otherwise since it's plain machine code, not a BASIC program.
SCREEN_BACKUP = $C000

MAX_ROW = 23     ; last editable row -- row 24 is the status line
STATUS_ROW_ADDR = $07C0   ; $0400 + 24*40

        .basic start

        .include "lib/text.inc"

start:
        CLS
        PRINT title_msg
        PRINT instructions_msg
        PRINT continue_msg
        jsr wait_for_key

        CLS
        lda #$00
        sta cursor_x
        sta cursor_y
        jsr recompute_screen_ptr
        jsr show_help_status

main_loop:
        jsr show_cursor
wait_key:
        jsr GETIN
        beq wait_key
        pha
        jsr hide_cursor
        pla

        cmp #$85                ; F1
        beq @do_quit
        cmp #$89                ; F2
        beq @do_new
        cmp #$86                ; F3
        beq @do_save
        cmp #$8a                ; F4
        beq @do_delete_file
        cmp #$87                ; F5
        beq @do_load
        cmp #$88                ; F7
        beq @do_directory
        cmp #$0d                ; RETURN
        beq @do_return
        cmp #$14                ; DEL
        beq @do_delete
        cmp #$11                ; cursor down
        beq @do_down
        cmp #$91                ; cursor up
        beq @do_up
        cmp #$1d                ; cursor right
        beq @do_right
        cmp #$9d                ; cursor left
        beq @do_left
        cmp #$20
        bcc main_loop            ; < $20: an unhandled control code -- ignore
        cmp #$60
        bcs main_loop            ; >= $60: not in the range this editor accepts -- ignore
        jsr insert_char
        jmp main_loop

@do_return:
        jsr handle_return
        jmp main_loop
@do_delete:
        jsr handle_delete
        jmp main_loop
@do_down:
        jsr move_cursor_down
        jmp main_loop
@do_up:
        jsr move_cursor_up
        jmp main_loop
@do_right:
        jsr move_cursor_right
        jmp main_loop
@do_left:
        jsr move_cursor_left
        jmp main_loop
@do_quit:
        jmp do_quit
@do_new:
        jmp do_new
@do_save:
        jmp do_save
@do_delete_file:
        jmp do_delete
@do_load:
        jmp do_load
@do_directory:
        jmp do_directory

do_quit:
        CLS
        PRINT bye_msg
        rts

; Busy-waits for any key via GETIN, discarding which one it was --
; used only for "press any key to continue" at startup, not the main
; editing loop, which needs to know exactly which key arrived.
wait_for_key:
        jsr GETIN
        beq wait_for_key
        rts

; Recomputes screen_ptr from cursor_x/cursor_y. Call this after
; changing either one -- every cursor-movement routine below does.
recompute_screen_ptr:
        ldx cursor_y
        lda screen_row_lo,x
        clc
        adc cursor_x
        sta screen_ptr
        lda screen_row_hi,x
        adc #$00
        sta screen_ptr+1
        rts

show_cursor:
        ldy #$00
        lda (screen_ptr),y
        ora #$80
        sta (screen_ptr),y
        rts

hide_cursor:
        ldy #$00
        lda (screen_ptr),y
        and #$7f
        sta (screen_ptr),y
        rts

; A holds a PETSCII code already confirmed to be in $20-$5F (main_loop
; filters everything else out before calling this). Converts it to
; the matching screen code (see this file's own header comment for
; the two-case rule) and writes it at the cursor, then advances right.
insert_char:
        cmp #$40
        bcc @unchanged
        sec
        sbc #$40
@unchanged:
        ldy #$00
        sta (screen_ptr),y
        jsr move_cursor_right
        rts

; Moves to the start of the next line, unless already on the last
; editable row, in which case only the column resets -- there's no
; scrolling in this version (see this file's own header comment).
handle_return:
        lda #$00
        sta cursor_x
        lda cursor_y
        cmp #MAX_ROW
        beq @recompute
        inc cursor_y
@recompute:
        jmp recompute_screen_ptr

; Erases the character behind the cursor (wrapping to the end of the
; previous line if the cursor is at column 0) -- but only if there's
; actually something behind it; at the very first cell (0,0), DEL
; does nothing, matching ordinary backspace behavior rather than
; erasing the character the cursor happens to be sitting on.
handle_delete:
        lda cursor_x
        bne @can_delete
        lda cursor_y
        beq @nothing_to_delete
@can_delete:
        jsr move_cursor_left
        lda #$20
        ldy #$00
        sta (screen_ptr),y
@nothing_to_delete:
        rts

; Each move_cursor_* routine leaves screen_ptr correctly pointing at
; the (possibly unchanged) cursor position either way -- no separate
; recompute needed afterward.
move_cursor_right:
        lda cursor_x
        cmp #39
        bne @same_row
        lda cursor_y
        cmp #MAX_ROW
        bne @next_row
        rts                      ; already at the very last cell -- stay put
@next_row:
        lda #$00
        sta cursor_x
        inc cursor_y
        jmp recompute_screen_ptr
@same_row:
        inc cursor_x
        jmp recompute_screen_ptr

move_cursor_left:
        lda cursor_x
        bne @same_row
        lda cursor_y
        beq @at_origin           ; already at (0,0) -- stay put
        dec cursor_y
        lda #39
        sta cursor_x
        jmp recompute_screen_ptr
@at_origin:
        rts
@same_row:
        dec cursor_x
        jmp recompute_screen_ptr

move_cursor_down:
        lda cursor_y
        cmp #MAX_ROW
        beq @no_move
        inc cursor_y
        jmp recompute_screen_ptr
@no_move:
        rts

move_cursor_up:
        lda cursor_y
        beq @no_move
        dec cursor_y
        jmp recompute_screen_ptr
@no_move:
        rts

title_msg:
        .text "SIMPLE TEXT EDITOR", 13, 0
instructions_msg:
        .text "CURSOR KEYS TO MOVE, DEL TO ERASE.", 13
        .text "F1=QUIT F2=NEW F3=SAVE F4=DELETE", 13
        .text "F5=LOAD F7=DIRECTORY", 13, 0
continue_msg:
        .text "PRESS ANY KEY TO START...", 13, 0
bye_msg:
        .text "GOODBYE", 13, 0
help_status_msg:
        .text "F1-F7: QUIT/NEW/SAVE/DEL/LOAD/DIR", 0
save_prompt_msg:
        .text "SAVE AS: ", 0
load_prompt_msg:
        .text "LOAD: ", 0
delete_prompt_msg:
        .text "DELETE: ", 0
confirm_delete_msg:
        .text "DELETE? (Y/N)", 0
saved_msg:
        .text "SAVED.", 0
new_msg:
        .text "NEW FILE.", 0
deleted_msg:
        .text "DELETED.", 0
loaded_msg:
        .text "LOADED.", 0
not_found_msg:
        .text "FILE NOT FOUND.", 0
cancelled_msg:
        .text "CANCELLED.", 0
directory_title_msg:
        .text "DISK DIRECTORY -- PRESS ANY KEY WHEN DONE", 13, 0
open_error_msg:
        .text "COULDN'T OPEN THE DIRECTORY -- IS A DRIVE", 13
        .text "PRESENT AND A DISK ATTACHED?", 13, 0
press_key_msg:
        .text 13, "PRESS ANY KEY TO RETURN TO THE EDITOR...", 0

; Row N's screen memory starts at $0400 + N*40 -- precomputed here
; rather than multiplying by 40 at runtime (not a power of two, and
; not one of lib/math.inc's own small non-power-of-two multipliers
; either), since there are only ever 25 possible rows to begin with.
screen_row_lo:
        .byte $00, $28, $50, $78, $A0, $C8, $F0, $18
        .byte $40, $68, $90, $B8, $E0, $08, $30, $58
        .byte $80, $A8, $D0, $F8, $20, $48, $70, $98
        .byte $C0
screen_row_hi:
        .byte $04, $04, $04, $04, $04, $04, $04, $05
        .byte $05, $05, $05, $05, $05, $06, $06, $06
        .byte $06, $06, $06, $06, $07, $07, $07, $07
        .byte $07

; --- status line ---------------------------------------------------

; Writes the null-terminated PETSCII message pointed to by msg_ptr
; (set by the caller) to row 24, converting each byte to a screen
; code the same way insert_char does, then pads the rest of the row
; with spaces to erase whatever was there before. Leaves the printed
; (pre-padding) length in status_msg_len, which prompt_filename uses
; to know where typed characters should start echoing.
print_status:
        ldy #$00
@copy_loop:
        lda (msg_ptr),y
        beq @pad
        cmp #$40
        bcc @unchanged
        sec
        sbc #$40
@unchanged:
        sta STATUS_ROW_ADDR,y
        iny
        cpy #40
        bne @copy_loop
        rts                       ; message filled the whole row exactly
@pad:
        sty status_msg_len
        lda #$20
@pad_loop:
        cpy #40
        beq @done
        sta STATUS_ROW_ADDR,y
        iny
        jmp @pad_loop
@done:
        rts

show_help_status:
        lda #<help_status_msg
        sta msg_ptr
        lda #>help_status_msg
        sta msg_ptr+1
        jmp print_status

; --- filename prompt -------------------------------------------------

; Shows the prompt msg_ptr already points to (set by the caller) at
; the start of row 24, then reads a filename into filename_buf (up to
; 16 characters, PETSCII, DEL to backspace) until RETURN. Returns the
; typed length in A and filename_len -- 0 means the prompt was
; cancelled (RETURN pressed with nothing typed).
prompt_filename:
        jsr print_status
        lda #$00
        sta filename_len
@type_loop:
        jsr GETIN
        beq @type_loop
        cmp #$03                ; RUN/STOP -- the C64 keyboard has no
        beq @cancel_via_stop       ; key labeled ESC; RUN/STOP (also
                                       ; reachable as Ctrl+C) is the
                                       ; conventional C64 equivalent for
                                       ; "abort this," so it cancels the
                                       ; same way an empty filename does
        cmp #$0d
        beq @done
        cmp #$14
        beq @do_backspace
        cmp #$20
        bcc @type_loop
        cmp #$60
        bcs @type_loop

        ldy filename_len
        cpy #16
        bcs @type_loop             ; buffer already full -- ignore

        sta filename_buf,y         ; store the raw PETSCII byte
        sta prompt_scratch
        cmp #$40
        bcc @sc_done
        sec
        sbc #$40
        sta prompt_scratch
@sc_done:
        tya
        clc
        adc status_msg_len
        tay
        lda prompt_scratch
        sta STATUS_ROW_ADDR,y
        inc filename_len
        jmp @type_loop

@do_backspace:
        lda filename_len
        beq @type_loop              ; nothing typed yet -- ignore
        dec filename_len
        ldy filename_len
        tya
        clc
        adc status_msg_len
        tay
        lda #$20
        sta STATUS_ROW_ADDR,y
        jmp @type_loop

@cancel_via_stop:
        lda #$00
        sta filename_len

@done:
        lda filename_len
        rts

; Appends ",S,W" (write) to filename_buf right after the typed
; portion, and sets filename_total_len to the combined length --
; what's actually passed to SETNAM.
append_write_suffix:
        ldy filename_len
        lda #$2c
        sta filename_buf,y
        iny
        lda #$53
        sta filename_buf,y
        iny
        lda #$2c
        sta filename_buf,y
        iny
        lda #$57
        sta filename_buf,y
        iny
        sty filename_total_len
        rts

; Same as append_write_suffix, but ",S,R" (read).
append_read_suffix:
        ldy filename_len
        lda #$2c
        sta filename_buf,y
        iny
        lda #$53
        sta filename_buf,y
        iny
        lda #$2c
        sta filename_buf,y
        iny
        lda #$52
        sta filename_buf,y
        iny
        sty filename_total_len
        rts

; --- save / load -----------------------------------------------------

; Streams all 1000 screen bytes ($0400-$07E7) to the current output
; channel (already CHKOUT'd to an open file by the caller), converting
; each screen code back to PETSCII -- the exact reverse of insert_char.
write_screen_to_file:
        lda #<$0400
        sta copy_ptr
        lda #>$0400
        sta copy_ptr+1
        ldx #4                     ; 4 * 240 = 960 bytes -- rows 0-23
                                      ; only; row 24 is the status line,
                                      ; not part of the document
@outer:
        ldy #$00
@inner:
        lda (copy_ptr),y
        and #$7f                    ; strip any cursor reverse-video bit
        cmp #$20
        bcs @unchanged                ; >= $20 -- PETSCII unchanged
        clc
        adc #$40
@unchanged:
        jsr CHROUT
        iny
        cpy #240
        bne @inner
        lda copy_ptr
        clc
        adc #240
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1
        dex
        bne @outer
        rts

; Reverse of write_screen_to_file: reads up to 1000 bytes from the
; current input channel (already CHKIN'd by the caller), converting
; PETSCII to screen codes, filling anything past the file's own end
; with spaces -- a file shorter than 1000 bytes leaves the rest of the
; screen blank, exactly like starting a new document.
read_file_to_screen:
        lda #<$0400
        sta copy_ptr
        lda #>$0400
        sta copy_ptr+1
        ldx #4                      ; 4 * 240 = 960 bytes -- rows 0-23
                                       ; only; row 24 is the status line
@outer:
        ldy #$00
@inner:
        jsr READST
        and #$40
        bne @fill_space
        jsr CHRIN
        cmp #$40
        bcc @sc_unchanged
        sec
        sbc #$40
        jmp @store
@sc_unchanged:
        jmp @store
@fill_space:
        lda #$20
@store:
        sta (copy_ptr),y
        iny
        cpy #240
        bne @inner
        lda copy_ptr
        clc
        adc #240
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1
        dex
        bne @outer
        rts

; Clears the editable area (rows 0-23) to blank, leaving the status
; line untouched, and resets the cursor to (0,0) -- "start a new
; file." No confirmation prompt: this matches F5 (load), which
; already overwrites the current document without asking first, so a
; second, inconsistent rule here (confirm to clear, but not to
; overwrite via load) would be more surprising than helpful.
do_new:
        lda #<$0400
        sta copy_ptr
        lda #>$0400
        sta copy_ptr+1
        ldx #$04                    ; 4 * 240 = 960 bytes -- rows 0-23
                                       ; only, matching write_screen_to_
                                       ; file/read_file_to_screen's own
                                       ; loop shape
@outer:
        lda #$20                    ; space screen code
        ldy #$00
@inner:
        sta (copy_ptr),y
        iny
        cpy #240
        bne @inner
        lda copy_ptr
        clc
        adc #240
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1
        dex
        bne @outer

        lda #$00
        sta cursor_x
        sta cursor_y
        jsr recompute_screen_ptr

        lda #<new_msg
        sta msg_ptr
        lda #>new_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

; Builds "S0:" followed by the typed filename (filename_buf, up to
; filename_len bytes) into scratch_cmd_buf -- the SCRATCH (delete)
; command actually sent over the command channel, e.g. "S0:NOTES".
; The "0:" drive-number prefix matters, not just style: shortened
; forms without it are documented to cause real 1541 firmware to try
; allocating a buffer for a second drive that doesn't exist, which can
; corrupt data -- see this file's own header comment.
build_scratch_cmd:
        lda #$53                   ; 'S'
        sta scratch_cmd_buf
        lda #$30                   ; '0'
        sta scratch_cmd_buf+1
        lda #$3a                    ; ':'
        sta scratch_cmd_buf+2
        ldy #$00
@copy_loop:
        cpy filename_len
        beq @done
        lda filename_buf,y
        sta scratch_cmd_buf+3,y
        iny
        jmp @copy_loop
@done:
        tya
        clc
        adc #$03
        sta scratch_cmd_len
        rts

; Scratches (deletes) the file named in filename_buf (already set by
; the caller, e.g. via prompt_filename) using the standard "S0:name"
; command-channel syntax. Returns A=1 if a file was actually deleted,
; A=0 if nothing matched -- which is a normal, expected result when
; saving a document for the first time, not an error, so do_save below
; doesn't check this at all; do_delete does, to tell "deleted" from
; "wasn't there to begin with" apart for the person watching.
;
; The command channel reports its result as
; "01,FILES SCRATCHED,NN,00", where NN is the actual count -- this
; reads past the first two comma-separated fields to reach it, then
; checks whether it's exactly "0" (a lone zero digit immediately
; followed by a comma) or something else (any other single digit, or
; more digits before the comma, as in "04").
scratch_current_file:
        jsr build_scratch_cmd

        lda scratch_cmd_len
        ldx #<scratch_cmd_buf
        ldy #>scratch_cmd_buf
        jsr SETNAM
        lda #$0f                   ; logical file 15
        ldx #$08                   ; device 8
        ldy #$0f                    ; secondary address 15 = command
                                       ; channel -- this is how a
                                       ; SCRATCH command is actually
                                       ; sent: as the "filename" an
                                       ; OPEN to the command channel
                                       ; carries
        jsr SETLFS
        jsr OPEN
        bcc @open_ok
        lda #$00                    ; couldn't even open the command
        rts                          ; channel -- treat as "nothing
                                        ; scratched" rather than
                                        ; guessing further
@open_ok:
        ldx #$0f
        jsr CHKIN

        ldx #$00                    ; comma counter
@skip_loop:
        jsr READST
        bne @nothing_found            ; response ended before reaching
                                          ; the count field
        jsr CHRIN
        cmp #$2c                       ; ','
        bne @skip_loop
        inx
        cpx #$02
        bne @skip_loop

        jsr READST
        bne @nothing_found
        jsr CHRIN
        cmp #$30                       ; '0' -- the count's first digit
        bne @something_found            ; nonzero first digit -- found
        jsr READST
        bne @nothing_found
        jsr CHRIN
        cmp #$30                        ; '0' -- the count's second digit;
        bne @something_found             ; the real response always
                                             ; zero-pads this field to
                                             ; two digits ("00", "01",
                                             ; "04", ...), so checking
                                             ; both digits are '0' is
                                             ; the correct, exact test
                                             ; for zero -- not a comma
                                             ; check, which an earlier
                                             ; version of this got
                                             ; wrong: a bare "0," never
                                             ; actually appears, only
                                             ; "00,", so that version
                                             ; misread "00" as nonzero
        jmp @nothing_found
@something_found:
        jsr CLRCHN
        lda #$0f
        jsr CLOSE
        lda #$01
        rts
@nothing_found:
        jsr CLRCHN
        lda #$0f
        jsr CLOSE
        lda #$00
        rts

; Unlike F2 (new) and F5 (load), which overwrite the in-memory
; document without confirmation, F4 asks first -- deleting a file from
; disk has no "just reload it" recovery path the way an unsaved screen
; does, so the stakes are genuinely different, not just a matter of
; being consistent with the other commands for its own sake.
do_delete:
        lda #<delete_prompt_msg
        sta msg_ptr
        lda #>delete_prompt_msg
        sta msg_ptr+1
        jsr prompt_filename
        cmp #$00
        bne @ask_confirm
        jmp @cancelled

@ask_confirm:
        lda #<confirm_delete_msg
        sta msg_ptr
        lda #>confirm_delete_msg
        sta msg_ptr+1
        jsr print_status
@confirm_wait:
        jsr GETIN
        beq @confirm_wait
        cmp #$03                    ; RUN/STOP -- cancel here too, same
        beq @cancelled                ; as at the filename prompt itself
        cmp #$4e                    ; 'N'
        beq @cancelled
        cmp #$59                     ; 'Y'
        bne @confirm_wait             ; anything else -- keep waiting

        jsr scratch_current_file
        cmp #$01
        beq @deleted

        lda #<not_found_msg
        sta msg_ptr
        lda #>not_found_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

@deleted:
        lda #<deleted_msg
        sta msg_ptr
        lda #>deleted_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

@cancelled:
        lda #<cancelled_msg
        sta msg_ptr
        lda #>cancelled_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

do_save:
        lda #<save_prompt_msg
        sta msg_ptr
        lda #>save_prompt_msg
        sta msg_ptr+1
        jsr prompt_filename
        cmp #$00
        bne @proceed
        jmp @cancelled
@proceed:
        jsr scratch_current_file   ; delete any existing file with this
                                      ; name first, then write fresh --
                                      ; see this file's own header
                                      ; comment for why this project
                                      ; uses scratch-then-write instead
                                      ; of "@0:...,S,W" save-and-replace.
                                      ; Must happen before
                                      ; append_write_suffix below, which
                                      ; modifies filename_buf itself;
                                      ; the result doesn't matter here,
                                      ; whether or not anything existed
                                      ; to delete
        jsr append_write_suffix

        lda filename_total_len
        ldx #<filename_buf
        ldy #>filename_buf
        jsr SETNAM
        lda #1                     ; logical file 1
        ldx #8                     ; device 8
        ldy #3                      ; secondary address 3 (write)
        jsr SETLFS
        jsr OPEN
        ldx #1
        jsr CHKOUT

        jsr write_screen_to_file

        jsr CLRCHN
        lda #1
        jsr CLOSE

        lda #<saved_msg
        sta msg_ptr
        lda #>saved_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

@cancelled:
        lda #<cancelled_msg
        sta msg_ptr
        lda #>cancelled_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

do_load:
        lda #<load_prompt_msg
        sta msg_ptr
        lda #>load_prompt_msg
        sta msg_ptr+1
        jsr prompt_filename
        cmp #$00
        bne @proceed
        jmp @cancelled
@proceed:
        jsr append_read_suffix

        lda filename_total_len
        ldx #<filename_buf
        ldy #>filename_buf
        jsr SETNAM
        lda #2                     ; logical file 2
        ldx #8                     ; device 8
        ldy #2                      ; secondary address 2 (read)
        jsr SETLFS
        jsr OPEN
        ldx #2
        jsr CHKIN

        jsr READST
        and #$40
        beq @found
        jmp @not_found

@found:
        jsr read_file_to_screen
        jsr CLRCHN
        lda #2
        jsr CLOSE

        lda #$00
        sta cursor_x
        sta cursor_y
        jsr recompute_screen_ptr

        lda #<loaded_msg
        sta msg_ptr
        lda #>loaded_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

@not_found:
        jsr CLRCHN
        lda #2
        jsr CLOSE
        lda #<not_found_msg
        sta msg_ptr
        lda #>not_found_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

@cancelled:
        lda #<cancelled_msg
        sta msg_ptr
        lda #>cancelled_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

; --- directory listing ------------------------------------------------

; Copies all 1000 bytes of screen memory to/from the $C000 scratch
; buffer -- used to show the directory listing on a blank screen and
; then restore exactly what was being edited, the same idea
; write_screen_to_file/read_file_to_screen use for files, just to
; another block of memory instead of a KERNAL channel.
save_screen_backup:
        ldx #$00
@loop:
        lda $0400,x
        sta SCREEN_BACKUP,x
        lda $0500,x
        sta SCREEN_BACKUP+$0100,x
        lda $0600,x
        sta SCREEN_BACKUP+$0200,x
        lda $0700,x
        sta SCREEN_BACKUP+$0300,x
        inx
        bne @loop
        ; the four 256-byte blocks above cover $0400-$07FF, 24 bytes
        ; more than the real 1000-byte screen -- harmless, since
        ; $07E8-$07FF is still ordinary RAM this program doesn't use
        ; for anything else, and copying a few extra bytes both ways
        ; is simpler than a fifth, short-length loop just to trim them
        rts

restore_screen_backup:
        ldx #$00
@loop:
        lda SCREEN_BACKUP,x
        sta $0400,x
        lda SCREEN_BACKUP+$0100,x
        sta $0500,x
        lda SCREEN_BACKUP+$0200,x
        sta $0600,x
        lda SCREEN_BACKUP+$0300,x
        sta $0700,x
        inx
        bne @loop
        rts

; Prints the 16-bit value in dec_lo/dec_hi as decimal via CHROUT, with
; leading zeros suppressed but always at least one digit printed (so
; zero itself prints as "0", not nothing) -- repeated subtraction of
; 10000/1000/100/10/1 rather than a binary-to-decimal divide, which is
; simpler to get right and plenty fast enough for the small values
; (block counts, up to a few hundred) this is ever actually used for.
print_decimal16:
        lda #$00
        sta dec_started
        ldx #$00
@digit_loop:
        lda #$00
        sta dec_digit
@subtract_loop:
        lda dec_hi
        cmp pow10_hi,x
        bcc @next_digit
        bne @do_subtract
        lda dec_lo
        cmp pow10_lo,x
        bcc @next_digit
@do_subtract:
        lda dec_lo
        sec
        sbc pow10_lo,x
        sta dec_lo
        lda dec_hi
        sbc pow10_hi,x
        sta dec_hi
        inc dec_digit
        jmp @subtract_loop
@next_digit:
        lda dec_digit
        bne @print_it
        lda dec_started
        bne @print_it
        cpx #4
        bne @skip_print
@print_it:
        lda #$01
        sta dec_started
        lda dec_digit
        clc
        adc #$30
        jsr CHROUT
@skip_print:
        inx
        cpx #5
        bne @digit_loop
        rts

pow10_lo: .byte <10000, <1000, <100, <10, <1
pow10_hi: .byte >10000, >1000, >100, >10, >1

do_directory:
        jsr save_screen_backup
        CLS
        PRINT directory_title_msg

        lda #$01
        ldx #<dirname
        ldy #>dirname
        jsr SETNAM
        lda #3                     ; logical file 3
        ldx #8
        ldy #$00                    ; secondary address -- must be 0 for
                                       ; the "$" directory request
                                       ; specifically (unlike an ordinary
                                       ; file read, where 2-14 all work
                                       ; equally well, used for load/save
                                       ; elsewhere in this file); see
                                       ; dir_sa_test.asm and this
                                       ; project's own CHANGELOG.md for
                                       ; how this was actually confirmed
        jsr SETLFS
        jsr OPEN
        bcc @directory_open_ok
        jmp directory_open_failed
@directory_open_ok:
        ldx #3
        jsr CHKIN

        jsr CHRIN                  ; skip the 2-byte fake load address
        jsr CHRIN

dir_loop:
        jsr CHRIN
        sta dir_link_lo
        jsr CHRIN
        sta dir_link_hi
        lda dir_link_lo
        ora dir_link_hi
        beq dir_done

        jsr CHRIN
        sta dir_num_lo
        jsr CHRIN
        sta dir_num_hi

        lda dir_num_lo
        ora dir_num_hi
        beq @text_only

        lda dir_num_lo
        sta dec_lo
        lda dir_num_hi
        sta dec_hi
        jsr print_decimal16
        lda #$20
        jsr CHROUT

@text_only:
@text_loop:
        jsr READST
        and #$40
        bne dir_done              ; the stream ended before a $00
                                      ; terminator showed up -- stop
                                      ; cleanly here instead of looping
                                      ; forever waiting for one
        jsr CHRIN
        beq @text_done
        jsr CHROUT
        jmp @text_loop
@text_done:
        lda #$92                  ; reverse off -- defensive: makes sure
                                      ; the disk name line's reverse-on
                                      ; code can never visually bleed
                                      ; into every line after it
        jsr CHROUT
        lda #13
        jsr CHROUT
        jmp dir_loop

directory_open_failed:
        PRINT open_error_msg
        PRINT press_key_msg
        jsr wait_for_key
        jsr restore_screen_backup
        jsr recompute_screen_ptr
        jmp main_loop

dir_done:
        jsr CLRCHN
        lda #3
        jsr CLOSE

        PRINT press_key_msg
        jsr wait_for_key

        jsr restore_screen_backup
        jsr recompute_screen_ptr
        jmp main_loop

dirname: .text "$"
