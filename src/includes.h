/*
 * includes.h - resolves and reads .include'd files, splicing their
 * lines into the same processing pipeline as the main file.
 *
 * .include "path" splices another file's lines into the source stream
 * at that point, as if they'd been pasted in directly -- resolved
 * relative to the directory of the file *containing* the .include line
 * (not the current working directory), which is what lets a library
 * file .include another library file sitting next to it, regardless of
 * where the assembler itself was invoked from. See macro.c for where
 * ".include" is actually recognized and this module gets called (both
 * for a real .include line, and once, for the top-level file itself --
 * see fileio.c).
 *
 * Handles three things a naive "just open and read the file" version
 * wouldn't:
 *   - Circular includes (A includes B includes A, directly or through a
 *     longer chain) are detected and reported with the full chain, not
 *     left to hang or to fail with a generic "too deep" message.
 *   - A hard depth limit as a backstop, in case some gap in the above
 *     were ever missed.
 *   - Automatic include-once semantics: a file that's already been
 *     fully processed earlier in this run is silently skipped on a
 *     later .include, the same way #pragma once works in C. This
 *     assembler has no conditional assembly, so it has no way to write
 *     a manual include guard -- and a shared library file (constants,
 *     common macros) being .include'd from more than one other file is
 *     the normal, expected case for "library files", not a mistake to
 *     flag.
 *
 * Both cycle detection and include-once comparison are done against
 * each file's canonical, symlink-and-".."-resolved path (via
 * realpath()), not the literal text after ".include" -- so the same
 * physical file reached via two syntactically different relative paths
 * (e.g. from two files in different directories) is still correctly
 * recognized as the same file. Display names shown in error messages
 * and stored per-line use the resolved-but-not-canonicalized form
 * instead (e.g. "lib/util.inc"), matching how the source actually
 * refers to it, since the fully-canonicalized form is usually a long,
 * less readable absolute path.
 */

#ifndef C64ASM_INCLUDES_H
#define C64ASM_INCLUDES_H

/* Called once per raw line read from a file. filename is that file's
 * resolved display name; line_no is 1-based within it. */
typedef void (*IncludeLineCallback)(const char *raw_line, const char *filename, int line_no);

/*
 * Sets the --lib-dir fallback search root (see includes_process_file()
 * below for exactly how it's used). Call this at most once, before any
 * call to includes_process_file() -- typically right after parsing
 * command-line arguments in main(). Passing NULL (or never calling
 * this at all) disables the fallback entirely, which is the default:
 * every .include resolves exactly as it did before --lib-dir existed.
 */
void includes_set_lib_dir(const char *dir);

/*
 * Resolves, opens, and processes one source file -- the main file
 * (including_file == NULL) or a .include'd one -- calling on_line for
 * every line read from it.
 *
 * requested_path:    the path as written in .include "..." (or, for the
 *                     top-level call, the command-line argument).
 * including_file:    the display name of the file containing the
 *                     .include line, used to resolve a relative
 *                     requested_path; NULL for the top-level file.
 * including_line_no, including_raw: the .include line's own line number
 *                     and source text, used to attribute any error this
 *                     call raises (file not found, circular include,
 *                     too deep) to the right place. Both ignored when
 *                     including_file is NULL.
 * on_line:           called once per line read from the resolved file.
 *
 * requested_path, if not absolute, is resolved relative to
 * including_file's own directory first -- this is the only resolution
 * that happens when includes_set_lib_dir() hasn't been called, and it
 * always takes priority even when it has. Only if that lookup fails,
 * a lib_dir was set, and requested_path is relative, is requested_path
 * *also* tried relative to lib_dir. lib_dir names the lib/ directory
 * itself (the one holding text.inc, input.inc, ...), not its parent,
 * so a leading "lib/" in requested_path is stripped first -- a lib_dir
 * of "/shared/c64lib" with `.include "lib/text.inc"` also tries
 * "/shared/c64lib/text.inc", not "/shared/c64lib/lib/text.inc". This
 * lets one shared library directory be reused across separate project
 * directories that don't each keep their own copy of lib/, without
 * changing behavior at all for anyone who doesn't set it.
 *
 * If this exact file (by canonical path) has already been fully
 * processed earlier in this run, this silently does nothing (see the
 * include-once note above) rather than calling on_line at all.
 */
void includes_process_file(const char *requested_path, const char *including_file,
                            int including_line_no, const char *including_raw,
                            IncludeLineCallback on_line);

#endif
