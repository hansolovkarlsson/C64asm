/*
 * error.c - see error.h for the public API. This file has two halves:
 *
 *   1. asm_error() -- unchanged from before multi-error reporting
 *      existed: prints one message and exits immediately. Still used
 *      for genuinely fatal, whole-file-structural problems (a missing
 *      file, a circular .include, a broken macro or conditional-
 *      assembly block).
 *
 *   2. The recoverable multi-error mechanism -- asm_error_recoverable()
 *      records a message (using the exact same formatting asm_error()
 *      has always produced) and returns normally instead of exiting.
 *      Each call site supplies its own safe fallback value right after
 *      the call, exactly as it already had to decide what to return in
 *      the success case. This is what lets one assembly run surface
 *      several independent mistakes instead of stopping at the first
 *      one. It's an intentional trade-off: a later error's line number
 *      and message are still exactly correct, but if an earlier error
 *      meant a value or an addressing-mode decision came out different
 *      from what the source actually implies, a handful of further
 *      messages may be downstream noise from that first real mistake
 *      rather than independent problems of their own -- fix the first
 *      one and reassemble if the rest look strange.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "error.h"
#include "common.h"

static char g_current_error_file[MAX_FILENAME_LEN] = "";
static int g_multi_file_mode = 0;

void asm_error_set_file(const char *filename) {
    if (filename && filename[0]) {
        strncpy(g_current_error_file, filename, sizeof(g_current_error_file) - 1);
        g_current_error_file[sizeof(g_current_error_file) - 1] = '\0';
    } else {
        g_current_error_file[0] = '\0';
    }
}

void asm_error_note_include_used(void) {
    g_multi_file_mode = 1;
}

/* Builds the exact "<prefix>: ..." text into a caller-supplied buffer
 * instead of straight to stderr (prefix is always "Assembly error" for
 * asm_error()/asm_error_recoverable()) -- shared by the fatal path
 * (asm_error), the recoverable path (asm_error_recoverable), and
 * asm_warning() (which passes "Warning" instead), so all three
 * produce byte-for-byte identical location formatting for the same
 * inputs. */
static void format_error_message(char *buf, size_t bufsz, int line_no,
                                  const char *raw, const char *msg,
                                  const char *prefix) {
    if (line_no > 0) {
        char head[MAX_LINE_LEN + 256];
        if (g_multi_file_mode && g_current_error_file[0])
            snprintf(head, sizeof(head), "%s: %s (%s, line %d",
                     prefix, msg, g_current_error_file, line_no);
        else
            snprintf(head, sizeof(head), "%s: %s (line %d", prefix, msg, line_no);
        if (raw && raw[0]) {
            char trimmed[MAX_LINE_LEN];
            strncpy(trimmed, raw, sizeof(trimmed) - 1);
            trimmed[sizeof(trimmed) - 1] = '\0';
            size_t n = strlen(trimmed);
            while (n > 0 && (trimmed[n-1] == '\n' || trimmed[n-1] == '\r' ||
                              trimmed[n-1] == ' ' || trimmed[n-1] == '\t'))
                trimmed[--n] = '\0';
            size_t start = 0;
            while (trimmed[start] == ' ' || trimmed[start] == '\t') start++;
            snprintf(buf, bufsz, "%s: %s)", head, trimmed + start);
        } else {
            snprintf(buf, bufsz, "%s)", head);
        }
    } else {
        snprintf(buf, bufsz, "%s: %s", prefix, msg);
    }
}

void asm_error(int line_no, const char *raw, const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char full[MAX_LINE_LEN + 1280];
    format_error_message(full, sizeof(full), line_no, raw, msg, "Assembly error");
    fprintf(stderr, "%s\n", full);
    exit(1);
}

/* Prints a '.warning' directive's message (see that directive's
 * handling in assembler.c), in the same "(line N: source text)" format
 * asm_error()/asm_error_recoverable() use -- but, unlike either of
 * those, this doesn't count toward the error total, doesn't stop pass
 * 2 from running or output from being written, and doesn't affect the
 * exit status. Nothing else in this assembler currently produces a
 * warning; this exists purely for the '.warning' directive itself to
 * call. */
void asm_warning(int line_no, const char *raw, const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char full[MAX_LINE_LEN + 1280];
    format_error_message(full, sizeof(full), line_no, raw, msg, "Warning");
    fprintf(stderr, "%s\n", full);
}

#define MAX_COLLECTED_ERRORS 20
static char *g_collected_errors[MAX_COLLECTED_ERRORS];
static int g_collected_count = 0;
static long g_total_error_count = 0;

int any_errors_recorded(void) { return g_total_error_count > 0; }

/* Must be called once, before the source file is loaded -- not just
 * before pass 1. Loading itself (line_parser.c's split_line(), via
 * whatever drives it) can record recoverable errors of its own (e.g.
 * "Unknown mnemonic or directive"), and those need to survive into the
 * pass-1/pass-2 checks; resetting after loading would silently wipe
 * them out. */
void reset_collected_errors(void) {
    for (int i = 0; i < g_collected_count; i++) { free(g_collected_errors[i]); g_collected_errors[i] = NULL; }
    g_collected_count = 0;
    g_total_error_count = 0;
}

void print_all_collected_errors_and_exit(void) {
    for (int i = 0; i < g_collected_count; i++)
        fprintf(stderr, "%s\n", g_collected_errors[i]);
    long remaining = g_total_error_count - g_collected_count;
    if (remaining > 0) {
        fprintf(stderr, "... and %ld more error%s (stopping after %d)\n",
                remaining, remaining == 1 ? "" : "s", MAX_COLLECTED_ERRORS);
    }
    fprintf(stderr, "%ld error%s.\n", g_total_error_count, g_total_error_count == 1 ? "" : "s");
    exit(1);
}

void asm_error_recoverable(int line_no, const char *raw, const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    g_total_error_count++;
    if (g_collected_count < MAX_COLLECTED_ERRORS) {
        size_t bufsz = MAX_LINE_LEN + 1280;
        char *buf = malloc(bufsz);
        if (!buf) { fprintf(stderr, "Out of memory\n"); exit(1); }
        format_error_message(buf, bufsz, line_no, raw, msg, "Assembly error");
        g_collected_errors[g_collected_count++] = buf;
        return;
    }
    /* At least the (MAX_COLLECTED_ERRORS+1)th error -- stop right here
     * rather than continuing to burn through what could be an enormous,
     * mostly-noise number of further messages on a badly-broken or
     * wrong-language source file. */
    print_all_collected_errors_and_exit();
}
