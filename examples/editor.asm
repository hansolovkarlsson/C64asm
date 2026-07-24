; editor.asm - a simple text editor with new/save/save-as/delete/load
; and directory listing, and a document that can scroll well beyond
; one screen (see DOC_BUF's own comment, further down, for how).
;
; The editable area is rows 0-23 (was all 25 rows in this file's first
; version) -- row 24 is now a status/prompt line, showing either brief
; help text, a "SAVE AS: " filename prompt, or a one-line result
; message ("SAVED.", "FILE NOT FOUND.", and so on) after an operation.
; This is a deliberate change from the first version, not an
; afterthought: load/save/directory are inherently UI-having
; operations, and there's nowhere else on a one-screen-at-a-time
; editor to put that UI, even one whose document isn't actually
; limited to a single screen anymore.
;
; Controls:
;   any typable key       insert it at the cursor, advance right
;   RETURN                 move to the start of the next line
;   DEL                    erase the character behind the cursor
;   cursor up/down/left/right   move without changing anything
;   HOME/CLR                page the viewport up/down a full screen
;                              (24 rows) at once -- not a cursor-key
;                              combination; see handle_page_down's own
;                              comment, further down, for why none of
;                              CTRL/SHIFT/C=+cursor could mean that
;
; Typing, RETURN, DEL, or any cursor key/page key shows the cursor's
; current row number ("ROW n") on the status line -- see
; show_row_status, further down.
;
;   F1                     quit
;   F2                      new file -- clears the document, no
;                              confirmation prompt (matches F5/load,
;                              which already overwrites without asking)
;   F3                      save -- reuses the document's current
;                              filename (already loaded, or already
;                              saved once this session) if it has one,
;                              otherwise prompts for one, same as F6
;   F4                      delete a file -- shows a list of what's
;                              actually on disk to pick from (cursor to
;                              move, RETURN to pick), then asks Y/N to
;                              confirm -- unlike F2/F5/F3-reusing-a-
;                              name, since there's no "just reload it"
;                              way back from deleting a file on disk
;   F5                      load -- shows the same kind of list F4
;                              does, rather than a filename typed blind
;   F6                      save as -- always prompts for a filename,
;                              even if the document already has one
;                              (unlike plain F3); the deliberate way to
;                              save under a different name, or to name
;                              a document for the first time
;   F7                      show the disk directory
;   F8                      show the bottom-row F-key assignments as a
;                              quick reference on the status line
;
; F3/F6's own filename prompt (typed, not the F4/F5 picker), and F4's
; own picker and Y/N confirmation, can all be backed out of without
; side effects: pressing RETURN with nothing typed at F3/F6's prompt
; cancels, and so does RUN/STOP anywhere in any of these -- the C64
; keyboard has no key labeled ESC, and RUN/STOP (also reachable as
; Ctrl+C) is the conventional C64 equivalent for "abort this."
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
; DOC_BUF (see that buffer's own declaration, further down) is the
; document's single source of truth, not screen memory -- save reads
; straight from it, and load fills it directly, with no PETSCII/
; screen-code conversion needed on either side, since DOC_BUF already
; stores plain PETSCII throughout, matching filename_buf's own
; convention. A file shorter than DOC_BUF leaves the rest of the
; document blank, exactly like starting a new one; save itself trims
; the other direction, scanning backward to skip writing any trailing
; rows that are entirely blank, so a short document doesn't take
; anywhere near as long on a real drive as writing the full, fixed
; buffer unconditionally every time would.
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

; copy_ptr: a third zero-page pointer, used two ways that never
; overlap in time, so sharing one address between them is safe: while
; copying the whole document to or from a file (write_screen_to_file/
; read_file_to_screen), and -- reused as "doc_ptr" via
; compute_doc_ptr, further down -- while insert_char/handle_delete/
; handle_return update DOC_BUF at the cursor's own current position
; during ordinary typing, which never happens while a file operation
; is also in progress. Kept separate from screen_ptr/msg_ptr either
; way, so none of the three ever collide.
;
; $03/$04, not $F5/$F6 (where an earlier version of this file placed
; it): real hardware's own KERNAL keyboard-scan IRQ actively clobbers
; $F3-$F6 -- this project's own past experience already identified
; that range as unsafe for exactly this purpose, and DOC_BUF's own
; introduction made this a real, reproducible bug rather than a
; theoretical one, since copy_ptr is now also used by loops
; (blank_doc_buf, render_viewport) long enough to run for multiple
; keyboard-scan IRQs in a row, giving many chances for it to actually
; get hit mid-loop -- confirmed directly, not just reasoned about, by
; running this file's own test suite with mini6502.py's zero-page
; poisoning simulation turned on (the default;
; simulate_zp_poisoning=True) rather than off. $02-$06 is free of that
; specific interference, and only $02 of it (kw_ptr/handler_vec) was
; already spoken for.
copy_ptr = $03

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

; pick_file's own state -- file_count is how many real files
; parse_directory_filenames actually found (capped at MAX_FILES),
; file_selection is the currently-highlighted row's index into
; file_list_buf (see that buffer's own comment, further down).
file_count     = $037b
file_selection = $037c

; The document's own "current" filename -- set after a successful
; load or save, cleared by F2 (new). current_filename_len == 0 means
; "no name yet" (a fresh, never-saved document), which is what makes
; F3 (save) prompt for one; a nonzero length means F3 can save
; straight back to that name without asking again, the same way most
; editors' plain "save" differs from "save as." F6 (save as) always
; prompts regardless, and updates this to whatever name was just typed
; -- see do_save/do_save_as/perform_save, further down.
current_filename_buf = $037d    ; 16 bytes: $037d-$038c
current_filename_len = $038d

; doc_top_row: which row of DOC_BUF (0-based, 0 to DOC_ROWS-1) is
; currently shown at the top of the visible 24-row window. The
; document's true row for a given on-screen cursor_y is always
; doc_top_row + cursor_y -- see compute_doc_ptr, further down, which
; is the only place that arithmetic actually happens.
doc_top_row = $038e

; compute_doc_ptr's own scratch: mult40_row holds the row value being
; multiplied by 40 (DOC_BUF's own row width) while it's worked on;
; mult40_temp_lo/hi holds row*8 temporarily, needed again after
; computing row*32, since 40 = 32 + 8 and the two partial shifts can't
; both live in the same 16-bit accumulator at once; doc_offset_lo/hi
; is that accumulator, and the final result.
mult40_row      = $038f
mult40_temp_lo  = $0390
mult40_temp_hi  = $0391
doc_offset_lo   = $0392
doc_offset_hi   = $0393

; insert_char's own scratch: the raw PETSCII byte just typed, held
; here across compute_doc_ptr (which clobbers A), since it's needed
; twice -- once written as-is into DOC_BUF, once converted to a screen
; code for the visible copy.
insert_char_scratch = $0394

; compute_doc_save_row_count's own state -- doc_scan_row is the row
; currently being checked while scanning backward for trailing blank
; ones; doc_save_row_count is the result, a 1-based count of how many
; rows from the start actually need writing to disk.
doc_scan_row       = $0395
doc_save_row_count = $0396

; show_row_status's own scratch: row_num_value holds the (1-based)
; row number while its decimal digits are being extracted;
; row_hundreds_digit/row_tens_digit/row_ones_digit hold those digits
; once found (DOC_ROWS is 200, so at most 3 digits, and the value
; always fits in a single byte). row_status_buf is where "ROW " plus
; those digits gets assembled before handing it to print_status --
; up to 4 + 3 + 1 (null) = 8 bytes.
row_num_value        = $0397
row_hundreds_digit    = $0398
row_tens_digit         = $0399
row_ones_digit          = $039a
row_status_buf           = $039b    ; 8 bytes: $039b-$03a2

; file_list_buf holds up to MAX_FILES filenames (as PETSCII, matching
; filename_buf's own convention -- not screen codes), one 16-byte,
; space-padded slot per file, filled in by parse_directory_filenames
; and read back by display_file_list/pick_file. 15*16 = 240 bytes.
; $C000-$C4EF is ordinary free RAM between BASIC ROM ($A000-$BFFF) and
; the VIC/SID/CIA I/O area ($D000+), unused by this program otherwise
; since it's plain machine code, not a BASIC program. (An earlier
; version of this file also kept a 1000-byte screen backup here,
; SCREEN_BACKUP, used only while the directory listing or file picker
; was on screen -- removed once DOC_BUF below made it unnecessary:
; render_viewport can always correctly reconstruct whatever was on
; screen before, from DOC_BUF and doc_top_row, which a literal
; byte-for-byte backup existed only to approximate in the first
; place.)
;
; MAX_FILES is 15, not a rounder-looking number, for a real reason:
; each slot is found by multiplying its index by 16 (MULT_16, a plain
; power-of-two shift), and that multiply is only correct up to index
; 15 (15*16 = 240, fits in a byte) -- index 16 (16*16 = 256) would
; silently wrap to 0 in an 8-bit register. 15 files is also
; comfortably within the roughly 22 rows available for the list once
; a couple of rows go to pick_file's own title and instructions.
MAX_FILES = 15
FILE_LIST_BUF = $C400      ; 240 bytes: $C400-$C4EF

; DOC_BUF: the document's own, single source of truth -- 200 rows of
; 40 columns each, PETSCII (not screen codes; matching filename_buf's
; own convention, and unlike screen memory itself, which is
; screen-code by hardware necessity). Screen memory is now only ever
; a rendered 24-row *view* onto some contiguous slice of this,
; starting at doc_top_row -- see render_viewport, further down. This
; is the change that makes scrolling possible at all: the previous
; version of this file used screen memory itself as the one and only
; copy of the document, which by definition can never hold more than
; one screen's worth of it.
;
; 200 rows (~8x a single screen) at $2000 is a deliberately generous,
; but still fixed and bounded, choice -- placed comfortably above this
; program's own code (which starts at $0801 and is nowhere near this
; large) and well below BASIC ROM ($A000), in what is otherwise
; entirely free RAM on a machine-code program like this one.
DOC_ROWS = 200
DOC_BUF = $2000            ; 8000 bytes: $2000-$3E7F

MAX_ROW = 23     ; last editable (screen) row -- row 24 is the status
                     ; line, and this is a screen-relative bound, not
                     ; a document one; DOC_ROWS is that bound
STATUS_ROW_ADDR = $07C0   ; $0400 + 24*40

        .basic start

        .include "lib/text.inc"
        .include "lib/math.inc"

start:
        CLS
        PRINT title_msg
        PRINT instructions_msg
        PRINT continue_msg
        jsr wait_for_key

        jsr blank_doc_buf          ; DOC_BUF is ordinary RAM -- real
                                       ; hardware doesn't guarantee it's
                                       ; already blank at power-on, any
                                       ; more than it guarantees
                                       ; current_filename_len is zero
                                       ; (see that own explicit reset,
                                       ; just below, for the identical
                                       ; reasoning)
        CLS
        lda #$00
        sta cursor_x
        sta cursor_y
        sta doc_top_row
        sta current_filename_len   ; must be explicit -- real hardware
                                       ; doesn't guarantee this RAM is
                                       ; zero at power-on, and a stray
                                       ; nonzero value here would make
                                       ; F3 wrongly believe the document
                                       ; already had a name
        jsr render_viewport
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
        bne @not_quit
        jmp do_quit
@not_quit:
        cmp #$89                ; F2
        bne @not_new
        jmp do_new
@not_new:
        cmp #$86                ; F3
        beq @do_save
        cmp #$8a                ; F4
        beq @do_delete_file
        cmp #$87                ; F5
        beq @do_load
        cmp #$8b                ; F6
        beq @do_save_as
        cmp #$88                ; F7
        beq @do_directory
        cmp #$8c                ; F8
        beq @do_help
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
        cmp #$13                ; HOME -- page up (see handle_page_up's
        beq @do_page_up            ; own comment for why this key, not
                                       ; a cursor-key combination)
        cmp #$93                ; CLR / SHIFT+HOME -- page down
        beq @do_page_down
        cmp #$20
        bcc main_loop            ; < $20: an unhandled control code -- ignore
        cmp #$60
        bcs main_loop            ; >= $60: not in the range this editor accepts -- ignore
        jsr insert_char
        bcc @update_row_status
        jmp main_loop            ; document-full message was just
                                     ; shown -- leave it, don't
                                     ; immediately overwrite it

@do_return:
        jsr handle_return
        bcc @update_row_status
        jmp main_loop            ; same as above
@do_delete:
        jsr handle_delete
        jmp @update_row_status
@do_down:
        jsr move_cursor_down
        jmp @update_row_status
@do_up:
        jsr move_cursor_up
        jmp @update_row_status
@do_right:
        jsr move_cursor_right
        jmp @update_row_status
@do_left:
        jsr move_cursor_left
        jmp @update_row_status
@do_page_up:
        jsr handle_page_up
        jmp @update_row_status
@do_page_down:
        jsr handle_page_down
        jmp @update_row_status

; Shared by every handler above (typing, RETURN, DEL, all four cursor
; keys, and page up/down) -- shows the cursor's current row number on
; the status line, replacing whatever was there before (matching how
; every other status update in this file already works: the most
; recent one simply wins). Deliberately not shared with the F-key
; handlers below, whose own status messages ("SAVED.", "CANCELLED.",
; and so on) are the more useful thing to leave on screen after one of
; those, not a row number.
@update_row_status:
        jsr show_row_status
        jmp main_loop

@do_save:
        jmp do_save
@do_delete_file:
        jmp do_delete
@do_load:
        jmp do_load
@do_save_as:
        jmp do_save_as
@do_directory:
        jmp do_directory
@do_help:
        jmp do_help

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

; Computes doc_offset_lo/hi := A * 40 (DOC_BUF's own row width). 40 =
; 32 + 8, so this shifts the row value left 3 times (row*8, saved
; aside), then 2 more times from the original row value again (row*32,
; since continuing the shift from row*8 would give row*32 too, but
; starting fresh from mult40_row keeps each partial result easy to
; check independently against 8*row and 32*row by hand), then adds the
; two partial results together. A plain 16-bit shift is needed at all
; -- unlike lib/math.inc's own MULT_N macros, which only ever produce
; an 8-bit result -- because a row index up to 199 times 40 can reach
; 7960, well past what a single byte holds.
compute_row_offset_40:
        sta mult40_row

        lda #$00
        sta doc_offset_hi
        lda mult40_row
        sta doc_offset_lo
        ldx #$03
@shift_by_8:
        asl doc_offset_lo
        rol doc_offset_hi
        dex
        bne @shift_by_8
        lda doc_offset_lo
        sta mult40_temp_lo
        lda doc_offset_hi
        sta mult40_temp_hi

        lda #$00
        sta doc_offset_hi
        lda mult40_row
        sta doc_offset_lo
        ldx #$05
@shift_by_32:
        asl doc_offset_lo
        rol doc_offset_hi
        dex
        bne @shift_by_32

        clc
        lda doc_offset_lo
        adc mult40_temp_lo
        sta doc_offset_lo
        lda doc_offset_hi
        adc mult40_temp_hi
        sta doc_offset_hi
        rts

; Points copy_ptr (reused here as "doc_ptr" -- see that zero-page
; declaration's own comment for why sharing it is safe) at DOC_BUF's
; own copy of the cursor's current cell: row (doc_top_row + cursor_y),
; column cursor_x. Called by insert_char/handle_delete/handle_return
; wherever they'd otherwise only have touched screen memory, so
; DOC_BUF stays the actual source of truth and the screen stays merely
; a rendering of it.
compute_doc_ptr:
        lda doc_top_row
        clc
        adc cursor_y
        jsr compute_row_offset_40

        clc
        lda doc_offset_lo
        adc #<DOC_BUF
        sta copy_ptr
        lda doc_offset_hi
        adc #>DOC_BUF
        sta copy_ptr+1

        lda cursor_x
        clc
        adc copy_ptr
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1
        rts

; Redraws all 24 visible rows from DOC_BUF, starting at doc_top_row --
; the only place the screen is ever filled from the document in bulk,
; used both after scrolling (doc_top_row changed) and in place of the
; old save_screen_backup/restore_screen_backup pair (returning from F7
; or the F4/F5 picker just means showing the same doc_top_row again,
; which this reconstructs correctly on its own, rather than needing a
; literal byte-for-byte copy of what was there before).
render_viewport:
        lda doc_top_row
        jsr compute_row_offset_40
        lda doc_offset_lo
        clc
        adc #<DOC_BUF
        sta copy_ptr
        lda doc_offset_hi
        adc #>DOC_BUF
        sta copy_ptr+1

        ldx #$00
@row_loop:
        lda screen_row_lo,x
        sta msg_ptr
        lda screen_row_hi,x
        sta msg_ptr+1

        ldy #$00
@col_loop:
        lda (copy_ptr),y
        cmp #$40
        bcc @char_unchanged
        sec
        sbc #$40
@char_unchanged:
        sta (msg_ptr),y
        iny
        cpy #40
        bne @col_loop

        lda copy_ptr
        clc
        adc #40
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1

        inx
        cpx #24
        bne @row_loop
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
; Writes the character, then advances -- except at the single
; absolute last cell (row MAX_ROW, column 39), where there's nowhere
; left for move_cursor_right to advance to. Without this check, every
; further keypress there would silently keep overwriting that same
; cell, discarding whatever was just typed, with no indication
; anything unusual was happening; a person typing normally would have
; no way to tell the document had quietly run out of room. Checked
; before writing rather than after: move_cursor_right's own "stay put"
; case only fires once the cursor is already sitting on that exact
; cell, which means the previous character just wrote successfully --
; showing the message and returning here skips that call entirely,
; since it would only re-derive the same "nowhere to go" result.
; Scrolls the viewport down/up by one document row and redraws, if
; there's actually room to -- shared by every place that needs to
; move past the visible screen's own top or bottom edge (typing or
; RETURN wrapping past the last visible row, and all four cursor
; keys). Returns with carry clear on a successful scroll (doc_top_row
; changed, screen redrawn) or carry set if there's nowhere further to
; go (already showing DOC_BUF's own first or last row) -- the caller
; is responsible for cursor_x/cursor_y and the final
; recompute_screen_ptr either way, since what those should end up as
; differs by caller (a wrapped RETURN resets cursor_x to 0; a plain
; down-arrow doesn't touch it at all).
try_scroll_down:
        lda doc_top_row
        cmp #DOC_ROWS - 24
        bcs @cant_scroll
        inc doc_top_row
        jsr render_viewport
        clc
        rts
@cant_scroll:
        sec
        rts

try_scroll_up:
        lda doc_top_row
        beq @cant_scroll
        dec doc_top_row
        jsr render_viewport
        clc
        rts
@cant_scroll:
        sec
        rts

; HOME/CLR (page up/down): moves the viewport by a full screen (24
; rows) at once, rather than one row at a time. Not a cursor-key
; combination -- confirmed directly (not assumed) that none of the
; usual modifiers work for that here: CTRL+cursor produces nothing at
; all in the keyboard buffer (the KERNAL doesn't decode that
; combination for cursor keys), and both SHIFT+cursor and C=+cursor
; produce the exact same code as the opposite plain cursor key (e.g.
; SHIFT+down is indistinguishable from plain up), so neither can mean
; anything different from what the plain cursor keys already do. HOME
; and CLR are a genuinely distinct, otherwise-unused key pair this
; editor doesn't already assign any other meaning to -- the same kind
; of repurposing RUN/STOP already gets for "cancel" elsewhere in this
; file, rather than its usual KERNAL meaning.
;
; cursor_x/cursor_y themselves don't change -- only doc_top_row does,
; capped at the same [0, DOC_ROWS-24] range try_scroll_down/
; try_scroll_up already enforce one row at a time, just moved by 24 at
; once here. Landing anywhere past DOC_BUF's own last visible window,
; or before its first, is capped to that boundary rather than
; overshooting it.
handle_page_down:
        lda doc_top_row
        clc
        adc #24
        cmp #DOC_ROWS - 24 + 1
        bcc @use_new_value
        lda #DOC_ROWS - 24
@use_new_value:
        sta doc_top_row
        jsr render_viewport
        jmp recompute_screen_ptr

handle_page_up:
        lda doc_top_row
        sec
        sbc #24
        bcs @use_new_value
        lda #$00
@use_new_value:
        sta doc_top_row
        jsr render_viewport
        jmp recompute_screen_ptr

; Writes the typed character into DOC_BUF (the document's own source
; of truth) and its screen-code form onto the visible screen, then
; advances -- except at the single absolute last cell of the entire
; document (DOC_BUF's own last row, column 39), where there's nowhere
; left for move_cursor_right to advance or scroll to. Without this
; check, every further keypress there would silently keep overwriting
; that same cell, discarding whatever was just typed, with no
; indication anything unusual was happening.
;
; Returns with carry set if the document-full message was just shown
; (the caller should leave it on screen, not immediately overwrite it
; with a row number), carry clear otherwise.
insert_char:
        sta insert_char_scratch

        lda cursor_x
        cmp #39
        bne @room_to_advance
        lda doc_top_row
        clc
        adc cursor_y
        cmp #DOC_ROWS - 1
        bne @room_to_advance

        ; the absolute last cell -- still write the character, but
        ; skip move_cursor_right entirely, since it would only
        ; re-derive the same "nowhere to go" result
        jsr compute_doc_ptr
        lda insert_char_scratch
        ldy #$00
        sta (copy_ptr),y

        lda insert_char_scratch
        cmp #$40
        bcc @screen_code_ready1
        sec
        sbc #$40
@screen_code_ready1:
        ldy #$00
        sta (screen_ptr),y

        lda #<document_full_msg
        sta msg_ptr
        lda #>document_full_msg
        sta msg_ptr+1
        jsr print_status
        sec
        rts

@room_to_advance:
        jsr compute_doc_ptr
        lda insert_char_scratch
        ldy #$00
        sta (copy_ptr),y

        lda insert_char_scratch
        cmp #$40
        bcc @screen_code_ready2
        sec
        sbc #$40
@screen_code_ready2:
        ldy #$00
        sta (screen_ptr),y

        jsr move_cursor_right
        clc
        rts

; Moves to the start of the next line -- scrolling the viewport down
; first if RETURN was pressed on the bottom visible row and there's
; more document below, the same as typing past column 39 there would.
; Only shows "no room left" (the same message insert_char's own
; absolute-last-cell case shows) when the viewport is already showing
; DOC_BUF's own very last row and truly can't scroll any further.
;
; Returns with carry set if that message was just shown (the caller
; should leave it on screen, not immediately overwrite it with a row
; number), carry clear otherwise -- the same convention insert_char
; uses.
handle_return:
        lda cursor_y
        cmp #MAX_ROW
        bne @same_viewport

        jsr try_scroll_down
        bcs @cant_advance
        lda #$00
        sta cursor_x
        jsr recompute_screen_ptr
        clc
        rts

@cant_advance:
        lda #$00
        sta cursor_x
        lda #<document_full_msg
        sta msg_ptr
        lda #>document_full_msg
        sta msg_ptr+1
        jsr print_status
        jsr recompute_screen_ptr
        sec
        rts

@same_viewport:
        lda #$00
        sta cursor_x
        inc cursor_y
        jsr recompute_screen_ptr
        clc
        rts

; Erases the character behind the cursor (wrapping to the end of the
; previous line if the cursor is at column 0, scrolling the viewport
; up first if that previous line isn't currently visible) -- but only
; if there's actually something behind it; at DOC_BUF's own very
; first cell, DEL does nothing, matching ordinary backspace behavior
; rather than erasing the character the cursor happens to be sitting
; on.
handle_delete:
        lda cursor_x
        bne @can_delete
        lda cursor_y
        bne @can_delete
        lda doc_top_row
        beq @nothing_to_delete
@can_delete:
        jsr move_cursor_left
        jsr compute_doc_ptr
        lda #$20
        ldy #$00
        sta (copy_ptr),y
        ldy #$00
        sta (screen_ptr),y
@nothing_to_delete:
        rts

; Each move_cursor_* routine leaves screen_ptr correctly pointing at
; the (possibly unchanged) cursor position either way -- no separate
; recompute needed afterward, except right after a scroll, which
; render_viewport doesn't itself touch screen_ptr for (see
; try_scroll_down/try_scroll_up's own comment for why that's left to
; each caller).
move_cursor_right:
        lda cursor_x
        cmp #39
        bne @same_row
        lda cursor_y
        cmp #MAX_ROW
        bne @next_row

        jsr try_scroll_down
        bcs @stay_put              ; nowhere further to go -- the
                                       ; absolute last cell
        lda #$00
        sta cursor_x
        jmp recompute_screen_ptr
@stay_put:
        rts

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
        bne @prev_row

        jsr try_scroll_up
        bcs @at_origin              ; nowhere further to go -- DOC_BUF's
                                        ; own very first cell
        lda #39
        sta cursor_x
        jmp recompute_screen_ptr
@at_origin:
        rts

@prev_row:
        dec cursor_y
        lda #39
        sta cursor_x
        jmp recompute_screen_ptr
@same_row:
        dec cursor_x
        jmp recompute_screen_ptr

move_cursor_down:
        lda cursor_y
        cmp #MAX_ROW
        bne @same_viewport
        jsr try_scroll_down
        bcs @no_move
        jmp recompute_screen_ptr
@no_move:
        rts
@same_viewport:
        inc cursor_y
        jmp recompute_screen_ptr

move_cursor_up:
        lda cursor_y
        bne @same_viewport
        jsr try_scroll_up
        bcs @no_move
        jmp recompute_screen_ptr
@no_move:
        rts
@same_viewport:
        dec cursor_y
        jmp recompute_screen_ptr

title_msg:
        .text "SIMPLE TEXT EDITOR", 13, 0
instructions_msg:
        .text "CURSOR KEYS TO MOVE, DEL TO ERASE.", 13
        .text "THE DOCUMENT SCROLLS -- KEEP GOING PAST", 13
        .text "THE BOTTOM OR TOP OF THE SCREEN.", 13
        .text "HOME/CLR PAGE UP/DOWN A FULL SCREEN.", 13
        .text "F1=QUIT F2=NEW F3=SAVE F4=DELETE", 13
        .text "F5=LOAD F6=SAVE AS F7=DIRECTORY", 13
        .text "F8=HELP (SHOWS THIS LIST AGAIN)", 13, 0
continue_msg:
        .text "PRESS ANY KEY TO START...", 13, 0
bye_msg:
        .text "GOODBYE", 13, 0
help_status_msg:
        .text "F1-F7: QUIT/NEW/SAVE/AS/DEL/LOAD/DIR", 0
fkey_help_msg:
        .text "1:Q 2:NEW 3:SAV 4:DEL 5:LD 6:AS 7:DR 8:?", 0
save_prompt_msg:
        .text "SAVE AS: ", 0
load_picker_title_msg:
        .text "SELECT A FILE TO LOAD", 13, 0
delete_picker_title_msg:
        .text "SELECT A FILE TO DELETE", 13, 0
picker_instructions_msg:
        .text "CURSOR MOVE, RETURN SELECT, STOP CANCEL", 13, 0
picker_empty_msg:
        .text "NO FILES ON DISK.", 13, 0
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
document_full_msg:
        .text "DOCUMENT FULL -- NO ROOM TO ADD MORE.", 0
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

; Shows "ROW n" (n = the cursor's current document row, 1-based,
; matching how people count lines) on the status line -- called after
; any operation that could move the cursor: typing, RETURN, DEL, all
; four cursor keys, and page up/down. DOC_ROWS is 200, so n is always
; 1-3 digits and always fits in a single byte; extracted the same way
; print_decimal16 extracts the (potentially larger) values it prints,
; via repeated subtraction, just simpler here since there's no need
; for a 16-bit value or more than 3 digits. Leading zeros are
; suppressed unless a higher digit was already shown (so row 5 prints
; as "ROW 5", not "ROW 005" or "ROW  5", but row 105 correctly prints
; its own zero in the tens place).
show_row_status:
        lda doc_top_row
        clc
        adc cursor_y
        clc
        adc #$01
        sta row_num_value

        ldx #$00
@hundreds_loop:
        lda row_num_value
        cmp #100
        bcc @hundreds_done
        sec
        sbc #100
        sta row_num_value
        inx
        jmp @hundreds_loop
@hundreds_done:
        stx row_hundreds_digit

        ldx #$00
@tens_loop:
        lda row_num_value
        cmp #10
        bcc @tens_done
        sec
        sbc #10
        sta row_num_value
        inx
        jmp @tens_loop
@tens_done:
        stx row_tens_digit
        lda row_num_value
        sta row_ones_digit

        ldy #$00
        lda #$52                    ; 'R'
        sta row_status_buf,y
        iny
        lda #$4f                     ; 'O'
        sta row_status_buf,y
        iny
        lda #$57                      ; 'W'
        sta row_status_buf,y
        iny
        lda #$20                       ; ' '
        sta row_status_buf,y
        iny

        lda row_hundreds_digit
        beq @skip_hundreds
        clc
        adc #$30
        sta row_status_buf,y
        iny
@skip_hundreds:
        lda row_hundreds_digit
        bne @force_tens
        lda row_tens_digit
        beq @skip_tens
@force_tens:
        lda row_tens_digit
        clc
        adc #$30
        sta row_status_buf,y
        iny
@skip_tens:
        lda row_ones_digit
        clc
        adc #$30
        sta row_status_buf,y
        iny

        lda #$00
        sta row_status_buf,y

        lda #<row_status_buf
        sta msg_ptr
        lda #>row_status_buf
        sta msg_ptr+1
        jmp print_status

; F8 -- shows the bottom-row F-key assignments as a quick reference.
; Abbreviated to fit the 40-column status line: the exact wording
; asked for ("1:Q 2:NEW 3:SAV 4:DEL 5:LD 6:AS 7:DIR 8:?") comes to 41
; characters, one over the limit -- "DIR" becomes "DR" here, the one
; abbreviation that could give a character without losing any of the
; other, harder-to-shorten words.
do_help:
        lda #<fkey_help_msg
        sta msg_ptr
        lda #>fkey_help_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

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

; Scans DOC_BUF backward from its own very last row, skipping any that
; are entirely blank, to find how many rows from the start actually
; need writing to disk -- so a short document doesn't take anywhere
; near as long to save on a real drive (roughly 40-50 bytes/second) as
; writing the full, fixed 8000-byte buffer unconditionally every time
; would. Sets doc_save_row_count to the result: always at least 1,
; even for a completely blank document, so an empty file still
; round-trips through a save/load cycle the same way any other one
; does, rather than becoming a zero-byte file that behaves
; differently.
compute_doc_save_row_count:
        lda #DOC_ROWS - 1
        sta doc_scan_row
@check_row:
        lda doc_scan_row
        jsr compute_row_offset_40
        lda doc_offset_lo
        clc
        adc #<DOC_BUF
        sta copy_ptr
        lda doc_offset_hi
        adc #>DOC_BUF
        sta copy_ptr+1

        ldy #$00
@col_loop:
        lda (copy_ptr),y
        cmp #$20
        bne @row_has_content
        iny
        cpy #40
        bne @col_loop

        lda doc_scan_row
        beq @row_has_content        ; row 0 itself is blank too -- still
                                        ; counts as the minimum, 1 row
        dec doc_scan_row
        jmp @check_row

@row_has_content:
        lda doc_scan_row
        clc
        adc #$01
        sta doc_save_row_count       ; 1-based: rows 0..doc_scan_row
        rts

; Streams DOC_BUF's own first doc_save_row_count rows (computed by
; compute_doc_save_row_count, which the caller must run first) to the
; current output channel (already CHKOUT'd to an open file). No
; PETSCII conversion needed here, unlike this routine's previous
; version -- DOC_BUF already holds plain PETSCII directly (see that
; buffer's own comment for why), not screen codes.
write_screen_to_file:
        lda #<DOC_BUF
        sta copy_ptr
        lda #>DOC_BUF
        sta copy_ptr+1

        ldx #$00
@row_loop:
        cpx doc_save_row_count
        beq @done

        ldy #$00
@col_loop:
        lda (copy_ptr),y
        jsr CHROUT
        iny
        cpy #40
        bne @col_loop

        lda copy_ptr
        clc
        adc #40
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1

        inx
        jmp @row_loop
@done:
        rts

; Reverse of write_screen_to_file: fills the entire 8000-byte DOC_BUF
; from the current input channel (already CHKIN'd by the caller),
; filling anything past the file's own end with spaces -- a file
; shorter than DOC_BUF leaves the rest of the document blank, exactly
; like starting a new one. No PETSCII conversion needed on this side
; either, for the same reason as write_screen_to_file above. The
; caller is responsible for resetting doc_top_row and the cursor and
; calling render_viewport afterward -- this only ever touches DOC_BUF
; itself, never the screen.
read_file_to_screen:
        lda #<DOC_BUF
        sta copy_ptr
        lda #>DOC_BUF
        sta copy_ptr+1

        ldx #$00
@row_loop:
        ldy #$00
@col_loop:
        jsr READST
        and #$40
        bne @fill_space
        jsr CHRIN
        jmp @store
@fill_space:
        lda #$20
@store:
        sta (copy_ptr),y
        iny
        cpy #40
        bne @col_loop

        lda copy_ptr
        clc
        adc #40
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1

        inx
        cpx #DOC_ROWS
        bne @row_loop
        rts

; Fills all of DOC_BUF with spaces (PETSCII, DOC_BUF's own convention
; -- see that buffer's declaration) -- shared by do_new and this
; file's own startup code, which both need exactly this and nothing
; more; startup additionally needs the intro screen shown first and a
; different final message, which is why this doesn't also reset
; cursor/doc_top_row/current_filename_len or render itself the way
; do_new's own, larger job does.
blank_doc_buf:
        lda #<DOC_BUF
        sta copy_ptr
        lda #>DOC_BUF
        sta copy_ptr+1

        ldx #$00
@row_loop:
        lda #$20
        ldy #$00
@col_loop:
        sta (copy_ptr),y
        iny
        cpy #40
        bne @col_loop

        lda copy_ptr
        clc
        adc #40
        sta copy_ptr
        lda copy_ptr+1
        adc #$00
        sta copy_ptr+1

        inx
        cpx #DOC_ROWS
        bne @row_loop
        rts

; Clears the entire document (all of DOC_BUF, not just the visible
; screen) to blank, scrolls back to the top, and resets the cursor to
; (0,0) -- "start a new file." No confirmation prompt: this matches F5
; (load), which already overwrites the current document without
; asking first, so a second, inconsistent rule here (confirm to
; clear, but not to overwrite via load) would be more surprising than
; helpful.
do_new:
        jsr blank_doc_buf

        lda #$00
        sta cursor_x
        sta cursor_y
        sta doc_top_row
        sta current_filename_len   ; a new document has no name yet --
                                       ; F3 should prompt for one, not
                                       ; silently reuse whatever the
                                       ; previous document was called
        jsr render_viewport
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
        jsr READST                  ; clear any status left over from
                                        ; this exchange -- see
                                        ; parse_directory_filenames's
                                        ; own identical fix and comment
                                        ; for the full reasoning
        lda #$01
        rts
@nothing_found:
        jsr CLRCHN
        lda #$0f
        jsr CLOSE
        jsr READST
        lda #$00
        rts

; Unlike F2 (new) and F5 (load), which overwrite the in-memory
; document without confirmation, F4 asks first -- deleting a file from
; disk has no "just reload it" recovery path the way an unsaved screen
; does, so the stakes are genuinely different, not just a matter of
; being consistent with the other commands for its own sake.
do_delete:
        lda #<delete_picker_title_msg
        ldy #>delete_picker_title_msg
        jsr pick_file
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

; Assumes filename_buf/filename_len are already set to the target name
; -- either copied from current_filename_buf (do_save reusing the
; document's existing name) or just typed via prompt_filename (do_save
; either with no existing name, or do_save_as). Scratches any existing
; file with that name first, then writes fresh (see this file's own
; header comment for why this project uses scratch-then-write instead
; of "@0:...,S,W" save-and-replace), remembers the name as the
; document's current one for next time, shows the result, and returns
; to main_loop.
perform_save:
        jsr compute_doc_save_row_count   ; doesn't depend on anything
                                             ; file-related, so compute
                                             ; it before even opening
                                             ; anything
        jsr scratch_current_file   ; must happen before
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

        ; remember this as the document's current filename -- copied
        ; from filename_buf's own first filename_len bytes, which
        ; append_write_suffix above left untouched (it only appends
        ; the ",S,W" suffix afterward, never modifies what's already
        ; there)
        ldx #$00
@remember_loop:
        cpx filename_len
        beq @remember_done
        lda filename_buf,x
        sta current_filename_buf,x
        inx
        jmp @remember_loop
@remember_done:
        stx current_filename_len

        lda #<saved_msg
        sta msg_ptr
        lda #>saved_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

; F3 -- reuses the document's current filename if it has one (already
; loaded, or already saved once this session), so a person editing a
; file they opened doesn't have to retype its name just to save
; changes. Falls through to do_save_as (prompting, same as always)
; when there isn't one yet: a brand new, never-saved document.
do_save:
        lda current_filename_len
        bne @has_current_name
        jmp do_save_as
@has_current_name:
        ldx #$00
@copy_loop:
        cpx current_filename_len
        beq @copy_done
        lda current_filename_buf,x
        sta filename_buf,x
        inx
        jmp @copy_loop
@copy_done:
        stx filename_len
        jmp perform_save

; F6 -- always prompts for a filename, even if the document already
; has one, unlike F3. The deliberate way to save under a different
; name, or to name a document for the first time.
do_save_as:
        lda #<save_prompt_msg
        sta msg_ptr
        lda #>save_prompt_msg
        sta msg_ptr+1
        jsr prompt_filename
        cmp #$00
        bne @proceed
        jmp @cancelled
@proceed:
        jmp perform_save

@cancelled:
        lda #<cancelled_msg
        sta msg_ptr
        lda #>cancelled_msg
        sta msg_ptr+1
        jsr print_status
        jmp main_loop

do_load:
        lda #<load_picker_title_msg
        ldy #>load_picker_title_msg
        jsr pick_file
        cmp #$00
        bne @proceed
        jmp @cancelled
@proceed:
        ; remember this as the document's current filename, before
        ; append_read_suffix below appends ",S,R" -- see perform_save's
        ; own identical pattern and comment
        ldx #$00
@remember_loop:
        cpx filename_len
        beq @remember_done
        lda filename_buf,x
        sta current_filename_buf,x
        inx
        jmp @remember_loop
@remember_done:
        stx current_filename_len

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
        sta doc_top_row              ; scroll back to the top of the
                                         ; newly loaded document, same
                                         ; as F2 (new) already does
        jsr render_viewport
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

; --- file picker (F4/F5 share this instead of typing a filename blind) ---

; Opens "$" and parses the directory listing into file_list_buf, one
; 16-byte (PETSCII, space-padded) slot per real file found -- skipping
; the disk name line (line number 0) and the trailing "BLOCKS FREE"
; line (recognized by not starting with a quote, unlike every real
; file's own text, which always does). Caps at MAX_FILES entries; any
; files beyond that aren't added to the list, but their bytes are
; still correctly consumed so the rest of the listing keeps parsing
; correctly. Sets file_count to how many were actually found (0 if
; the disk is empty, or if OPEN itself failed -- checked via carry,
; the same check do_directory's own listing uses).
parse_directory_filenames:
        lda #$00
        sta file_count

        lda #$01
        ldx #<dirname
        ldy #>dirname
        jsr SETNAM
        lda #3
        ldx #8
        ldy #$00
        jsr SETLFS
        jsr OPEN
        bcc @open_ok
        rts
@open_ok:
        ldx #3
        jsr CHKIN

        jsr CHRIN
        jsr CHRIN

@entry_loop:
        jsr READST
        beq @got_link_lo
        jmp @close_and_done
@got_link_lo:
        jsr CHRIN
        sta dir_link_lo
        jsr READST
        beq @got_link_hi
        jmp @close_and_done
@got_link_hi:
        jsr CHRIN
        sta dir_link_hi
        lda dir_link_lo
        ora dir_link_hi
        bne @got_link
        jmp @close_and_done
@got_link:

        jsr READST
        beq @got_num_lo
        jmp @close_and_done
@got_num_lo:
        jsr CHRIN
        sta dir_num_lo
        jsr READST
        beq @got_num_hi
        jmp @close_and_done
@got_num_hi:
        jsr CHRIN
        sta dir_num_hi

        lda dir_num_lo
        ora dir_num_hi
        beq @consume_rest          ; line 0 -- disk name line, not a file

@skip_leading_spaces:
        jsr READST
        beq @got_char
        jmp @close_and_done
@got_char:
        jsr CHRIN
        cmp #$20                    ; ' ' -- real files are padded with
        beq @skip_leading_spaces       ; leading spaces before the
                                           ; opening quote, to align the
                                           ; filename column against the
                                           ; block-count number's own
                                           ; variable width; skip past
                                           ; them rather than mistaking
                                           ; the padding for "not a file"
        cmp #$22                    ; '"'
        beq @is_a_file
        jmp @consume_rest             ; some other non-space,
                                          ; non-quote character (e.g.
                                          ; 'B' from "BLOCKS FREE.") --
                                          ; not a file; the byte just
                                          ; read is already consumed,
                                          ; @consume_rest just keeps
                                          ; going until $00

@is_a_file:
        lda file_count
        cmp #MAX_FILES
        bcs @consume_rest            ; list already full -- don't
                                         ; store, but still consume

        lda file_count
        MULT_16
        clc
        adc #<FILE_LIST_BUF
        sta copy_ptr
        lda #>FILE_LIST_BUF
        adc #$00
        sta copy_ptr+1

        ldy #$00
        lda #$20
@blank_loop:
        sta (copy_ptr),y
        iny
        cpy #16
        bne @blank_loop

        ldy #$00
@copy_name_loop:
        jsr READST
        bne @name_done
        jsr CHRIN
        cmp #$22                     ; closing quote -- name is done
        beq @name_done
        cpy #16
        bcs @name_done                 ; safety cap -- shouldn't
                                           ; trigger for anything this
                                           ; editor's own save wrote
        sta (copy_ptr),y
        iny
        jmp @copy_name_loop
@name_done:
        inc file_count

@consume_rest:
        jsr READST
        beq @got_text_char
        jmp @close_and_done
@got_text_char:
        jsr CHRIN
        bne @consume_rest
        jmp @entry_loop               ; $00 -- end of this line's text

@close_and_done:
        jsr CLRCHN
        lda #3
        jsr CLOSE
        jsr READST                 ; real READST's own status persists
                                       ; until explicitly read again,
                                       ; regardless of what other
                                       ; operations happen in between --
                                       ; reading (and so clearing) it
                                       ; here stops a stale EOF flag
                                       ; from this directory read (set
                                       ; by the final CHRIN that
                                       ; happened to also be the last
                                       ; byte of the listing) from
                                       ; being mistaken by a later,
                                       ; completely unrelated OPEN for
                                       ; that file not existing
        rts

; Displays file_list_buf's first file_count entries, one per screen
; row starting at row 2 (rows 0-1 are pick_file's own title and
; instructions), in columns 2-17. Converts each stored PETSCII
; character to its screen-code equivalent the same way insert_char
; does. Doesn't draw the "> " selector itself -- see
; set_selector_char, called separately by pick_file's own loop.
display_file_list:
        ldx #$00
@row_loop:
        cpx file_count
        beq @done

        txa
        clc
        adc #$02
        tay
        lda screen_row_lo,y
        clc
        adc #$02                    ; +2 columns, past the selector gap
        sta msg_ptr
        lda screen_row_hi,y
        adc #$00
        sta msg_ptr+1

        txa
        MULT_16
        clc
        adc #<FILE_LIST_BUF
        sta copy_ptr
        lda #>FILE_LIST_BUF
        adc #$00
        sta copy_ptr+1

        ldy #$00
@char_loop:
        lda (copy_ptr),y
        cmp #$40
        bcc @char_unchanged
        sec
        sbc #$40
@char_unchanged:
        sta (msg_ptr),y
        iny
        cpy #16
        bne @char_loop

        inx
        jmp @row_loop
@done:
        rts

; Writes A (a screen code -- '>' and ' ', the only two values this is
; ever called with, are both in the "unchanged" $20-$3F PETSCII/
; screen-code range, so no conversion is needed for either) to column
; 0 of the screen row showing file_list_buf's entry number X
; (0-based) -- used to draw or erase the "> " selector as
; file_selection moves.
set_selector_char:
        pha
        txa
        clc
        adc #$02
        tay
        lda screen_row_lo,y
        sta msg_ptr
        lda screen_row_hi,y
        sta msg_ptr+1
        pla
        ldy #$00
        sta (msg_ptr),y
        rts

; The shared file picker behind F4 (delete) and F5 (load) -- shows a
; selectable list of every file actually on the disk, rather than
; asking for a filename to be typed blind. Caller passes a one-line
; title in A (low)/Y (high), matching print_msg's own convention,
; since that's what this passes it straight to. Handles its own
; screen backup/restore the same way do_directory does, since a
; multi-row list needs the same full-screen treatment a read-only
; directory view does, not the one-line status prompt prompt_filename
; itself uses. Returns the same way prompt_filename does: A (and
; filename_len) holds the chosen filename's length, 0 meaning
; cancelled (RUN/STOP, or an empty disk with nothing to pick).
pick_file:
        pha
        tya
        pha
        CLS
        pla
        tay
        pla
        jsr print_msg
        PRINT picker_instructions_msg

        jsr parse_directory_filenames
        lda file_count
        bne @has_files

        PRINT picker_empty_msg
        PRINT press_key_msg
        jsr wait_for_key
        jsr render_viewport
        jsr recompute_screen_ptr
        lda #$00
        sta filename_len
        rts

@has_files:
        jsr display_file_list
        lda #$00
        sta file_selection
        lda #$3e                    ; '>'
        ldx #$00
        jsr set_selector_char

@select_loop:
        jsr GETIN
        beq @select_loop
        cmp #$03                     ; RUN/STOP
        bne @not_stop
        jmp @cancelled
@not_stop:
        cmp #$0d                      ; RETURN
        beq @selected
        cmp #$11                       ; cursor down
        beq @move_down
        cmp #$91                        ; cursor up
        beq @move_up
        jmp @select_loop

@move_down:
        lda file_selection
        clc
        adc #$01
        cmp file_count
        bcs @select_loop                 ; already at the last entry

        lda #$20
        ldx file_selection
        jsr set_selector_char
        inc file_selection
        lda #$3e
        ldx file_selection
        jsr set_selector_char
        jmp @select_loop

@move_up:
        lda file_selection
        beq @select_loop                  ; already at the first entry

        lda #$20
        ldx file_selection
        jsr set_selector_char
        dec file_selection
        lda #$3e
        ldx file_selection
        jsr set_selector_char
        jmp @select_loop

@selected:
        lda file_selection
        MULT_16
        clc
        adc #<FILE_LIST_BUF
        sta copy_ptr
        lda #>FILE_LIST_BUF
        adc #$00
        sta copy_ptr+1

        ldy #$00
@copy_selected_loop:
        lda (copy_ptr),y
        sta filename_buf,y
        iny
        cpy #16
        bne @copy_selected_loop

        ldy #16                       ; trim trailing spaces back off,
@trim_loop:                             ; matching what a typed
        dey                              ; filename would look like
        lda filename_buf,y
        cmp #$20
        bne @trim_done
        cpy #$00
        beq @trim_done
        jmp @trim_loop
@trim_done:
        iny
        sty filename_len

        jsr render_viewport
        jsr recompute_screen_ptr
        lda filename_len
        rts

@cancelled:
        jsr render_viewport
        jsr recompute_screen_ptr
        lda #$00
        sta filename_len
        rts

do_directory:
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
        jsr render_viewport
        jsr recompute_screen_ptr
        jmp main_loop

dir_done:
        jsr CLRCHN
        lda #3
        jsr CLOSE
        jsr READST                 ; clear any stale EOF status left by
                                       ; the final CHRIN above happening
                                       ; to also be the listing's own
                                       ; last byte -- see
                                       ; parse_directory_filenames's own
                                       ; identical fix, and its comment,
                                       ; for the full reasoning; real
                                       ; READST's status otherwise
                                       ; persists until read again,
                                       ; regardless of what other
                                       ; operations (like a much later
                                       ; F5/F4) happen in between

        PRINT press_key_msg
        jsr wait_for_key

        jsr render_viewport
        jsr recompute_screen_ptr
        jmp main_loop

dirname: .text "$"
