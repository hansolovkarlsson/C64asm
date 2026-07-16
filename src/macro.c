/*
 * macro.c - see macro.h.
 */

#include <string.h>
#include <ctype.h>
#include <strings.h>
#include "macro.h"
#include "common.h"
#include "error.h"
#include "strutils.h"
#include "opcodes.h"
#include "lineparser.h"
#include "locallabels.h"

typedef struct {
    char name[MAX_IDENT];
    char params[MAX_MACRO_PARAMS][MAX_IDENT];
    int param_count;
    char body[MAX_MACRO_BODY_LINES][MAX_LINE_LEN];
    int body_line_count;
} MacroDef;

static MacroDef macros[MAX_MACROS];
static int macro_count = 0;
static MacroDef *capturing_macro = NULL;
static int macro_expansion_depth = 0;

/* Not exposed via macro.h -- nothing outside this file needs to look up
 * a macro by name. */
static MacroDef *find_macro(const char *name) {
    for (int i = 0; i < macro_count; i++)
        if (strcasecmp(macros[i].name, name) == 0) return &macros[i];
    return NULL;
}

/* Extracts the first whitespace-delimited token from s into out. */
static void first_token(const char *s, char *out, size_t outsz) {
    size_t i = 0, n = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    while (s[i] && !isspace((unsigned char)s[i]) && n + 1 < outsz) out[n++] = s[i++];
    out[n] = '\0';
}

/* Replaces every \paramname in `text` with its argument's text. A
 * backslash not followed by an identifier character is left as a
 * literal backslash (harmless -- this syntax has no other use for one);
 * a \name that doesn't match any declared parameter is a fatal error,
 * to catch typos rather than silently leaving the literal text in place. */
static void macro_substitute(const char *text, MacroDef *m, char args[][MAX_LINE_LEN],
                              char *out, size_t outsz, int line_no) {
    size_t oi = 0;
    size_t n = strlen(text);
    for (size_t i = 0; i < n && oi + 1 < outsz; ) {
        if (text[i] == '\\' && i + 1 < n &&
            (isalpha((unsigned char)text[i+1]) || text[i+1] == '_')) {
            size_t j = i + 1;
            char pname[MAX_IDENT]; size_t pn = 0;
            while (j < n && (isalnum((unsigned char)text[j]) || text[j] == '_') &&
                   pn + 1 < sizeof(pname)) {
                pname[pn++] = text[j++];
            }
            pname[pn] = '\0';
            int pidx = -1;
            for (int k = 0; k < m->param_count; k++)
                if (strcmp(m->params[k], pname) == 0) { pidx = k; break; }
            if (pidx < 0)
                asm_error(line_no, text, "Unknown macro parameter '\\%s'", pname);
            for (const char *a = args[pidx]; *a && oi + 1 < outsz; a++) out[oi++] = *a;
            i = j;
        } else {
            out[oi++] = text[i++];
        }
    }
    out[oi] = '\0';
}

/* Appends one fully-resolved (non-macro) line to g_lines, the same way
 * fileio.c's load_source() loop used to do inline before macro support
 * existed. This is the single choke point every genuinely final line
 * passes through, whether it came from ordinary source text or from
 * expanding a macro body -- so it's also where local-label scoping
 * (locallabels.h) is applied: advance the scope if this line defines an
 * ordinary global label, then mangle any @name references using
 * whatever scope is now current. Not exposed via macro.h; only
 * macro_process_line() below needs it. */
static void emit_source_line(const char *raw_line, int line_no) {
    if (g_line_count >= MAX_LINES)
        asm_error(line_no, raw_line, "Too many source lines (max %d)", MAX_LINES);

    char line[MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1); line[sizeof(line)-1] = '\0';
    strip_comment(line);
    size_t ln = strlen(line);
    while (ln > 0 && (line[ln-1]=='\n' || line[ln-1]=='\r')) line[--ln]='\0';
    trim(line);
    locallabels_maybe_new_scope(line);

    char mangled[MAX_LINE_LEN];
    locallabels_mangle(raw_line, mangled, sizeof(mangled));

    split_line(mangled, line_no, &g_lines[g_line_count]);
    g_line_count++;
}

