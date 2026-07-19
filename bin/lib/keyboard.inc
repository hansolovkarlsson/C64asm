; keyboard.inc - named constants for every key on the C64 keyboard
; matrix, plus wait_any_key for a blocking "press any key" read.
;
; Provides:
;   KEY_<NAME>_COL    the column-select byte for CIA1_PRA
;   KEY_<NAME>_ROW    the row-bit mask for CIA1_PRB
;   KEY_<NAME>_CODE   a single-byte 0-63 "matrix code" identifying
;                     the same key, for use with wait_any_key below
;   wait_any_key      subroutine: blocks until any key is held, then
;                     returns its matrix code (0-63) in A
;
; Requires -- define this with `=` before this file's own .include
; line:
;   key_scratch   1-byte zero page -- wait_any_key's own scratch byte,
;                 never live across a call to anything else in this
;                 library, so it's always safe to alias with any other
;                 file's required zero-page byte(s)
;
; This file is a companion to input.inc's READ_KEY macro, not a
; replacement for it -- the two are meant to be .include'd together.
; READ_KEY needs a column-select byte and a row-bit mask; before this
; file existed, those had to be written out as raw %binary literals at
; every call site (see pong.asm/lander.asm's own READ_KEY calls for
; what that looks like), which is easy to get subtly wrong -- and, in
; fact, exactly this happened elsewhere in this project: an earlier
; version of demo.asm checked for a "Y (restart)" keypress using
; %11111011, %00000001, which is actually the matrix position of the
; "5" key, not Y (verified against the matrix table below and
; https://sta.c64.org/cbm64kbdlay.html, the standard published
; reference for this table). Using the named constants below instead
; of hand-written binary literals doesn't just save typing -- it's the
; difference between a mistake like that failing to assemble (an
; undefined symbol) and it silently compiling into a keypress check
; for the wrong key entirely. With this file:
;
;       READ_KEY KEY_Y_COL, KEY_Y_ROW
;       bne key_y_is_pressed
;
; The full matrix (verified against the same reference above; rows
; are the byte written to CIA1_PRA/$DC00, columns are the bit read
; back from CIA1_PRB/$DC01):
;
;              bit0     bit1     bit2     bit3     bit4      bit5    bit6     bit7
;   col0 $FE:  DEL      RETURN   CRSR_LR  F7       F1        F3      F5       CRSR_UD
;   col1 $FD:  3        W        A        4        Z         S       E        LSHIFT
;   col2 $FB:  5        R        D        6        C         F       T        X
;   col3 $F7:  7        Y        G        8        B         H       U        V
;   col4 $EF:  9        I        J        0        M         K       O        N
;   col5 $DF:  PLUS     P        L        MINUS    PERIOD    COLON   AT       COMMA
;   col6 $BF:  POUND    ASTERISK SEMICOLON HOME    RSHIFT    EQUALS  UPARROW  SLASH
;   col7 $7F:  1        LEFTARROW CTRL    2        SPACE     COMMODORE Q      RUNSTOP
;
; RESTORE isn't part of the matrix at all (it's wired to the NMI line
; directly) and has no constants here; SHIFT LOCK is mechanically tied
; to LSHIFT's own matrix position, so KEY_LSHIFT covers both.

.include "hardware.inc"

