/*
 * operand.h - works out which of the 6502's 13 addressing modes a
 * single instruction's operand text is asking for, purely from its
 * syntax (a leading '#', parentheses, a trailing ",X", and so on).
 *
 * This is the piece of the assembler that lets you write "LDA $10" and
 * have it become zero-page addressing, while "LDA $1000" becomes
 * absolute, and "LDA #$10" becomes immediate -- all without you having
 * to say which mode you mean anywhere except through how you wrote the
 * operand.
 */

#ifndef C64ASM_OPERAND_H
#define C64ASM_OPERAND_H

#include "opcodes.h"

/*
 * Determines the addressing mode of operand_in for the given mnemonic,
 * and evaluates it.
 *
 * mnemonic:    the instruction this operand belongs to (already
 *              uppercased by split_line()) -- needed both to look up
 *              which modes it actually supports (so e.g. an accidental
 *              "STA #$10" is rejected, since STA has no immediate mode)
 *              and because branch instructions always use relative
 *              addressing regardless of the operand's own syntax.
 * operand_in:  the operand text, e.g. "#$10", "($10),Y", "label,X".
 * pc:          current program counter, passed through to eval_expr().
 * line_no, raw: passed through to asm_error() for error messages.
 * val_out:     receives the evaluated operand value (the address or
 *              immediate value), except for M_IMP/M_ACC, which have no
 *              operand value at all.
 * undef_out:   receives whether val_out relied on a symbol that isn't
 *              defined yet -- see expr.h's eval_expr() for what this
 *              means and why it matters during pass 1.
 *
 * Returns the resolved Mode. An operand syntax the assembler doesn't
 * recognize, or one that names an addressing mode the instruction
 * doesn't actually support, is a fatal error via asm_error().
 */
Mode parse_operand(const char *mnemonic, const char *operand_in,
                    long pc, int line_no, const char *raw,
                    long *val_out, int *undef_out);

#endif
