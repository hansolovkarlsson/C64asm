; adventure.asm - a small text adventure: a few rooms, a rock, a locked
; chest containing a key, and a locked door the key opens, leading to a
; treasure. Reads commands typed at the keyboard via the KERNAL's own
; line editor (CHRIN) rather than raw keyboard-matrix scanning -- this
; is deliberately the "safe" approach: CHRIN's line input, editing, and
; echo are all handled by well-tested KERNAL code, unlike the manual CIA
; register scanning used for the Pong demo (which had real, subtle bugs).
;
; All room descriptions and other printed messages use real mixed-case
; text (via '.charset lower' and text.inc's SET_LOWERCASE_CHARSET --
; see c64asm-reference.md's "Text and PETSCII" section) rather than the
; all-caps PETSCII most C64 programs default to. The player-typed verb
; and noun keywords this reads back against stay uppercase, since
; that's what CHRIN naturally returns from an unshifted keyboard
; regardless of which character set is active -- see this file's own
; comment right before the "printed strings" section below for why
; that split matters.
;
; The room/item/puzzle state machine below was validated in a Python
; simulation before being written here: the solution path (forest ->
; cave entrance -> back for the key -> cottage -> chest -> key -> cave
; entrance -> door -> dark cave -> treasure) was walked start to finish,
; and failure cases (wrong room, missing key, already-open chest, taking
; something not present) were all checked to produce sensible messages.
;
; Uses lib/text.inc (print_msg/PRINT, str_equal) and lib/input.inc
; (read_line, extract_word) rather than defining its own copies of
; these -- they were originally written here, then generalized into
; the library (see lib-reference.md's "worked examples" for the
; smaller, focused demos this project now builds alongside the
; original kitchen-sink demo.asm). Everything specific to *this*
; particular adventure -- the room/item state machine, the verb table,
; the two-word "verb noun" grammar built from extract_word, and every
; printed message -- stays here, since none of that is reusable across
; other programs the way the underlying string/input primitives are.
;
; room_exits (see that table's own comment, further down) uses
; '.struct' (c64asm-reference.md section 9) for named field access --
; room_exits+Exits.north instead of a bare offset number, or the four
; entirely separate exit_north/exit_south/exit_east/exit_west arrays
; this table used to be before '.struct' existed.

        .basic start   ; auto-emits `jmp start` right after the loader
                          ; stub -- needed because the .include lines
                          ; below (text.inc/input.inc) emit real code,
                          ; which would otherwise run first

; Zero-page pointers. Each pair is shared by two routines that are never
; active at the same time (e.g. str_ptr is only needed once tokenizing,
; which happens first, is completely finished), keeping total zero-page
; use within the $02-$06 / $FB-$FE range that's widely documented as
; safe for machine code that isn't returning to BASIC -- see
; c64-memory-reference.md's zero-page notes, extended here since this
; program needs a few more pointer pairs than the earlier demos did.
;
; str_ptr, cmp_ptr, and kw_ptr are required by lib/text.inc
; (print_msg and str_equal respectively); word_dest_ptr is required by
; lib/input.inc (extract_word). See both files' header comments for
; why sharing addresses between them, as done here, is safe.
str_ptr       = $fb   ; print_msg's string pointer
word_dest_ptr = $fb   ; extract_word's destination pointer (tokenizing
                        ; finishes completely before anything is printed)
cmp_ptr       = $fd   ; str_equal's "left" side of a comparison
kw_ptr        = $02   ; str_equal's "right" side (the candidate keyword)
handler_vec   = $02   ; indirect-call target (only set after str_equal
                        ; has already returned, so this never collides
                        ; with kw_ptr being in use)

        .include "lib/text.inc"
        .include "lib/input.inc"

; Room numbers
CLEARING      = 0
FOREST        = 1
COTTAGE       = 2
CAVE_ENTRANCE = 3
DARK_CAVE     = 4

; Item numbers
ROCK     = 0
KEY      = 1
TREASURE = 2

; item_location sentinel values (rooms themselves are 0-4)
LOC_NONE = 255   ; not present anywhere yet (the key, inside the unopened chest)
LOC_INV  = 254   ; carried in the player's inventory

start:
        SET_LOWERCASE_CHARSET     ; switch the C64's actual character set
                                     ; to lowercase/uppercase -- without
                                     ; this, every message below (assembled
                                     ; under .charset lower, further down
                                     ; this file) would still display in
                                     ; uppercase, since .charset only
                                     ; controls assembled bytes, not this
                                     ; runtime hardware state; see
                                     ; c64asm-reference.md's "Text and
                                     ; PETSCII" section
        DISABLE_CHARSET_SWITCH     ; and don't let CBM+SHIFT undo that --
                                     ; this program has one consistent
                                     ; character set for its whole run
        lda #<msg_welcome1
        ldy #>msg_welcome1
        jsr print_msg
        lda #<msg_welcome2
        ldy #>msg_welcome2
        jsr print_msg
        jsr describe_room

main_loop:
        lda #<prompt_str
        ldy #>prompt_str
        jsr print_msg
        jsr read_line
        NEWLINE     ; without this, the response prints running onto
                      ; the same screen line as whatever was just typed
                      ; -- see lib/text.inc's NEWLINE for the general
                      ; note on why this needs to be explicit
        jsr tokenize
        jsr dispatch
        jmp main_loop

; --- input ---

; Splits input_buf (filled by read_line, lib/input.inc) into up to two
; space-separated words: verb_buf and noun_buf (either may end up
; empty if the input had fewer words). The two-word "verb noun"
; grammar is this adventure's own policy -- lib/input.inc's
; extract_word just pulls one word at a time; how many words a
; program wants, and what to call them, is up to the program.
tokenize:
        ldx #$00
        lda #<verb_buf
        ldy #>verb_buf
        jsr extract_word
        lda #<noun_buf
        ldy #>noun_buf
        jsr extract_word
        rts

; --- string comparison ---
; (str_equal itself now lives in lib/text.inc)

; --- command dispatch ---

; A small stub enabling an "indirect JSR": 6502 has no such addressing
; mode directly, but JSR-to-a-stub-that-JMP-(indirect)s works, because
; JMP doesn't touch the stack -- the handler's eventual RTS pops the
; return address the original JSR pushed, landing right after the "jsr
; call_indirect" in dispatch, not inside this stub.
call_indirect:
        jmp (handler_vec)

; Matches verb_buf against every keyword in verb_table and, on a match,
; sets cmp_ptr to noun_buf (so the handler can immediately compare
; against it without any setup of its own) and calls the matched
; handler.
dispatch:
        lda #<verb_buf
        sta cmp_ptr
        lda #>verb_buf
        sta cmp_ptr+1
        ldx #$00
dispatch_loop:
        lda verb_table,x
        ora verb_table+1,x
        beq dispatch_not_found       ; both bytes zero -> end-of-table marker
        lda verb_table,x
        ldy verb_table+1,x
        jsr str_equal
        cmp #1
        bne dispatch_next
        lda #<noun_buf
        sta cmp_ptr
        lda #>noun_buf
        sta cmp_ptr+1
        lda verb_table+2,x
        sta handler_vec
        lda verb_table+3,x
        sta handler_vec+1
        jsr call_indirect
        rts
dispatch_next:
        txa
        clc
        adc #4
        tax
        jmp dispatch_loop
dispatch_not_found:
        lda #<msg_dont_understand
        ldy #>msg_dont_understand
        jsr print_msg
        rts

; --- room description ---

; Prints the current room's base description, then any items lying in
; it, then room-specific chest/door status (rooms 2 and 3 only).
describe_room:
        ldx current_room
        txa
        asl a
        tay
        lda room_desc_table,y
        pha
        lda room_desc_table+1,y
        tay
        pla
        jsr print_msg

        ldx #$00
describe_item_loop:
        lda item_location,x
        cmp current_room
        bne describe_item_next
        txa
        asl a
        tay
        lda item_here_table,y
        pha
        lda item_here_table+1,y
        tay
        pla
        jsr print_msg
describe_item_next:
        inx
        cpx #3
        bne describe_item_loop

        lda current_room
        cmp #COTTAGE
        bne skip_chest_desc
        lda chest_open
        bne chest_is_open
        lda #<chest_closed_msg
        ldy #>chest_closed_msg
        jsr print_msg
        jmp skip_chest_desc
chest_is_open:
        lda item_location+KEY
        cmp #COTTAGE
        beq skip_chest_desc          ; key still visible -- item loop above already announced it
        lda #<chest_open_empty_msg
        ldy #>chest_open_empty_msg
        jsr print_msg
skip_chest_desc:

        lda current_room
        cmp #CAVE_ENTRANCE
        bne skip_door_desc
        lda door_unlocked
        bne door_is_open
        lda #<door_locked_msg
        ldy #>door_locked_msg
        jsr print_msg
        jmp skip_door_desc
door_is_open:
        lda #<door_unlocked_msg
        ldy #>door_unlocked_msg
        jsr print_msg
skip_door_desc:
        rts

; --- command handlers ---

handle_look:
        jsr describe_room
        rts

handle_go:
        lda noun_buf
        bne handle_go_check_words
        lda #<msg_bad_direction
        ldy #>msg_bad_direction
        jsr print_msg
        rts
handle_go_check_words:
        lda #<kw_north
        ldy #>kw_north
        jsr str_equal
        cmp #1
        beq handle_go_north
        lda #<kw_n
        ldy #>kw_n
        jsr str_equal
        cmp #1
        beq handle_go_north
        lda #<kw_south
        ldy #>kw_south
        jsr str_equal
        cmp #1
        beq handle_go_south
        lda #<kw_s
        ldy #>kw_s
        jsr str_equal
        cmp #1
        beq handle_go_south
        lda #<kw_east
        ldy #>kw_east
        jsr str_equal
        cmp #1
        beq handle_go_east
        lda #<kw_e
        ldy #>kw_e
        jsr str_equal
        cmp #1
        beq handle_go_east
        lda #<kw_west
        ldy #>kw_west
        jsr str_equal
        cmp #1
        beq handle_go_west
        lda #<kw_w
        ldy #>kw_w
        jsr str_equal
        cmp #1
        beq handle_go_west
        lda #<msg_bad_direction
        ldy #>msg_bad_direction
        jsr print_msg
        rts

handle_go_north:
        jsr compute_room_exits_offset
        lda room_exits+Exits.north,x
        jmp do_move_check
handle_go_south:
        jsr compute_room_exits_offset
        lda room_exits+Exits.south,x
        jmp do_move_check
handle_go_east:
        jsr compute_room_exits_offset
        lda room_exits+Exits.east,x
        jmp do_move_check
handle_go_west:
        jsr compute_room_exits_offset
        lda room_exits+Exits.west,x
        jmp do_move_check

; Computes X := current_room * Exits.size, for indexing into
; room_exits (see that table's own comment, further down, for the
; layout this relies on).
.assert Exits.size == 4, "compute_room_exits_offset assumes 4 fields (two left shifts below) -- update both if Exits ever changes shape"
compute_room_exits_offset:
        lda current_room
        asl a
        asl a                  ; A := current_room * 4 -- relies on
                                  ; Exits.size being exactly 4 (two
                                  ; left shifts); if the struct ever
                                  ; gains or loses a field, this needs
                                  ; updating to match, since there's no
                                  ; way to shift by a symbolic amount
                                  ; on the 6502 -- the .assert just
                                  ; above turns "silently wrong room
                                  ; navigation at runtime" into a clear
                                  ; assembly-time error if that's ever
                                  ; forgotten
        tax
        rts

do_move_check:
        cmp #$ff
        bne move_ok
        lda #<msg_cant_go
        ldy #>msg_cant_go
        jsr print_msg
        rts
move_ok:
        sta current_room
        jsr describe_room
        rts

handle_take:
        lda noun_buf
        bne take_check_words
        lda #<msg_take_what
        ldy #>msg_take_what
        jsr print_msg
        rts
take_check_words:
        lda #<kw_rock
        ldy #>kw_rock
        jsr str_equal
        cmp #1
        bne take_try_key
        ldx #ROCK
        jmp take_item
take_try_key:
        lda #<kw_key
        ldy #>kw_key
        jsr str_equal
        cmp #1
        bne take_try_treasure
        ldx #KEY
        jmp take_item
take_try_treasure:
        lda #<kw_treasure
        ldy #>kw_treasure
        jsr str_equal
        cmp #1
        bne take_unknown
        ldx #TREASURE
        jmp take_item
take_unknown:
        lda #<msg_take_fail
        ldy #>msg_take_fail
        jsr print_msg
        rts

take_item:
        lda item_location,x
        cmp current_room
        beq take_ok
        lda #<msg_take_fail
        ldy #>msg_take_fail
        jsr print_msg
        rts
take_ok:
        lda #LOC_INV
        sta item_location,x
        txa
        asl a
        tay
        lda item_take_msg_table,y
        pha
        lda item_take_msg_table+1,y
        tay
        pla
        jsr print_msg
        rts

handle_open:
        lda noun_buf
        bne open_check_words
        lda #<msg_open_what
        ldy #>msg_open_what
        jsr print_msg
        rts
open_check_words:
        lda #<kw_chest
        ldy #>kw_chest
        jsr str_equal
        cmp #1
        beq handle_open_chest
        lda #<kw_door
        ldy #>kw_door
        jsr str_equal
        cmp #1
        beq handle_open_door
        lda #<msg_open_nothing
        ldy #>msg_open_nothing
        jsr print_msg
        rts

handle_open_chest:
        lda current_room
        cmp #COTTAGE
        beq open_chest_here
        lda #<msg_no_chest_here
        ldy #>msg_no_chest_here
        jsr print_msg
        rts
open_chest_here:
        lda chest_open
        beq chest_not_open_yet
        lda #<msg_chest_already_open
        ldy #>msg_chest_already_open
        jsr print_msg
        rts
chest_not_open_yet:
        lda #1
        sta chest_open
        lda #COTTAGE
        sta item_location+KEY        ; reveal the key
        lda #<msg_chest_opened
        ldy #>msg_chest_opened
        jsr print_msg
        rts

handle_open_door:
        lda current_room
        cmp #CAVE_ENTRANCE
        beq open_door_here
        lda #<msg_no_door_here
        ldy #>msg_no_door_here
        jsr print_msg
        rts
open_door_here:
        lda door_unlocked
        beq door_not_open_yet
        lda #<msg_door_already_open
        ldy #>msg_door_already_open
        jsr print_msg
        rts
door_not_open_yet:
        lda item_location+KEY
        cmp #LOC_INV
        beq door_have_key
        lda #<msg_door_locked_need_key
        ldy #>msg_door_locked_need_key
        jsr print_msg
        rts
door_have_key:
        lda #1
        sta door_unlocked
        lda #DARK_CAVE
        sta room_exits + CAVE_ENTRANCE*Exits.size + Exits.north  ; the
                                                                     ; connection now exists
        lda #<msg_door_unlocked
        ldy #>msg_door_unlocked
        jsr print_msg
        rts

handle_inventory:
        ldx #$00
        lda #$00
        sta inv_found_flag
inv_check_loop:
        lda item_location,x
        cmp #LOC_INV
        bne inv_check_next
        lda #1
        sta inv_found_flag
inv_check_next:
        inx
        cpx #3
        bne inv_check_loop

        lda inv_found_flag
        bne inv_nonempty
        lda #<msg_inventory_empty
        ldy #>msg_inventory_empty
        jsr print_msg
        rts

inv_nonempty:
        lda #<msg_inventory_header
        ldy #>msg_inventory_header
        jsr print_msg
        ldx #$00
inv_print_loop:
        lda item_location,x
        cmp #LOC_INV
        bne inv_print_next
        txa
        asl a
        tay
        lda item_name_table,y
        pha
        lda item_name_table+1,y
        tay
        pla
        jsr print_msg
inv_print_next:
        inx
        cpx #3
        bne inv_print_loop
        rts

handle_help:
        lda #<msg_help1
        ldy #>msg_help1
        jsr print_msg
        lda #<msg_help2
        ldy #>msg_help2
        jsr print_msg
        lda #<msg_help3
        ldy #>msg_help3
        jsr print_msg
        lda #<msg_help4
        ldy #>msg_help4
        jsr print_msg
        lda #<msg_help5
        ldy #>msg_help5
        jsr print_msg
        lda #<msg_help6
        ldy #>msg_help6
        jsr print_msg
        rts

; --- mutable game state ---
; (plain RAM, not zero page -- nothing here needs indirect addressing,
; so there's no reason to spend scarce zero-page bytes on it)

current_room:  .byte CLEARING
chest_open:    .byte 0
door_unlocked: .byte 0
inv_found_flag: .byte 0

; indexed by item number (ROCK, KEY, TREASURE): current room, or
; LOC_NONE / LOC_INV
item_location:
        .byte CLEARING, LOC_NONE, DARK_CAVE

; Each room's exits, one struct-sized (4-byte) record per room --
; replaces what used to be four separate parallel arrays (exit_north,
; exit_south, exit_east, exit_west, one per direction instead of one
; per room). $ff = no exit that way. room_exits + CAVE_ENTRANCE*Exits.size
; + Exits.north starts as $ff (door locked) and is patched to DARK_CAVE
; once unlocked -- see the door-unlock handler above.
.struct Exits
        .byte north, south, east, west
.endstruct
;                    north    south          east            west
room_exits:
        .byte        FOREST,  $ff,           COTTAGE,        $ff             ; CLEARING
        .byte        $ff,     CLEARING,      CAVE_ENTRANCE,  $ff             ; FOREST
        .byte        $ff,     $ff,           $ff,            CLEARING        ; COTTAGE
        .byte        $ff,     $ff,           $ff,            FOREST          ; CAVE_ENTRANCE
        .byte        $ff,     CAVE_ENTRANCE, $ff,            $ff             ; DARK_CAVE

; --- input buffers ---
; (input_buf itself now lives in lib/input.inc, filled by read_line)
verb_buf:  .fill 16, 0
noun_buf:  .fill 16, 0

; --- verb dispatch table: keyword pointer, handler pointer ---
verb_table:
        .word kw_look,      handle_look
        .word kw_go,        handle_go
        .word kw_north,     handle_go_north
        .word kw_n,         handle_go_north
        .word kw_south,     handle_go_south
        .word kw_s,         handle_go_south
        .word kw_east,      handle_go_east
        .word kw_e,         handle_go_east
        .word kw_west,      handle_go_west
        .word kw_w,         handle_go_west
        .word kw_take,      handle_take
        .word kw_get,       handle_take
        .word kw_open,      handle_open
        .word kw_inventory, handle_inventory
        .word kw_i,         handle_inventory
        .word kw_help,      handle_help
        .word $0000, $0000

; --- lookup tables (indexed by room*2 or item*2) ---
room_desc_table:
        .word room0_desc, room1_desc, room2_desc, room3_desc, room4_desc
item_here_table:
        .word rock_here_msg, key_here_msg, treasure_here_msg
item_take_msg_table:
        .word rock_take_msg, key_take_msg, treasure_take_msg
item_name_table:
        .word rock_name_msg, key_name_msg, treasure_name_msg

; --- keyword strings (compared against, never printed) ---
kw_look:      .text "LOOK"
        .byte 0
kw_go:        .text "GO"
        .byte 0
kw_north:     .text "NORTH"
        .byte 0
kw_n:         .text "N"
        .byte 0
kw_south:     .text "SOUTH"
        .byte 0
kw_s:         .text "S"
        .byte 0
kw_east:      .text "EAST"
        .byte 0
kw_e:         .text "E"
        .byte 0
kw_west:      .text "WEST"
        .byte 0
kw_w:         .text "W"
        .byte 0
kw_take:      .text "TAKE"
        .byte 0
kw_get:       .text "GET"
        .byte 0
kw_open:      .text "OPEN"
        .byte 0
kw_inventory: .text "INVENTORY"
        .byte 0
kw_i:         .text "I"
        .byte 0
kw_help:      .text "HELP"
        .byte 0
kw_rock:      .text "ROCK"
        .byte 0
kw_key:       .text "KEY"
        .byte 0
kw_treasure:  .text "TREASURE"
        .byte 0
kw_chest:     .text "CHEST"
        .byte 0
kw_door:      .text "DOOR"
        .byte 0

; --- printed strings ---
; Everything below is assembled under .charset lower: uppercase source
; letters still display correctly as uppercase regardless of which
; character set is active (see the mixing caveat in
; c64asm-reference.md's "Text and PETSCII" section), while lowercase
; source letters display as genuine lowercase now that start: has
; switched the runtime character set -- see SET_LOWERCASE_CHARSET
; above. The keyword strings above this point stay under the default
; .charset upper, unchanged: they're compared against typed keyboard
; input, not printed, and keyboard input's PETSCII bytes don't depend
; on which character set happens to be active (only which physical key
; and shift state do) -- see the "keyboard PETSCII matches .text
; PETSCII" check near the top of test_adventure.py for why that
; comparison staying correct matters.
        .charset lower

prompt_str:            .text "> "
        .byte 0

msg_welcome1:  .text "=== The Forgotten Cottage ==="
        .byte $0d,0
msg_welcome2:  .text "A short text adventure. Type HELP for commands."
        .byte $0d,0

room0_desc: .text "You are standing in a sunny clearing. A path leads north into a forest, and a small cottage lies to the east."
        .byte $0d,0
room1_desc: .text "You are in a dark forest. Tall trees surround you. The clearing is south, and a rocky path leads east."
        .byte $0d,0
room2_desc: .text "You are inside a small, dusty cottage. The clearing is west."
        .byte $0d,0
room3_desc: .text "You stand before the mouth of a dark cave. The forest is west."
        .byte $0d,0
room4_desc: .text "You are in a damp, dark cave. Something glints in the shadows."
        .byte $0d,0

rock_here_msg:     .text "There is a rock here."
        .byte $0d,0
key_here_msg:      .text "There is a key here."
        .byte $0d,0
treasure_here_msg: .text "Something glitters here - it's a treasure!"
        .byte $0d,0

rock_take_msg:     .text "You pick up the rock."
        .byte $0d,0
key_take_msg:      .text "You take the key."
        .byte $0d,0
treasure_take_msg: .text "You take the treasure. Sunlight glints off the gold - you win!"
        .byte $0d,0

rock_name_msg:     .text "A rock"
        .byte $0d,0
key_name_msg:      .text "A key"
        .byte $0d,0
treasure_name_msg: .text "The treasure"
        .byte $0d,0

chest_closed_msg:     .text "There is a closed wooden chest here."
        .byte $0d,0
chest_open_empty_msg: .text "The wooden chest is open and empty."
        .byte $0d,0
msg_chest_already_open: .text "It's already open."
        .byte $0d,0
msg_chest_opened:        .text "You open the chest. Inside is a key!"
        .byte $0d,0
msg_no_chest_here:       .text "There is no chest here."
        .byte $0d,0

door_locked_msg:   .text "A locked wooden door blocks the way north."
        .byte $0d,0
door_unlocked_msg: .text "An open door leads north into darkness."
        .byte $0d,0
msg_door_already_open:    .text "The door is already open."
        .byte $0d,0
msg_door_locked_need_key: .text "It's locked. You need a key."
        .byte $0d,0
msg_door_unlocked:        .text "You unlock the door. It swings open."
        .byte $0d,0
msg_no_door_here:         .text "There is no door here."
        .byte $0d,0

msg_take_fail:        .text "You don't see that here."
        .byte $0d,0
msg_take_what:         .text "Take what?"
        .byte $0d,0
msg_open_nothing:       .text "You can't open that."
        .byte $0d,0
msg_open_what:            .text "Open what?"
        .byte $0d,0
msg_dont_understand:       .text "I don't understand that."
        .byte $0d,0
msg_cant_go:                 .text "You can't go that way."
        .byte $0d,0
msg_bad_direction:             .text "Go where?"
        .byte $0d,0
msg_inventory_empty:             .text "You aren't carrying anything."
        .byte $0d,0
msg_inventory_header:              .text "You are carrying:"
        .byte $0d,0

msg_help1: .text "Commands:"
        .byte $0d,0
msg_help2: .text "LOOK - describe your surroundings"
        .byte $0d,0
msg_help3: .text "GO <direction>, or just N/S/E/W"
        .byte $0d,0
msg_help4: .text "TAKE <item>"
        .byte $0d,0
msg_help5: .text "OPEN <chest/door>"
        .byte $0d,0
msg_help6: .text "INVENTORY (or I), HELP"
        .byte $0d,0
