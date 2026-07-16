/*
 * strutils.h - small, generic string helpers used throughout the
 * assembler. None of these know anything about 6502 assembly syntax --
 * they're the kind of little utility functions C's standard library
 * doesn't provide, that almost every C program ends up writing its own
 * version of.
 */

#ifndef C64ASM_STRUTILS_H
#define C64ASM_STRUTILS_H

#include "common.h"

/*
 * Trims leading and trailing whitespace from a string, in place.
 * Whitespace is whatever isspace() considers it (space, tab, newline,
 * carriage return, form feed, vertical tab).
 *
 * "In place" means this modifies the buffer it's given directly, using
 * memmove() to shift the remaining characters left if there was leading
 * whitespace to remove. There's no separately-allocated result to free
 * afterward -- the caller's own buffer is now the trimmed string.
 */
void trim(char *s);

/* True if c can legally start an identifier (label, mnemonic, directive,
 * symbol name): a letter or underscore. 6502 assembly identifiers can't
 * start with a digit, which is what lets the tokenizer in expr.c tell
 * "10" (a number) apart from "L10" (an identifier) just by looking at
 * the first character. */
int is_ident_start(char c);

/* True if c can appear anywhere *after* the first character of an
 * identifier: a letter, digit, or underscore. */
int is_ident_char(char c);

/*
 * Converts an ASCII string to C64 PETSCII bytes, suitable for output
 * through the KERNAL CHROUT routine on the C64's default (uppercase)
 * character set.
 *
 * Uppercase letters, digits, and punctuation are already correct
 * PETSCII values in that range (identical to ASCII); only lowercase
 * input needs folding up to display as uppercase, since the default
 * character set has no distinct lowercase glyphs at those codes -- the
 * codes that *would* be lowercase display as line-drawing/graphics
 * characters instead unless the C64 is switched into its alternate
 * character set (which this assembler doesn't do). Getting this
 * mapping backwards was a real, shipped bug at one point in this
 * project's history: text that was already uppercase came out as
 * scrambled graphics symbols instead of letters.
 *
 * s:      null-terminated input string.
 * out:    caller-provided buffer, must be at least strlen(s) bytes.
 * outlen: receives the number of bytes written (== strlen(s), always,
 *         since this is a straight one-byte-in-one-byte-out mapping).
 */
void ascii_to_petscii(const char *s, unsigned char *out, int *outlen);

/* Removes a ';' comment from `line`, in place, stopping at the first ';'
 * that isn't inside a double-quoted string -- so a semicolon inside a
 * .text "..." string doesn't get mistaken for the start of a comment.
 * Used by both lineparser.c (parsing an ordinary source line) and
 * macro.c (deciding what a raw line's meaningful content is, before
 * checking whether it's a macro definition or invocation). */
void strip_comment(char *line);

/*
 * Splits a comma-separated argument list (e.g. the operand of
 * ".byte 1, 2, \"hi\", 3", or a macro invocation's argument list) into
 * individual trimmed argument strings, respecting quotes and
 * parentheses -- a comma inside a quoted string or inside parentheses
 * doesn't count as a separator.
 *
 * operand:  the text to split, e.g. "1, 2, \"hi\", 3".
 * args:     caller-provided output array.
 * max_args: how many entries `args` has room for.
 *
 * Returns the number of arguments found (and written into `args`).
 */
int split_args(const char *operand, char args[][MAX_LINE_LEN], int max_args);

#endif
