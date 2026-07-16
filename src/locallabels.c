/*
 * locallabels.c - see locallabels.h.
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "locallabels.h"
#include "common.h"
#include "strutils.h"

#define LOCAL_LABEL_PREFIX "__local"

/* current_scope: the scope id used to mangle @names right now.
 * next_scope: an ever-increasing counter; a fresh value is handed out
 *             for every new scope, so no two scopes ever share an id.
 * scope_stack: saved current_scope values, so a push/pop pair can
 *              restore the enclosing scope after a (possibly nested)
 *              macro expansion. Sized for MAX_MACRO_EXPANSION_DEPTH
 *              nested invocations, matching the depth limit macro.c
 *              enforces, so this can never overflow. */
static int current_scope = 0;
static int next_scope = 1;
static int scope_stack[MAX_MACRO_EXPANSION_DEPTH + 1];
static int scope_stack_top = 0;

void locallabels_maybe_new_scope(const char *trimmed_line) {
    if (trimmed_line[0] == '\0' || trimmed_line[0] == '@' || !is_ident_start(trimmed_line[0]))
        return;
    size_t i = 0;
    while (trimmed_line[i] && is_ident_char(trimmed_line[i])) i++;
    if (trimmed_line[i] == ':') {
        current_scope = next_scope;
        next_scope++;
    }
}

void locallabels_mangle(const char *text, char *out, size_t outsz) {
    size_t oi = 0;
    size_t n = strlen(text);
    int in_str = 0;
    for (size_t i = 0; i < n && oi + 1 < outsz; ) {
        char c = text[i];
        if (c == '"') {
            in_str = !in_str;
            out[oi++] = c; i++;
        } else if (c == '@' && !in_str && i + 1 < n &&
                   (isalpha((unsigned char)text[i+1]) || text[i+1] == '_')) {
            size_t j = i + 1;
            while (j < n && (isalnum((unsigned char)text[j]) || text[j] == '_')) j++;
            int written = snprintf(out + oi, outsz - oi, "%s%d_%.*s",
                                    LOCAL_LABEL_PREFIX, current_scope,
                                    (int)(j - (i + 1)), text + i + 1);
            oi += (written > 0) ? (size_t)written : 0;
            if (oi > outsz - 1) oi = outsz - 1;
            i = j;
        } else {
            out[oi++] = c; i++;
        }
    }
    out[oi] = '\0';
}

void locallabels_push_scope(void) {
    scope_stack[scope_stack_top++] = current_scope;
    current_scope = next_scope;
    next_scope++;
}

void locallabels_pop_scope(void) {
    current_scope = scope_stack[--scope_stack_top];
}
