/*
 * fileio.h - reads the source file into g_lines (see lineparser.h),
 * one SourceLine per line of input.
 */

#ifndef C64ASM_FILEIO_H
#define C64ASM_FILEIO_H

/*
 * Opens `path`, reads it line by line, and calls split_line() on each
 * line to populate g_lines/g_line_count. This is the only place
 * g_lines itself gets allocated.
 *
 * Exits the program (via a direct fprintf+exit, not asm_error(), since
 * there's no specific source line to blame yet) if the file can't be
 * opened, if it exceeds MAX_LINES, or if the initial allocation fails.
 */
void load_source(const char *path);

#endif
