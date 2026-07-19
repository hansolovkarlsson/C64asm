/*
 * opcodes.h - the 6502/6510 instruction set: addressing modes, the
 * opcode table itself, and lookups into it.
 *
 * This is the closest thing this codebase has to a "hardware spec" file
 * -- everything in here is just a direct transcription of publicly
 * documented 6502 facts (which mnemonic + addressing mode combinations
 * exist, and what single byte each one assembles to), not something
 * this project invented. If you want to double check any of it against
 * an independent source, any 6502 opcode reference table will do.
 */

#ifndef C64ASM_OPCODES_H
#define C64ASM_OPCODES_H

/*
 * The 6502 has 13 distinct addressing modes -- 13 different ways an
 * instruction's operand can specify where its data comes from. Not
 * every instruction supports every mode (e.g. STA has no immediate
 * mode, since "store the accumulator into a literal constant" doesn't
 * mean anything) -- see the OpcodeEntry.op[] array below, and
 * operand.c, which is where the assembler works out *which* of these a
 * given line of source is asking for.
 *
 * M_IMP  implied      no operand at all, e.g. RTS
 * M_ACC  accumulator  operates on A directly, e.g. ASL A
 * M_IMM  immediate    #expr        -- the operand IS the value
 * M_ZP   zero page    expr         -- one-byte address, $00-$FF
 * M_ZPX  zero page,X  expr,X
 * M_ZPY  zero page,Y  expr,Y
 * M_REL  relative     expr         -- branches only; encoded as a signed
 *                                     8-bit offset from the next instruction
 * M_ABS  absolute     expr         -- two-byte address
 * M_ABSX absolute,X   expr,X
 * M_ABSY absolute,Y   expr,Y
 * M_IND  indirect     (expr)       -- JMP only
 * M_INDX indexed indirect   (expr,X)
 * M_INDY indirect indexed   (expr),Y
 */
typedef enum {
    M_IMP = 0, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_REL,
    M_ABS, M_ABSX, M_ABSY, M_IND, M_INDX, M_INDY,
    M_COUNT
} Mode;

/* How many bytes an instruction occupies in memory for each addressing
 * mode -- 1 byte for the opcode itself, plus 0, 1, or 2 bytes of
 * operand. Indexed by Mode. This is what lets the assembler's first
 * pass (see assembler.c) figure out where every subsequent label ends
 * up, before any actual machine code exists yet. */
extern const int MODE_SIZE[M_COUNT];

/*
 * One entry per mnemonic: its name, and the opcode byte for each
 * addressing mode it supports (or -1 if it doesn't support that mode
 * at all). For example ADC supports 8 different modes with 8 different
 * opcode bytes; BRK supports only M_IMP, with every other slot -1.
 *
 * illegal[] parallels op[]: illegal[mode] is 1 if that (mnemonic, mode)
 * slot is an illegal/undocumented opcode -- one the NMOS 6502/6510
 * executes, but MOS never documented or supported -- and therefore
 * requires the '.cpu 6510x' directive before it can actually be
 * assembled. See init_opcodes() in opcodes.c and c64asm-reference.md's
 * "Illegal opcodes" section for the full explanation. Every entry from
 * the 56 documented mnemonics has illegal[] all zero; NOP is the one
 * mnemonic with a mix of both (M_IMP is a real, documented opcode;
 * its four extra illegal-opcode modes are not).
 */
typedef struct {
    char mnemonic[5];   /* 4 chars max (USBC, an illegal-opcode mnemonic,
                            is the only one longer than 3) + NUL */
    int  op[M_COUNT];
    int  illegal[M_COUNT];
} OpcodeEntry;

/*
 * Populates the opcode table with all 56 documented NMOS 6502/6510
 * mnemonics, plus the illegal/undocumented ones (see the "Illegal
 * opcodes" comment block in opcodes.c). Must be called once, before
 * anything else in this file (or split_line/parse_operand elsewhere)
 * is used -- the table starts empty.
 */
void init_opcodes(void);

/*
 * Looks up a mnemonic (case-insensitively, so "lda", "LDA", and "Lda"
 * all find the same entry) and returns a pointer to its OpcodeEntry, or
 * NULL if the name isn't a real 6502 mnemonic at all.
 *
 * This one function does double duty across the codebase: it's how
 * split_line() in lineparser.c recognizes "this token is an
 * instruction" while splitting a line into label/op/operand, and it's
 * how parse_operand() in operand.c and run_pass() in assembler.c look
 * up which opcode byte a specific addressing mode needs.
 */
OpcodeEntry *find_mnemonic(const char *name);

/* True if name is one of the 8 branch mnemonics (BCC, BCS, BEQ, BMI,
 * BNE, BPL, BVC, BVS). Branches are the one case where a single-operand
 * instruction always uses relative addressing regardless of what the
 * operand's syntax would normally imply -- see operand.c. */
int is_branch_mnemonic(const char *name);

/* True if tok (case-insensitively) is one of this assembler's
 * directives: .org, .byte, .db, .word, .dw, .text, .asc, .fill, .ds,
 * .res, .basic, .equ, .align, .cpu, .if, .elif, .else, .endif, .ifdef,
 * .ifndef. (The bare "*=" org form and "label = expr"
 * aren't matched here -- lineparser.c recognizes those directly by their
 * punctuation before a directive name would even come into it.) */
int is_directive(const char *tok);

#endif
