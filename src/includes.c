/*
 * includes.c - see includes.h.
 */

/* Must come before any #include: realpath() is POSIX.1-2008, not
 * standard C99, and without this some libcs (in strict -std=c99 mode)
 * won't declare it from <stdlib.h> at all -- which, left unnoticed, is
 * worse than a missing function: an implicitly-declared function is
 * assumed by the compiler to return int, and on a 64-bit platform where
 * pointers and int are different sizes, that silently corrupts the
 * real (pointer-returning) result rather than just failing to compile.
 * _XOPEN_SOURCE 700 (rather than _POSIX_C_SOURCE, which on its own
 * turned out not to be sufficient on glibc) is the portable choice
 * that's recognized consistently by both glibc and macOS's libc. */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>    /* PATH_MAX */
#include <sys/stat.h>  /* stat, S_ISREG */
#include "includes.h"
#include "common.h"
#include "error.h"

#ifndef PATH_MAX
#define PATH_MAX 4096   /* POSIX systems normally define this; this fallback
                            only matters on the rare system that doesn't */
#endif

/* canon: canonical (realpath()-resolved) paths, for cycle detection and
 *        include-once comparison -- small, fixed arrays (bounded by
 *        MAX_INCLUDE_DEPTH / MAX_INCLUDED_FILES, not by MAX_LINES), so
 *        a full PATH_MAX per entry is affordable here even though it
 *        isn't for the per-line filename in SourceLine.
 * display: matching resolved-but-not-canonicalized names, for error
 *        messages and for what gets passed to on_line as `filename`. */
static char open_stack_canon[MAX_INCLUDE_DEPTH + 1][PATH_MAX];
static char open_stack_display[MAX_INCLUDE_DEPTH + 1][MAX_FILENAME_LEN];
static int open_stack_top = 0;

static char already_included[MAX_INCLUDED_FILES][PATH_MAX];
static int already_included_count = 0;

/* --lib-dir's value, or NULL if includes_set_lib_dir() was never
 * called -- see includes_process_file() for how it's used as a
 * fallback search root. */
static const char *g_lib_dir = NULL;

void includes_set_lib_dir(const char *dir) {
    g_lib_dir = dir;
}

/* Copies the directory portion of `path` (including the trailing '/')
 * into `out`, or an empty string if `path` has no directory component
 * (a bare filename, implicitly relative to the current directory). */
