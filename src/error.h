/*
 * error.h - the assembler's one and only error-reporting function.
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
 * This function never returns -- it always calls exit(1). It's still
 * declared as returning void rather than marked _Noreturn, to keep this
 * file plain, portable C99 without relying on newer standard-library
 * attributes.
 */
void asm_error(int line_no, const char *raw, const char *fmt, ...);

#endif