; --- column 0: %11111110 (DEL, RETURN, cursor keys, function keys) ---
KEY_DEL_COL       = %11111110
KEY_DEL_ROW       = %00000001
KEY_DEL_CODE      = 0
KEY_RETURN_COL    = %11111110
KEY_RETURN_ROW    = %00000010
KEY_RETURN_CODE   = 1
KEY_CRSR_LR_COL   = %11111110
KEY_CRSR_LR_ROW   = %00000100
KEY_CRSR_LR_CODE  = 2
KEY_F7_COL        = %11111110
KEY_F7_ROW        = %00001000
KEY_F7_CODE       = 3
KEY_F1_COL        = %11111110
KEY_F1_ROW        = %00010000
KEY_F1_CODE       = 4
KEY_F3_COL        = %11111110
KEY_F3_ROW        = %00100000
KEY_F3_CODE       = 5
KEY_F5_COL        = %11111110
KEY_F5_ROW        = %01000000
KEY_F5_CODE       = 6
KEY_CRSR_UD_COL   = %11111110
KEY_CRSR_UD_ROW   = %10000000
KEY_CRSR_UD_CODE  = 7
;
; --- column 1: %11111101 (3, W, A, 4, Z, S, E, left Shift) ---
KEY_3_COL         = %11111101
KEY_3_ROW         = %00000001
KEY_3_CODE        = 8
KEY_W_COL         = %11111101
KEY_W_ROW         = %00000010
KEY_W_CODE        = 9
KEY_A_COL         = %11111101
KEY_A_ROW         = %00000100
KEY_A_CODE        = 10
KEY_4_COL         = %11111101
KEY_4_ROW         = %00001000
KEY_4_CODE        = 11
KEY_Z_COL         = %11111101
KEY_Z_ROW         = %00010000
KEY_Z_CODE        = 12
KEY_S_COL         = %11111101
KEY_S_ROW         = %00100000
KEY_S_CODE        = 13
KEY_E_COL         = %11111101
KEY_E_ROW         = %01000000
KEY_E_CODE        = 14
KEY_LSHIFT_COL    = %11111101
KEY_LSHIFT_ROW    = %10000000
KEY_LSHIFT_CODE   = 15
;
; --- column 2: %11111011 (5, R, D, 6, C, F, T, X) ---
KEY_5_COL         = %11111011
KEY_5_ROW         = %00000001
KEY_5_CODE        = 16
KEY_R_COL         = %11111011
KEY_R_ROW         = %00000010
KEY_R_CODE        = 17
KEY_D_COL         = %11111011
KEY_D_ROW         = %00000100
KEY_D_CODE        = 18
KEY_6_COL         = %11111011
KEY_6_ROW         = %00001000
KEY_6_CODE        = 19
KEY_C_COL         = %11111011
KEY_C_ROW         = %00010000
KEY_C_CODE        = 20
KEY_F_COL         = %11111011
KEY_F_ROW         = %00100000
KEY_F_CODE        = 21
KEY_T_COL         = %11111011
KEY_T_ROW         = %01000000
KEY_T_CODE        = 22
KEY_X_COL         = %11111011
KEY_X_ROW         = %10000000
KEY_X_CODE        = 23
;
; --- column 3: %11110111 (7, Y, G, 8, B, H, U, V) ---
KEY_7_COL         = %11110111
KEY_7_ROW         = %00000001
KEY_7_CODE        = 24
KEY_Y_COL         = %11110111
KEY_Y_ROW         = %00000010
KEY_Y_CODE        = 25
KEY_G_COL         = %11110111
KEY_G_ROW         = %00000100
KEY_G_CODE        = 26
KEY_8_COL         = %11110111
KEY_8_ROW         = %00001000
KEY_8_CODE        = 27
KEY_B_COL         = %11110111
KEY_B_ROW         = %00010000
KEY_B_CODE        = 28
KEY_H_COL         = %11110111
KEY_H_ROW         = %00100000
KEY_H_CODE        = 29
KEY_U_COL         = %11110111
KEY_U_ROW         = %01000000
KEY_U_CODE        = 30
KEY_V_COL         = %11110111
KEY_V_ROW         = %10000000
KEY_V_CODE        = 31
;
; --- column 4: %11101111 (9, I, J, 0, M, K, O, N) ---
KEY_9_COL         = %11101111
KEY_9_ROW         = %00000001
KEY_9_CODE        = 32
KEY_I_COL         = %11101111
KEY_I_ROW         = %00000010
KEY_I_CODE        = 33
KEY_J_COL         = %11101111
KEY_J_ROW         = %00000100
KEY_J_CODE        = 34
KEY_0_COL         = %11101111
KEY_0_ROW         = %00001000
KEY_0_CODE        = 35
KEY_M_COL         = %11101111
KEY_M_ROW         = %00010000
KEY_M_CODE        = 36
KEY_K_COL         = %11101111
KEY_K_ROW         = %00100000
KEY_K_CODE        = 37
KEY_O_COL         = %11101111
KEY_O_ROW         = %01000000
KEY_O_CODE        = 38
KEY_N_COL         = %11101111
KEY_N_ROW         = %10000000
KEY_N_CODE        = 39
;
; --- column 5: %11011111 (+, P, L, -, ., :, @, ,) ---
KEY_PLUS_COL      = %11011111
KEY_PLUS_ROW      = %00000001
KEY_PLUS_CODE     = 40
KEY_P_COL         = %11011111
KEY_P_ROW         = %00000010
KEY_P_CODE        = 41
KEY_L_COL         = %11011111
KEY_L_ROW         = %00000100
KEY_L_CODE        = 42
KEY_MINUS_COL     = %11011111
KEY_MINUS_ROW     = %00001000
KEY_MINUS_CODE    = 43
KEY_PERIOD_COL    = %11011111
KEY_PERIOD_ROW    = %00010000
KEY_PERIOD_CODE   = 44
KEY_COLON_COL     = %11011111
KEY_COLON_ROW     = %00100000
KEY_COLON_CODE    = 45
KEY_AT_COL        = %11011111
KEY_AT_ROW        = %01000000
KEY_AT_CODE       = 46
KEY_COMMA_COL     = %11011111
KEY_COMMA_ROW     = %10000000
KEY_COMMA_CODE    = 47
;
; --- column 6: %10111111 (British pound, *, ;, HOME, right Shift, =, up-arrow, /) ---
KEY_POUND_COL     = %10111111
KEY_POUND_ROW     = %00000001
KEY_POUND_CODE    = 48
KEY_ASTERISK_COL  = %10111111
KEY_ASTERISK_ROW  = %00000010
KEY_ASTERISK_CODE = 49
KEY_SEMICOLON_COL = %10111111
KEY_SEMICOLON_ROW = %00000100
KEY_SEMICOLON_CODE= 50
KEY_HOME_COL      = %10111111
KEY_HOME_ROW      = %00001000
KEY_HOME_CODE     = 51
KEY_RSHIFT_COL    = %10111111
KEY_RSHIFT_ROW    = %00010000
KEY_RSHIFT_CODE   = 52
KEY_EQUALS_COL    = %10111111
KEY_EQUALS_ROW    = %00100000
KEY_EQUALS_CODE   = 53
KEY_UPARROW_COL   = %10111111
KEY_UPARROW_ROW   = %01000000
KEY_UPARROW_CODE  = 54
KEY_SLASH_COL     = %10111111
KEY_SLASH_ROW     = %10000000
KEY_SLASH_CODE    = 55
;
; --- column 7: %01111111 (1, left-arrow, CTRL, 2, SPACE, Commodore, Q, RUN/STOP) ---
KEY_1_COL         = %01111111
KEY_1_ROW         = %00000001
KEY_1_CODE        = 56
KEY_LEFTARROW_COL = %01111111
KEY_LEFTARROW_ROW = %00000010
KEY_LEFTARROW_CODE= 57
KEY_CTRL_COL      = %01111111
KEY_CTRL_ROW      = %00000100
KEY_CTRL_CODE     = 58
KEY_2_COL         = %01111111
KEY_2_ROW         = %00001000
KEY_2_CODE        = 59
KEY_SPACE_COL     = %01111111
KEY_SPACE_ROW     = %00010000
KEY_SPACE_CODE    = 60
KEY_COMMODORE_COL = %01111111
KEY_COMMODORE_ROW = %00100000
KEY_COMMODORE_CODE= 61
KEY_Q_COL         = %01111111
KEY_Q_ROW         = %01000000
KEY_Q_CODE        = 62
KEY_RUNSTOP_COL   = %01111111
KEY_RUNSTOP_ROW   = %10000000
KEY_RUNSTOP_CODE  = 63
;

