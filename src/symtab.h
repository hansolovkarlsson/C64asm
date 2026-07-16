/*
 * symtab.h - the symbol table: every label and named constant the
 * assembler has seen, and the address or value it's bound to.
 *
 * This is what makes forward references possible ("JMP loop" written
 * before "loop:" is actually defined) -- see the header comment in
 * assembler.c for how the two-pass structure and this table work
 * together.
 */

#ifndef C64ASM_SYMTAB_H
#define C64ASM_SYMTAB_H

#include "common.h"

typedef struct {
    char name[MAX_IDENT];
    long value;
    int first_li;   /* g_lines index of this symbol's FIRST definition,
                        never updated on a later redefinition -- used only
                        by .ifdef/.ifndef (assembler.c) to correctly answer
                        "has this been defined YET" in a way that stays
                        consistent across both assembly passes. See the
                        conditional-assembly design note in assembler.h
                        for why a plain find_symbol() != NULL check isn't
                        safe for that specific question. */
} Symbol;

/* The symbol table and its current size, exposed directly (not through
 * accessor functions) because main.c needs to walk and sort the whole
 * table to print it in the optional listing file. A larger production
 * codebase might wrap this in getters to keep the representation
 * private; for a program this size, direct access to a well-named
 * global is simpler to read and there's only ever one symbol table in
 * the whole program anyway. */
extern Symbol symtab[MAX_SYMBOLS];
extern int symtab_count;

/* Looks up a symbol by exact, case-sensitive name. Returns NULL if it
 * hasn't been defined (yet, or at all) -- the expression evaluator in
 * expr.c uses that to detect forward references and undefined symbols. */
Symbol *find_symbol(const char *name);

/*
 * Like find_symbol(), but only counts as found if the symbol's FIRST
 * definition was strictly before g_lines index `li`. This is what
 * .ifdef/.ifndef (assembler.c) use instead of find_symbol() -- see the
 * conditional-assembly design note in assembler.h for why "defined
 * right now" and "defined before this specific line" are genuinely
 * different questions once a two-pass assembler's persistent symbol
 * table is involved.
 */
Symbol *find_symbol_defined_before(const char *name, int li);

/*
 * Defines (or, for a label revisited on pass 2, redefines) a symbol.
 *
 * name:           the symbol's name.
 * value:          the address (for a label) or evaluated expression
 *                 (for a "name = expr" constant) to bind it to.
 * line_no, raw:   passed straight through to asm_error() if something's
 *                 wrong; identify where in the source this happened.
 * pass_no:        1 or 2 -- see assembler.c's header comment for what
 *                 the two passes are for.
 * allow_redefine: constants (defined with "=") are allowed to change
 *                 value if the same name is assigned again later in the
 *                 source (pass this as nonzero). Labels are not --
 *                 a label appearing twice with two different resolved
 *                 addresses is treated as a genuine error, but only
 *                 during pass 1 (pass 2 just re-records the same,
 *                 by-then-already-known-consistent value; see the .c
 *                 file for exactly why the check is pass-1-only).
 * li:             the g_lines index of this definition -- recorded as
 *                 the symbol's first_li only the first time it's ever
 *                 defined (never touched on a later redefinition). See
 *                 find_symbol_defined_before() above for why this
 *                 matters.
 */
void define_symbol(const char *name, long value, int line_no,
                    int pass_no, int allow_redefine, const char *raw, int li);

#endif