void macro_process_line(const char *raw_line, int line_no) {
    char line[MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1); line[sizeof(line)-1] = '\0';
    strip_comment(line);
    size_t ln = strlen(line);
    while (ln > 0 && (line[ln-1]=='\n' || line[ln-1]=='\r')) line[--ln]='\0';

    char trimmed[MAX_LINE_LEN];
    strncpy(trimmed, line, sizeof(trimmed)-1); trimmed[sizeof(trimmed)-1]='\0';
    trim(trimmed);

    if (capturing_macro != NULL) {
        char first[MAX_IDENT];
        first_token(trimmed, first, sizeof(first));
        if (strcasecmp(first, ".endmacro") == 0 || strcasecmp(first, ".endm") == 0) {
            capturing_macro = NULL;
            return;
        }
        if (strcasecmp(first, ".macro") == 0)
            asm_error(line_no, raw_line,
                      "nested macro definitions are not supported (already defining '%s')",
                      capturing_macro->name);
        if (capturing_macro->body_line_count >= MAX_MACRO_BODY_LINES)
            asm_error(line_no, raw_line, "Macro '%s' body too long (max %d lines)",
                      capturing_macro->name, MAX_MACRO_BODY_LINES);
        strncpy(capturing_macro->body[capturing_macro->body_line_count], trimmed, MAX_LINE_LEN - 1);
        capturing_macro->body[capturing_macro->body_line_count][MAX_LINE_LEN-1] = '\0';
        capturing_macro->body_line_count++;
        return;
    }

    if (trimmed[0] == '\0') {
        emit_source_line(raw_line, line_no);
        return;
    }

    char first[MAX_IDENT];
    first_token(trimmed, first, sizeof(first));

    if (strcasecmp(first, ".macro") == 0) {
        char rest[MAX_LINE_LEN];
        strncpy(rest, trimmed + strlen(first), sizeof(rest)-1); rest[sizeof(rest)-1]='\0';
        trim(rest);
        if (rest[0] == '\0') asm_error(line_no, raw_line, "'.macro' requires a name");

        char name[MAX_IDENT];
        size_t sp = 0;
        while (rest[sp] && !isspace((unsigned char)rest[sp])) sp++;
        size_t cn = sp < sizeof(name)-1 ? sp : sizeof(name)-1;
        memcpy(name, rest, cn); name[cn] = '\0';

        if (find_mnemonic(name) != NULL || is_directive(name))
            asm_error(line_no, raw_line,
                      "macro name '%s' conflicts with a built-in mnemonic or directive", name);
        if (find_macro(name) != NULL)
            asm_error(line_no, raw_line, "macro '%s' already defined", name);

        char paramstr[MAX_LINE_LEN];
        strncpy(paramstr, rest + sp, sizeof(paramstr)-1); paramstr[sizeof(paramstr)-1]='\0';
        trim(paramstr);

        if (macro_count >= MAX_MACROS)
            asm_error(line_no, raw_line, "Too many macros (max %d)", MAX_MACROS);
        MacroDef *m = &macros[macro_count++];
        strncpy(m->name, name, sizeof(m->name)-1); m->name[sizeof(m->name)-1] = '\0';
        m->param_count = 0;
        m->body_line_count = 0;
        if (paramstr[0] != '\0') {
            char pargs[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(paramstr, pargs, MAX_ARGS);
            for (int i = 0; i < nargs && i < MAX_MACRO_PARAMS; i++) {
                strncpy(m->params[i], pargs[i], MAX_IDENT - 1);
                m->params[i][MAX_IDENT-1] = '\0';
                m->param_count++;
            }
        }
        capturing_macro = m;
        return;
    }

    if (strcasecmp(first, ".endmacro") == 0 || strcasecmp(first, ".endm") == 0)
        asm_error(line_no, raw_line, "'.endmacro' with no matching '.macro'");

    MacroDef *m = find_macro(first);
    if (m != NULL) {
        char argtext[MAX_LINE_LEN];
        strncpy(argtext, trimmed + strlen(first), sizeof(argtext)-1); argtext[sizeof(argtext)-1]='\0';
        trim(argtext);
        char args[MAX_ARGS][MAX_LINE_LEN];
        int nargs = (argtext[0] != '\0') ? split_args(argtext, args, MAX_ARGS) : 0;
        if (nargs != m->param_count)
            asm_error(line_no, raw_line, "macro '%s' expects %d argument(s), got %d",
                      m->name, m->param_count, nargs);

        macro_expansion_depth++;
        if (macro_expansion_depth > MAX_MACRO_EXPANSION_DEPTH)
            asm_error(line_no, raw_line,
                      "macro expansion nested too deep (recursive macro '%s'?)", m->name);

        locallabels_push_scope();

        for (int i = 0; i < m->body_line_count; i++) {
            char substituted[MAX_LINE_LEN];
            macro_substitute(m->body[i], m, args, substituted, sizeof(substituted), line_no);
            macro_process_line(substituted, line_no);
        }

        locallabels_pop_scope();
        macro_expansion_depth--;
        return;
    }

    emit_source_line(raw_line, line_no);
}
