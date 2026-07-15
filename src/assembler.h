/*
 * assembler.h - the core two-pass assembly loop: walks every parsed
 * source line (see lineparser.h) in order, and either builds the symbol
 * table (pass 1) or produces real machine code and listing entries
 * (pass 2).
 *
 * Why two passes: a label can be *used* before it's *defined* --
 * "JMP loop" written above "loop:" is completely normal 6502 assembly.
 * To assemble "JMP loop" correctly, the assembler needs to already know
 * loop's address, which it can't, the first time through the file. The
 * standard solution (used by essentially every assembler for every
 * architecture) is to make two passes over the source:
 *
 *   Pass 1: walk the whole file just to work out where everything ends
 *           up. Every label gets defined with its real address, by
 *           tracking the program counter line by line, but no machine
 *           code is actually produced -- a forward reference like
 *           "loop" being undefined *at the time it's evaluated* is
 *           fine and expected here, and just means "this line's exact
 *           value doesn't matter yet, only its final byte size does."
 *   Pass 2: walk the whole file *again*, now with a complete symbol
 *           table already built. Every reference resolves correctly
 *           this time (loop's address is already known), so real
 *           opcode bytes and operand values can be emitted, and it's
 *           now a genuine error for anything to still be undefined.
 *
 * run_pass() is written to do both jobs from the same code, controlled
 * by the pass_no argument -- see its comment below for how the two
 * passes stay consistent with each other (in particular, why an
 * instruction must come out the *same size* in both passes, and what
 * that implies about this assembler's zero-page-vs-absolute heuristic).
 */

#ifndef C64ASM_ASSEMBLER_H
#define C64ASM_ASSEMBLER_H

#include "bytebuf.h"

/*
 * Runs one full pass over g_lines (see lineparser.h) from top to
 * bottom.
 *
 * pass_no: 1 or 2, as described above.
 * output:  where assembled bytes are appended, pass 2 only. Pass 1
 *          still needs a valid ByteBuf to pass in (bb_init() it first)
 *          even though nothing is written to it, because .org's forward-
 *          gap-padding logic (see the comment in assembler.c) needs to
 *          read its current length -- harmlessly always zero during
 *          pass 1, since nothing pushes to it then.
 * origin_out: receives the program's load address (the address in
 *          effect at the first .org / .basic / "*=" encountered, or
 *          $0801 if the source never sets one).
 *
 * Returns the final program counter value after the last line --  i.e.
 * one past the highest address anything was assembled to.
 */
long run_pass(int pass_no, ByteBuf *output, long *origin_out);

#endif
