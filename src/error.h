/*
 * error.h - the assembler's one and only error-reporting function, plus
 * the small bit of state that lets its messages be filename-aware once
 * .include (macro.h/includes.h) is used.
 */

#ifndef C64ASM_ERROR_H
#define C64ASM_ERROR_H

/*
 * Prints "Assembly error: <message> (line <N>: <source text>)" to
 * stderr and terminates the program immediately with exit status 1.
 *
 * This assembler does not try to recover from errors and keep going to
 * report a second or third problem -- the moment something is wrong, it
 * stops. That's a real, deliberate trade-off: a "keep going and collect
 * all the errors" assembler is friendlier for large programs with many
 * mistakes, but is also a lot more code (you have to decide, for every
 * kind of error, what a "reasonable" default is so the rest of the file
 * can still be processed meaningfully). For a small hobbyist assembler,
 * stop-at-the-first-error is simpler to get right and arguably more
 * honest: nothing after the first uncaught mistake is generated.
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
 * support existed, with no filename at all. That "no filename until
 * .include is actually used" behavior is deliberate: it means every
 * program that predates .include, or simply never uses it, gets
 * byte-for-byte identical error output to before.
 *
 * This function never returns -- it always calls exit(1). It's still
 * declared as returning void rather than marked _Noreturn, to keep this
 * file plain, portable C99 without relying on newer standard-library
 * attributes.
 */
void asm_error(int line_no, const char *raw, const char *fmt, ...);

/*
 * Sets which file subsequent asm_error() calls should attribute their
 * line number to, until the next call. Pass NULL (or an empty string)
 * to clear it.
 *
 * This is deliberately global, mutable state rather than a filename
 * parameter threaded through eval_expr(), parse_operand(), and every
 * other function that can call asm_error(): doing that properly would
 * mean touching a large fraction of this codebase's functions to plumb
 * a value through that, for the overwhelming majority of programs
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
 * asm_error() should start showing filenames. Idempotent -- safe to
 * call every time an .include is processed, not just the first.
 */
void asm_error_note_include_used(void);

#endif
