/*
 * common.h - shared size limits used across the whole assembler.
 *
 * These are all "big enough for any realistic program" fixed sizes.
 * This assembler doesn't use dynamic string buffers for most things
 * (source lines, identifiers, expressions) -- everything is a fixed-size
 * char array on the stack or in a struct. That's a deliberate, very
 * traditional C style choice: it avoids a huge amount of malloc/free
 * bookkeeping at the cost of hard ceilings on things like identifier
 * length. If you're reading this codebase to learn: this is one of the
 * two classic ways to handle strings in C (the other being fully
 * dynamic, growable buffers, which you can see in bytebuf.h/.c instead,
 * used specifically for the assembled output because its size truly
 * can't be known in advance).
 */

#ifndef C64ASM_COMMON_H
#define C64ASM_COMMON_H

#define MAX_LINE_LEN   1024    /* longest source line (or expression, or
                                   operand text) this assembler will handle */
#define MAX_LINES      200000  /* largest source file, in lines */
#define MAX_SYMBOLS    32768   /* largest symbol table (labels + constants) */
#define MAX_MNEMONICS  64      /* size of the opcode table -- there are only
                                   56 real 6502 mnemonics, so this has room
                                   to spare */
#define MAX_TOKENS     128     /* longest expression, in tokens */
#define MAX_ARGS       64      /* most comma-separated arguments a single
                                   .byte/.word/.text/.fill line can have */
#define MAX_IDENT      128     /* longest label, mnemonic, or directive name */

#define MAX_MACROS              128  /* largest macro table */
#define MAX_MACRO_PARAMS        8    /* most parameters one macro can declare */
#define MAX_MACRO_BODY_LINES    200  /* longest macro body, in lines */
#define MAX_MACRO_EXPANSION_DEPTH 16 /* guards against runaway/infinite recursive macros */

#define MAX_FILENAME_LEN 256    /* longest display filename this assembler will
                                    track per line -- deliberately much smaller
                                    than PATH_MAX, since it's multiplied by
                                    MAX_LINES in SourceLine (lineparser.h); a
                                    canonical path used only for cycle/dedup
                                    comparisons (never stored per-line) gets a
                                    full PATH_MAX-sized buffer instead, see
                                    includes.c */
#define MAX_INCLUDE_DEPTH 16     /* guards against runaway/circular .include chains */
#define MAX_INCLUDED_FILES 256   /* total distinct files includable in one run */
#define MAX_COND_DEPTH 16        /* guards against runaway .if/.ifdef nesting */

#endif