static void dirname_of(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    if (!slash) { out[0] = '\0'; return; }
    size_t len = (size_t)(slash - path + 1);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static void join_path(const char *dir, const char *name, char *out, size_t outsz) {
    if (dir[0] == '\0') {
        strncpy(out, name, outsz - 1); out[outsz - 1] = '\0';
    } else {
        snprintf(out, outsz, "%s%s", dir, name);
    }
}

static int is_already_included(const char *canon) {
    for (int i = 0; i < already_included_count; i++)
        if (strcmp(already_included[i], canon) == 0) return 1;
    return 0;
}

static int is_currently_open(const char *canon) {
    for (int i = 0; i < open_stack_top; i++)
        if (strcmp(open_stack_canon[i], canon) == 0) return 1;
    return 0;
}

void includes_resolve_asset_path(const char *requested_path, const char *including_file,
                                  char *resolved_display, size_t resolved_sz,
                                  char *lib_dir_display, size_t lib_dir_sz,
                                  int *tried_lib_dir) {
    *tried_lib_dir = 0;
    if (including_file && requested_path[0] != '/') {
        char dir[MAX_FILENAME_LEN];
        dirname_of(including_file, dir, sizeof(dir));
        join_path(dir, requested_path, resolved_display, resolved_sz);
    } else {
        strncpy(resolved_display, requested_path, resolved_sz - 1);
        resolved_display[resolved_sz - 1] = '\0';
    }

    /* --lib-dir is purely a fallback: the default resolution above
     * (relative to the file containing the directive) is always
     * tried first, and if it finds the file, --lib-dir is never even
     * consulted -- so a project with its own local lib/ next to it
     * keeps working unchanged whether or not --lib-dir is given. Only
     * when that default lookup comes up empty, a lib_dir was set, and
     * the requested path isn't absolute (an absolute path is never
     * subject to search-path fallback, same as the default resolution
     * above), do we also try requested_path relative to lib_dir.
     *
     * --lib-dir names the lib/ directory itself (the one holding
     * text.inc, input.inc, ...), not its parent -- so a leading
     * "lib/" in the requested path (this project's own convention,
     * e.g. `.include "lib/text.inc"`) is stripped before joining with
     * --lib-dir, or `--lib-dir /shared/c64lib` would end up looking
     * for /shared/c64lib/lib/text.inc, one "lib" too many. A
     * requested path that doesn't start with "lib/" is joined as-is. */
    struct stat default_st;
    int default_exists = (stat(resolved_display, &default_st) == 0 && S_ISREG(default_st.st_mode));
    if (!default_exists && g_lib_dir && including_file && requested_path[0] != '/') {
        const char *lib_relative = requested_path;
        if (strncmp(lib_relative, "lib/", 4) == 0) lib_relative += 4;
        snprintf(lib_dir_display, lib_dir_sz, "%s/%s", g_lib_dir, lib_relative);
        struct stat lib_st;
        *tried_lib_dir = 1;
        if (stat(lib_dir_display, &lib_st) == 0 && S_ISREG(lib_st.st_mode)) {
            strncpy(resolved_display, lib_dir_display, resolved_sz - 1);
            resolved_display[resolved_sz - 1] = '\0';
        }
    }
}

void includes_process_file(const char *requested_path, const char *including_file,
                            int including_line_no, const char *including_raw,
                            IncludeLineCallback on_line) {
    char resolved_display[MAX_FILENAME_LEN];
    char lib_dir_display[MAX_FILENAME_LEN];
    int tried_lib_dir;
    includes_resolve_asset_path(requested_path, including_file,
                                 resolved_display, sizeof(resolved_display),
                                 lib_dir_display, sizeof(lib_dir_display), &tried_lib_dir);

    char canon[PATH_MAX];
    struct stat st;
    if (!realpath(resolved_display, canon) || stat(canon, &st) != 0 || !S_ISREG(st.st_mode)) {
        if (including_file) {
            if (tried_lib_dir && strcmp(resolved_display, lib_dir_display) != 0)
                asm_error(including_line_no, including_raw,
                          "Cannot open included file '%s' (also tried '%s' via --lib-dir)",
                          resolved_display, lib_dir_display);
            else
                asm_error(including_line_no, including_raw,
                          "Cannot open included file '%s'", resolved_display);
        } else {
            fprintf(stderr, "Cannot open input file '%s'\n", resolved_display);
            exit(1);
        }
        return; /* unreachable */
    }

    if (is_already_included(canon))
        return; /* include-once: silently skip a file already fully processed */

    if (is_currently_open(canon)) {
        char chain[MAX_LINE_LEN];
        chain[0] = '\0';
        for (int i = 0; i < open_stack_top; i++) {
            strncat(chain, open_stack_display[i], sizeof(chain) - strlen(chain) - 1);
            strncat(chain, " -> ", sizeof(chain) - strlen(chain) - 1);
        }
        strncat(chain, resolved_display, sizeof(chain) - strlen(chain) - 1);
        asm_error(including_line_no, including_raw, "circular .include detected: %s", chain);
    }

    if (open_stack_top >= MAX_INCLUDE_DEPTH)
        asm_error(including_line_no, including_raw,
                  ".include nested too deeply (max %d) -- possible circular include?",
                  MAX_INCLUDE_DEPTH);

    strncpy(open_stack_canon[open_stack_top], canon, PATH_MAX - 1);
    open_stack_canon[open_stack_top][PATH_MAX - 1] = '\0';
    strncpy(open_stack_display[open_stack_top], resolved_display, MAX_FILENAME_LEN - 1);
    open_stack_display[open_stack_top][MAX_FILENAME_LEN - 1] = '\0';
    open_stack_top++;
    asm_error_set_file(resolved_display);

    FILE *f = fopen(resolved_display, "r");
    if (!f) /* realpath()+stat() already confirmed this file exists and is
             * regular, so this would only fail on something like a
             * permissions change between then and now -- defensive, not
             * expected to actually trigger in practice */
        asm_error(including_line_no, including_raw,
                  "Cannot open included file '%s'", resolved_display);

    char buf[MAX_LINE_LEN];
    int line_no = 0;
    while (fgets(buf, sizeof(buf), f)) {
        line_no++;
        on_line(buf, resolved_display, line_no);
    }
    fclose(f);

    open_stack_top--;
    asm_error_set_file(open_stack_top > 0 ? open_stack_display[open_stack_top - 1] : NULL);
    if (already_included_count < MAX_INCLUDED_FILES) {
        strncpy(already_included[already_included_count], canon, PATH_MAX - 1);
        already_included[already_included_count][PATH_MAX - 1] = '\0';
        already_included_count++;
    }
}
