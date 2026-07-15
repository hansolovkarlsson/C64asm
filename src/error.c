/*
 * error.c - see error.h for the design rationale.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "error.h"
#include "common.h"

void asm_error(int line_no, const char *raw, const char *fmt, ...) {
    /* Render the caller's printf-style message into a fixed buffer
     * first, so the "(line N: ...)" suffix can be appended afterward
     * regardless of what the caller passed in. va_list/va_start/va_end
     * are the standard C mechanism for a function that -- like
     * printf itself -- accepts a variable number of arguments. */
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (line_no > 0) {
        fprintf(stderr, "Assembly error: %s (line %d", msg, line_no);
        if (raw && raw[0]) {
            /* Show the offending line, trimmed of the trailing newline
             * fgets() leaves on it and any surrounding whitespace, so
             * the error reads cleanly regardless of how the user
             * indented their source. */
            char trimmed[MAX_LINE_LEN];
            strncpy(trimmed, raw, sizeof(trimmed) - 1);
            trimmed[sizeof(trimmed) - 1] = '\0';
            size_t n = strlen(trimmed);
            while (n > 0 && (trimmed[n-1] == '\n' || trimmed[n-1] == '\r' ||
                              trimmed[n-1] == ' ' || trimmed[n-1] == '\t'))
                trimmed[--n] = '\0';
            size_t start = 0;
            while (trimmed[start] == ' ' || trimmed[start] == '\t') start++;
            fprintf(stderr, ": %s", trimmed + start);
        }
        fprintf(stderr, ")\n");
    } else {
        fprintf(stderr, "Assembly error: %s\n", msg);
    }
    exit(1);
}
