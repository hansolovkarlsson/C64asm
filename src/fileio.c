/*
 * fileio.c - see fileio.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include "fileio.h"
#include "common.h"
#include "lineparser.h"
#include "macro.h"

void load_source(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open input file '%s'\n", path); exit(1); }

    /* g_lines is sized for the largest source file this assembler
     * supports (MAX_LINES) up front, rather than grown dynamically as
     * bytebuf.c's ByteBuf does -- a reasonable choice here since a
     * SourceLine is a fixed, known size and the total (MAX_LINES *
     * sizeof(SourceLine)) is a single allocation the OS can satisfy
     * easily, whereas the assembled *output* size in bytebuf.c has no
     * natural upper bound to size a fixed array for in the first place. */
    g_lines = malloc(sizeof(SourceLine) * MAX_LINES);
    if (!g_lines) { fprintf(stderr, "Out of memory\n"); exit(1); }

    char buf[MAX_LINE_LEN];
    int line_no = 0;
    while (fgets(buf, sizeof(buf), f)) {
        line_no++;
        /* macro_process_line (macro.c) is what actually calls
         * split_line() and appends to g_lines now -- a raw line here
         * might expand into several real source lines (a macro
         * invocation), or none at all (part of a macro definition), so
         * the old direct "one fgets() line in, one g_lines entry out"
         * loop body no longer holds. */
        macro_process_line(buf, line_no);
    }
    fclose(f);
}
