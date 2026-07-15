/*
 * lineparser.h - turns one raw line of source text into a structured
 * SourceLine: an optional label, an optional mnemonic/directive, and
 * an operand string. This is the assembler's "line grammar" -- separate
 * from expr.c's "expression grammar" and operand.c's "addressing mode
 * grammar", each of which only ever sees the small piece of text this
 * stage has already carved out for it.
 */

#ifndef C64ASM_LINEPARSER_H
#define C64ASM_LINEPARSER_H

#include "common.h"

typedef struct {
    int line_no;
    char raw[MAX_LINE_LEN];       /* the original, unmodified line, kept
                                      only for error messages and listings */
    int has_label;
    char label[MAX_IDENT];
    int has_op;
    char op[MAX_IDENT];           /* an uppercased mnemonic, a lowercased
                                      directive, or the literal string "="
                                      for a constant assignment */
    char operand[MAX_LINE_LEN];
} SourceLine;

/* The whole source file, one entry per line, populated by load_source()
 * in fileio.c (which just calls split_line() below in a loop) and then
 * read twice from top to bottom by run_pass() in assembler.c -- once
 * per assembly pass. */
extern SourceLine *g_lines;
extern int g_line_count;

/*
 * Parses one raw source line into `out`. Recognizes, in order:
 *   - blank lines (out->has_label and has_op both end up 0)
 *   - "* = expr" or "*= expr"                (org directive)
 *   - "identifier = expr"                     (constant assignment)
 *   - "label:" or "label" at the start of a line, optionally followed
 *     by a mnemonic or directive and its operand
 *   - a bare mnemonic or directive with no label
 *
 * Distinguishing "label" from "mnemonic/directive with no label" when
 * there's no colon is genuinely ambiguous from punctuation alone (both
 * are just "a word at the start of the line") -- this function resolves
 * it by checking whether that first word is a *known* mnemonic or
 * directive (via find_mnemonic()/is_directive() from opcodes.h): if it
 * is, there's no label; if it isn't, it must be a label, and whatever
 * follows it is checked the same way.
 *
 * raw_line: one line of source, as fgets() would hand it over -- still
 *           including its trailing newline, if any.
 * line_no:  1-based line number, used only for error messages.
 * out:      caller-owned SourceLine to fill in.
 *
 * An input this function can't make sense of (an unrecognized mnemonic
 * or directive) is a fatal error via asm_error(), not a return code to
 * check.
 */
void split_line(const char *raw_line, int line_no, SourceLine *out);

#endif
