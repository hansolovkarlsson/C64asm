/*
 * symtab.c - see symtab.h.
 */

#include <string.h>
#include "symtab.h"
#include "error.h"

Symbol symtab[MAX_SYMBOLS];
int symtab_count = 0;

Symbol *find_symbol(const char *name) {
    /* A simple linear scan. With up to MAX_SYMBOLS (32768) entries and
     * every one of them re-looked-up on every reference across two full
     * passes, a hash table would be the "proper" choice for a
     * production assembler -- but for the source sizes this project
     * actually targets (small hobbyist programs, not something with
     * tens of thousands of labels), a linear scan is simple, correct,
     * and fast enough that the difference is not something you'd
     * notice. Optimizing this would be a reasonable next step if you
     * wanted to extend this codebase to handle much larger programs. */
    for (int i = 0; i < symtab_count; i++)
        if (strcmp(symtab[i].name, name) == 0) return &symtab[i];
    return NULL;
}

void define_symbol(const char *name, long value, int line_no,
                    int pass_no, int allow_redefine, const char *raw) {
    Symbol *s = find_symbol(name);
    if (s) {
        /* Only complain about a genuine redefinition during pass 1.
         * By pass 2, every label's final address is already known from
         * pass 1, so seeing "the same label again" during pass 2 just
         * means we're revisiting the same line of source we saw before
         * -- it's expected to match, not a new conflict to check for. */
        if (pass_no == 1 && !allow_redefine && s->value != value)
            asm_error(line_no, raw, "Symbol '%s' already defined", name);
        s->value = value;
        return;
    }
    if (symtab_count >= MAX_SYMBOLS)
        asm_error(line_no, raw, "Too many symbols");
    strncpy(symtab[symtab_count].name, name, MAX_IDENT - 1);
    symtab[symtab_count].name[MAX_IDENT - 1] = '\0';
    symtab[symtab_count].value = value;
    symtab_count++;
}
