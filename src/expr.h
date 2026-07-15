/*
 * expr.h - the expression evaluator: turns text like "SCREEN + 40*ROW"
 * into a number.
 *
 * This is a small hand-written recursive-descent parser and evaluator
 * combined into one pass -- there's no separate "build a tree, then
 * walk the tree" step, which is common in bigger compilers/assemblers.
 * For an expression grammar this simple (numbers, symbols, +, -, *, /,
 * parentheses, and two unary operators), evaluating directly while
 * parsing is a completely standard, well-understood simplification, and
 * it's the easiest way to see how recursive descent works if you
 * haven't written one before: each grammar rule becomes one function,
 * and functions call each other in exactly the pattern the grammar's
 * precedence implies. See the comment above eval_expr() in expr.c for
 * the actual grammar and how the call structure encodes operator
 * precedence.
 */

#ifndef C64ASM_EXPR_H
#define C64ASM_EXPR_H

/*
 * Evaluates a single expression and returns its value.
 *
 * text:          the expression text, e.g. "$0400 + ROW*40 + COL".
 * pc:             the current program counter -- needed because '*'
 *                 used as a whole term (not between two other terms)
 *                 means "the address of this instruction", not
 *                 multiplication. See the comment on parse_atom() in
 *                 expr.c for how the parser tells the two apart.
 * line_no:        passed through to asm_error() if the expression is
 *                 malformed.
 * undefined_out:  if non-NULL, receives 1 if the expression referenced
 *                 at least one symbol that isn't defined (yet, or at
 *                 all), or 0 if every symbol resolved. This is how the
 *                 assembler supports forward references: pass 1 can
 *                 evaluate an expression that mentions a label defined
 *                 later in the file, get *some* placeholder value back
 *                 (0, for the undefined part), and know via this flag
 *                 not to trust that value for anything meaningful yet
 *                 -- only pass 2, after every label's real address is
 *                 known, treats an undefined symbol as an actual error.
 *
 * Calling this with a syntactically invalid expression (mismatched
 * parentheses, a stray character, trailing garbage after a complete
 * expression) is a fatal error via asm_error() -- there's no "invalid
 * expression" return value to check for separately, unlike the
 * "undefined symbol" case above, which is a normal, expected situation
 * during pass 1.
 */
long eval_expr(const char *text, long pc, int line_no, int *undefined_out);

#endif
