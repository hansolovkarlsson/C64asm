/*
 * fileio.c - see fileio.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include "fileio.h"
#include "common.h"
#include "lineparser.h"
#include "macro.h"
#include "includes.h"

void load_source(const char *path) {
    /* g_lines is sized for the largest source file this assembler
     * supports (MAX_LINES) up front, rather than grown dynamically as
     * bytebuf.c's ByteBuf does -- a reasonable choice here since a
     * SourceLine is a fixed, known size and the total (MAX_LINES *
     * sizeof(SourceLine)) is a single allocation the OS can satisfy
     * easily, whereas the assembled *output* size in bytebuf.c has no
     * natural upper bound to size a fixed array for in the first place. */
    g_lines = malloc(sizeof(SourceLine) * MAX_LINES);
    if (!g_lines) { fprintf(stderr, "Out of memory\n"); exit(1); }

    /* The actual file-opening, reading, and per-line dispatch (through
     * macro_process_line(), which is what calls split_line() and
     * appends to g_lines -- see macro.c) is identical whether this is
     * the top-level file or one reached via .include, so it all lives
     * in includes_process_file() (includes.h) now. Passing NULL as the
     * "including file" here is what tells that function this is the
     * top-level call -- among other things, it's what selects the
     * plain "Cannot open input file" message (matching this function's
     * behavior from before .include existed) over the file-and-line-
     * attributed "Cannot open included file" used for a real .include. */
    includes_process_file(path, NULL, 0, NULL, macro_process_line);
}
