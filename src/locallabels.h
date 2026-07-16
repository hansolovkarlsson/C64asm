/*
 * locallabels.h - @-prefixed local labels, scoped to a region of
 * ordinary code or a single macro expansion.
 *
 * A label name starting with '@' (e.g. "@loop") is textually rewritten
 * to a scope-specific global name (e.g. "__local5_loop") before
 * split_line() (lineparser.h) ever sees it -- so as far as the rest of
 * the assembler (symbol table, expression evaluator, everything) is
 * concerned, it's just an ordinary label; all the "local" behavior
 * lives entirely in this module.
 *
 * A new scope begins:
 *   - each time an ordinary ("identifier:") global label is defined,
 *     and
 *   - each time a macro invocation (macro.h) begins expanding, with the
 *     previous scope restored once that invocation's body is fully
 *     processed.
 *
 * The second rule is what makes a macro's own @-labels distinct on
 * every separate invocation, automatically -- no suffix parameter or
 * other caller-side bookkeeping required, unlike before this module
 * existed. See macro.c for where locallabels_push_scope() and
 * locallabels_pop_scope() are called, around a macro's body expansion.
 *
 * A reference to an @-label from outside the scope it was defined in
 * mangles to a name that was never actually defined, and so becomes an
 * ordinary "undefined symbol" error at assembly time (see assembler.c)
 * -- scope violations are caught by the existing machinery for free,
 * without any dedicated scope-checking code in this module.
 *
 * Deliberate limitations (documented in c64asm-reference.md, not
 * oversights):
 *   - A new scope is only recognized from the explicit "identifier:"
 *     form -- a bare label with no colon doesn't start a new scope.
 *   - '@' inside a double-quoted string (e.g. .text "user@example.com")
 *     is left alone, not mangled.
 */

#ifndef C64ASM_LOCALLABELS_H
#define C64ASM_LOCALLABELS_H

#include <stddef.h>

/*
 * Call once per genuinely final source line, with its comment-stripped,
 * trimmed text, *before* mangling that same line with
 * locallabels_mangle(). If the line defines an ordinary global label
 * ("identifier:", not starting with '@'), advances to a fresh scope;
 * otherwise does nothing.
 */
void locallabels_maybe_new_scope(const char *trimmed_line);

/*
 * Rewrites every @name in `text` to a name specific to the current
 * scope, writing up to outsz-1 bytes (plus a null terminator) into
 * `out`. Leaves anything inside a double-quoted string untouched.
 */
void locallabels_mangle(const char *text, char *out, size_t outsz);

/*
 * Pushes a fresh scope that has never been used before and never will
 * be again, and makes it the current scope -- call this right before
 * expanding a macro invocation's body.
 */
void locallabels_push_scope(void);

/*
 * Restores whatever scope was current before the most recent
 * locallabels_push_scope() call -- call this right after a macro
 * invocation's body has been fully expanded. Push/pop calls nest
 * correctly for nested macro invocations, as long as every push is
 * matched by exactly one pop, in reverse order (ordinary LIFO stack
 * discipline).
 */
void locallabels_pop_scope(void);

#endif
