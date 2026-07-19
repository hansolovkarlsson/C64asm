/*
 * error.h - the assembler's error-reporting functions: asm_error() for
 * genuinely fatal problems, and the recoverable multi-error mechanism
 * (asm_error_recoverable() and friends) for everything else. Plus the
 * small bit of state that lets messages be filename-aware once
 * .include (macro.h/includes.h) is used.
 */

#ifndef C64ASM_ERROR_H
#define C64ASM_ERROR_H

/*
 * Prints "Assembly error: <message> (line <N>: <source text>)" to
 * stderr and terminates the program immediately with exit status 1.
 *
 * Reserved for genuinely fatal problems -- a missing file, a circular
 * .include, a macro or conditional-assembly block whose structure is
 * broken -- where the shape of the rest of the source file becomes
 * ambiguous and there's no reasonable way to keep going. Everything
 * else uses asm_error_recoverable() below instead, so a single run can
 * surface several independent mistakes rather than stopping at the
 * first one.
 *
 * line_no: the 1-based source line the error relates to, or 0 if there
 *          isn't a specific line (e.g. a command-line argument problem).
 * raw:     the original, unmodified text of that source line, or NULL.
 *          Shown for context, trimmed of leading/trailing whitespace.
 * fmt:     a printf-style format string for the error message itself.
 *
 * If .include has been used anywhere in this run (see
 * asm_error_note_include_used() below), the message also names which
 * file line_no belongs to (see asm_error_set_file()) -- otherwise the
 * message is identical to what it would have been before .include
 * support existed, with no filename at all.
 *
 * This function never returns -- it always calls exit(1). It's still
 * declared as returning void rather than marked _Noreturn, to keep this
 * file plain, portable C99 without relying on newer standard-library
 * attributes.
 */
void asm_error(int line_no, const char *raw, const char *fmt, ...);

/*
 * Prints a '.warning' directive's message, in the same
 * "(line N: source text)" format asm_error()/asm_error_recoverable()
 * use -- but doesn't count toward the error total, doesn't stop pass 2
 * from running or output from being written, and doesn't affect the
 * exit status. Nothing else in this assembler currently produces a
 * warning; this exists purely for the '.warning' directive itself
 * (see assembler.c) to call.
 */
void asm_warning(int line_no, const char *raw, const char *fmt, ...);

/*
 * The recoverable counterpart to asm_error() -- see the note at the top
 * of error.c for the full design rationale. Records the message (in
 * asm_error()'s exact display format) and returns normally instead of
 * exiting, so the caller can carry on with some sensible fallback value
 * -- each call site chooses its own fallback right where it calls this,
 * the same way it already had to choose what to do in the success case.
 *
 * If the number of recorded errors reaches the cap (see error.c), this
 * prints everything collected so far and exits immediately -- so, like
 * asm_error(), this function may not return, and callers must supply a
 * fallback value as if it always does.
 */
void asm_error_recoverable(int line_no, const char *raw, const char *fmt, ...);

/* True if asm_error_recoverable() has recorded at least one error so far
 * this run. */
int any_errors_recorded(void);

/* Clears all recorded errors and the total count. Must be called once,
 * before loading/assembling a source file -- see the note in error.c
 * about why this has to happen before the source is loaded, not just
 * before pass 1. */
void reset_collected_errors(void);

/* Prints every collected error message, an "... and N more errors"
 * summary line if the cap was hit, and a final "N errors." line, then
 * exits with status 1. */
void print_all_collected_errors_and_exit(void);

/*
 * Sets which file subsequent asm_error()/asm_error_recoverable() calls
 * should attribute their line number to, until the next call. Pass NULL
 * (or an empty string) to clear it.
 *
 * This is deliberately global, mutable state rather than a filename
 * parameter threaded through eval_expr(), parse_operand(), and every
 * other function that can call these: doing that properly would mean
 * touching a large fraction of this codebase's functions to plumb a
 * value through that, for the overwhelming majority of programs
 * (anything not using .include), is never even read. Reading a small
 * piece of global state at the exact moment an error is actually raised
 * gets the same result with a far smaller footprint -- and it's safe
 * here specifically because assembly is strictly sequential and
 * single-threaded: there is only ever one "currently relevant" file for
 * error-reporting purposes at any given moment, whether during
 * preprocessing or during either assembly pass. See assembler.c's
 * run_pass() and includes.c for where this gets called.
 */
void asm_error_set_file(const char *filename);

/*
 * Marks that .include has been used at least once in this run, so
 * error messages should start showing filenames. Idempotent -- safe to
 * call every time an .include is processed, not just the first.
 */
void asm_error_note_include_used(void);

#endif
