/*
 * fileio.h - loads the main source file into g_lines (see
 * lineparser.h), delegating the actual file-reading and per-line
 * dispatch to includes.c (the same machinery ".include" uses).
 */

#ifndef C64ASM_FILEIO_H
#define C64ASM_FILEIO_H

/*
 * Allocates g_lines (the only place that happens) and processes `path`
 * as the top-level source file -- see includes_process_file()
 * (includes.h) for what "processes" actually involves: resolving,
 * opening, reading it line by line, and feeding each line to
 * macro_process_line() (macro.h), which populates g_lines/g_line_count.
 *
 * Exits the program with a plain "Cannot open input file" message (not
 * routed through asm_error(), since there's no specific source line to
 * blame yet) if `path` can't be opened, or if the initial g_lines
 * allocation fails.
 */
void load_source(const char *path);

#endif