; column-select bytes in column-index order (0-7), used by
; wait_any_key below to scan every column in turn.
key_col_table:
        .byte KEY_DEL_COL, KEY_3_COL, KEY_5_COL, KEY_7_COL
        .byte KEY_9_COL, KEY_PLUS_COL, KEY_POUND_COL, KEY_1_COL

; Blocks until any key on the keyboard is held down, then returns its
; matrix code (0-63) in A -- compare against the KEY_<NAME>_CODE
; constants above to find out which key it was, e.g.:
;
;       jsr wait_any_key
;       cmp #KEY_Y_CODE
;       beq handle_y
;
; Scans one column at a time, lowest column index first, and within a
; column, lowest row index first (see the matrix table in this file's
; header comment for what "column"/"row" mean here). If more than one
; key happens to be held at the exact moment of the scan, the
; lowest-numbered one by that order wins -- for the overwhelmingly
; common case of exactly one key held, this is simply that key's code;
; three or more keys held at once in certain row/column combinations
; is a well-known "ghosting" limitation of matrix keyboards in
; general, not something this routine (or any single CIA1 read) can
; fully resolve.
;
; Each individual column scan is wrapped in SEI/CLI, same as READ_KEY
; and for the same reason (see input.inc's header comment) -- but
; unlike READ_KEY, that's only around the brief write-then-read pair
; for ONE column, not the whole wait: interrupts stay enabled between
; columns and between retries, so the jiffy clock, cursor blink, and
; any IRQ-driven sound keep running normally for however long this
; ends up waiting.
wait_any_key:
        ldx #$00
@col_loop:
        sei
        lda #%11111111
        sta CIA1_DDRA
        lda key_col_table,x
        sta CIA1_PRA
        lda CIA1_PRB
        cli
        eor #$ff             ; active-HIGH: bit=1 means that key is held
        beq @next_col
        ldy #$00
@row_loop:
        lsr a
        bcs @found
        iny
        jmp @row_loop
@next_col:
        inx
        cpx #$08
        bne @col_loop
        ldx #$00
        jmp @col_loop
@found:
        txa
        asl a
        asl a
        asl a                 ; A = column_index * 8
        sta key_scratch
        tya
        clc
        adc key_scratch        ; A = column_index*8 + row_index
        rts
