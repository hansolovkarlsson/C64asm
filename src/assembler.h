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
 *
 * run_pass() also implements conditional assembly (.if/.elif/.else/
 * .endif and .ifdef/.ifndef/.else/.endif) -- deliberately as part of
 * this assembly-time pass, not as a preprocessing step the way macros
 * (macro.c) and .include (includes.c) are, so a condition can see real
 * constants and labels (like a PAL/NTSC flag defined with "="), not
 * just things known before any real parsing happens. The trade-off:
 * .if can gate whether instructions and data get assembled, but it
 * can't gate which .macro gets *defined* or which file gets
 * .include'd, since those are already fully resolved before .if is
 * ever evaluated.
 *
 * Two correctness requirements come directly from this being a
 * two-pass assembler, and are easy to get wrong:
 *
 *  1. ".if"/".elif" conditions must not reference a forward-declared
 *     symbol -- this is an unconditional error, checked the same way
 *     on both passes, *not* deferred to pass 2 the way other
 *     expressions' undefined-symbol checks are (see .org/.align in
 *     assembler.c). The reason: for an ordinary expression, pass 1
 *     guessing wrong about an undefined symbol's value only affects a
 *     byte *value*, silently corrected once pass 2 knows better. For
 *     "if", a wrong guess changes which lines exist at all -- which
 *     would desynchronize every address computed after it between the
 *     two passes. Requiring the condition to be fully known equally on
 *     both passes is what keeps that from ever happening.
 *
 *  2. ".ifdef"/".ifndef" must NOT simply check "is this symbol in the
 *     symbol table right now" (find_symbol(), symtab.h). symtab is
 *     never reset between pass 1 and pass 2 (pass 2 needs pass 1's
 *     complete table to resolve forward references) -- which means by
 *     the time pass 2 *starts*, the table already contains every
 *     symbol defined anywhere in the file, including ones that don't
 *     textually appear until later. A plain existence check would see
 *     "not defined" during pass 1 (walking forward, symbol not
 *     reached yet) but "defined" during pass 2, for the exact same
 *     .ifdef line -- the two passes would disagree about whether that
 *     line's block even exists. The fix: find_symbol_defined_before()
 *     (symtab.h) asks "was it defined strictly before this line's
 *     index" rather than "does it exist right now". Since both passes
 *     walk g_lines in the same order, that question has the same
 *     answer on both passes.
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
