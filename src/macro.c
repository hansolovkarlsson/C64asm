/*
 * macro.c - see macro.h.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>
#include "macro.h"
#include "common.h"
#include "error.h"
#include "strutils.h"
#include "opcodes.h"
#include "lineparser.h"
#include "locallabels.h"
#include "includes.h"

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

/* A '.repeat'/'.dup' block currently being captured -- essentially an
 * anonymous, single-use MacroDef with zero or one parameter (the loop
 * index), expanded immediately at '.endrepeat'/'.enddup' rather than
 * deferred to a later separate invocation the way a named macro is.
 * See expand_repeat() below. */
typedef struct {
    long count;
    char index_name[MAX_IDENT];
    int has_index;
    char body[MAX_REPEAT_BODY_LINES][MAX_LINE_LEN];
    int body_line_count;
} RepeatCapture;

static RepeatCapture repeat_capture_storage;
static RepeatCapture *capturing_repeat = NULL;

/* A '.struct' block currently being captured -- its body lines are
 * kept as raw text, same as RepeatCapture's, and parsed for field
 * declarations only once '.endstruct' is reached. See
 * expand_struct() below. */
typedef struct {
    char name[MAX_IDENT];
    char body[MAX_REPEAT_BODY_LINES][MAX_LINE_LEN];
    int body_line_count;
} StructCapture;

static StructCapture struct_capture_storage;
static StructCapture *capturing_struct = NULL;

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

/* Parses '.repeat'/'.dup's count argument -- a single plain integer
 * literal (decimal, $hex, or %binary), deliberately NOT a full
 * expression: this runs during macro/include preprocessing, entirely
 * before pass 1 even starts building a symbol table, so there's no way
 * to look up a label or forward-declared constant here even if the
 * syntax allowed writing one. */
static long parse_repeat_count(const char *text, int line_no, const char *raw_line) {
    char t[MAX_LINE_LEN];
    strncpy(t, text, sizeof(t) - 1); t[sizeof(t) - 1] = '\0';
    trim(t);
    char *end = NULL;
    long n;
    if (t[0] == '$') {
        n = strtol(t + 1, &end, 16);
    } else if (t[0] == '%') {
        n = strtol(t + 1, &end, 2);
    } else {
        n = strtol(t, &end, 10);
    }
    if (t[0] == '\0' || end == NULL || *end != '\0' || end == t ||
        (t[0] == '$' && end == t + 1) || (t[0] == '%' && end == t + 1)) {
        asm_error(line_no, raw_line,
            "'.repeat'/'.dup' count must be a plain integer literal "
            "(decimal, $hex, or %%binary) -- symbols and expressions "
            "aren't available yet at this point in assembly, got '%s'", text);
    }
    if (n < 0)
        asm_error(line_no, raw_line, "'.repeat'/'.dup' count must not be negative (got %ld)", n);
    if (n > MAX_REPEAT_COUNT)
        asm_error(line_no, raw_line, "'.repeat'/'.dup' count %ld exceeds the maximum (%d)",
                  n, MAX_REPEAT_COUNT);
    return n;
}

/* Expands a captured '.repeat'/'.dup' block's body `count` times, in
 * order -- essentially an anonymous macro with zero or one parameter
 * (the loop index), immediately invoked that many times with the loop
 * index (0, 1, 2, ...) as the argument, reusing the exact same
 * macro_substitute() and per-invocation local-label scoping (see
 * locallabels.h) every ordinary macro invocation already gets. */
static void expand_repeat(RepeatCapture *r, const char *filename, int line_no) {
    macro_expansion_depth++;
    if (macro_expansion_depth > MAX_MACRO_EXPANSION_DEPTH)
        asm_error(line_no, NULL,
            "'.repeat'/'.dup' nested too deep (via a macro invocation "
            "inside its own body?)");

    MacroDef fake;   /* macro_substitute() needs a MacroDef* to look up
                         parameter names against -- build a throwaway
                         one-parameter (or zero-parameter) one here
                         rather than changing that function's signature
                         just for this caller */
    fake.param_count = 0;
    if (r->has_index) {
        strncpy(fake.params[0], r->index_name, sizeof(fake.params[0]) - 1);
        fake.params[0][sizeof(fake.params[0]) - 1] = '\0';
        fake.param_count = 1;
    }

    for (long i = 0; i < r->count; i++) {
        locallabels_push_scope();

        char arg[32];
        snprintf(arg, sizeof(arg), "%ld", i);
        char single_arg[1][MAX_LINE_LEN];
        char (*args)[MAX_LINE_LEN] = NULL;
        if (r->has_index) {
            strncpy(single_arg[0], arg, sizeof(single_arg[0]) - 1);
            single_arg[0][sizeof(single_arg[0]) - 1] = '\0';
            args = single_arg;
        }

        for (int bi = 0; bi < r->body_line_count; bi++) {
            char substituted[MAX_LINE_LEN];
            macro_substitute(r->body[bi], &fake, args, substituted, sizeof(substituted), line_no);
            macro_process_line(substituted, filename, line_no);
        }

        locallabels_pop_scope();
    }
    macro_expansion_depth--;
}

static int is_valid_ident(const char *s) {
    if (!s[0] || !is_ident_start(s[0])) return 0;
    for (const char *p = s + 1; *p; p++)
        if (!is_ident_char(*p)) return 0;
    return 1;
}

/* Generates one "StructName.field = offset" line and feeds it through
 * macro_process_line() -- the same '=' assignment pipeline an ordinary
 * hand-written constant goes through, so no separate symbol-table
 * code is needed here at all. */
static void emit_struct_field(const char *struct_name, const char *field_name, long offset,
                               const char *filename, int line_no, const char *raw_line) {
    if (!is_valid_ident(field_name))
        asm_error(line_no, raw_line, "'.struct %s': '%s' is not a valid field name",
                  struct_name, field_name);
    char synthetic[MAX_LINE_LEN];
    snprintf(synthetic, sizeof(synthetic), "%s.%s = %ld", struct_name, field_name, offset);
    macro_process_line(synthetic, filename, line_no);
}

/* Expands a captured '.struct' block's body into a set of
 * Name.field = offset symbol-assignment lines, one per declared
 * field, plus a final Name.size giving the whole struct's total byte
 * width. Nothing here emits any actual bytes or advances the
 * assembled program's own address -- a '.struct' block is purely a
 * compile-time source of named offsets, the same as a plain '='
 * constant is.
 *
 * line_no here is the '.endstruct' line's own number, used for errors
 * that aren't tied to one specific field declaration; a malformed
 * individual field declaration's error instead points at that
 * field's own line, using the body line's original text. */
static void expand_struct(StructCapture *s, const char *filename, int line_no) {
    long offset = 0;
    for (int bi = 0; bi < s->body_line_count; bi++) {
        char stripped[MAX_LINE_LEN];
        strncpy(stripped, s->body[bi], sizeof(stripped) - 1); stripped[sizeof(stripped)-1] = '\0';
        trim(stripped);
        if (stripped[0] == '\0') continue;

        char directive[MAX_IDENT];
        first_token(stripped, directive, sizeof(directive));
        char rest[MAX_LINE_LEN];
        strncpy(rest, stripped + strlen(directive), sizeof(rest) - 1); rest[sizeof(rest)-1] = '\0';
        trim(rest);

        char lower[MAX_IDENT];
        strncpy(lower, directive, sizeof(lower) - 1); lower[sizeof(lower)-1] = '\0';
        for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

        if (strcmp(lower, ".byte") == 0 || strcmp(lower, ".db") == 0 ||
            strcmp(lower, ".word") == 0 || strcmp(lower, ".dw") == 0) {
            int field_size = (strcmp(lower, ".byte") == 0 || strcmp(lower, ".db") == 0) ? 1 : 2;
            if (rest[0] == '\0')
                asm_error(line_no, stripped, "'.struct %s': '%s' requires at least one field name",
                          s->name, directive);
            char fields[MAX_ARGS][MAX_LINE_LEN];
            int nfields = split_args(rest, fields, MAX_ARGS);
            for (int fi = 0; fi < nfields; fi++) {
                trim(fields[fi]);
                emit_struct_field(s->name, fields[fi], offset, filename, line_no, stripped);
                offset += field_size;
            }
        } else if (strcmp(lower, ".res") == 0 || strcmp(lower, ".ds") == 0 || strcmp(lower, ".fill") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(rest, args, MAX_ARGS);
            if (nargs != 2)
                asm_error(line_no, stripped,
                    "'.struct %s': '%s' requires exactly a field name and a byte "
                    "count, e.g. '.res buf, 16'", s->name, directive);
            trim(args[0]); trim(args[1]);
            long count = parse_repeat_count(args[1], line_no, stripped);
            /* parse_repeat_count()'s own restriction to a plain integer
             * literal (no symbols/expressions) applies here too, and for
             * the same reason: struct field offsets, like a .repeat
             * count, need to be known during this same preprocessing
             * pass, before pass 1 builds a symbol table. */
            emit_struct_field(s->name, args[0], offset, filename, line_no, stripped);
            offset += count;
        } else {
            asm_error(line_no, stripped,
                "'.struct %s': '%s' is not a valid field declaration -- "
                "expected .byte, .word, or .res", s->name, directive);
        }
    }
    emit_struct_field(s->name, "size", offset, filename, line_no, NULL);
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
static void emit_source_line(const char *raw_line, const char *filename, int line_no) {
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
    strncpy(g_lines[g_line_count].filename, filename ? filename : "", MAX_FILENAME_LEN - 1);
    g_lines[g_line_count].filename[MAX_FILENAME_LEN - 1] = '\0';
    g_line_count++;
}

void macro_process_line(const char *raw_line, const char *filename, int line_no) {
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
        if (strcasecmp(first, ".repeat") == 0 || strcasecmp(first, ".dup") == 0)
            asm_error(line_no, raw_line,
                      "'.repeat'/'.dup' cannot appear inside a '.macro' body "
                      "-- define the macro first, then use '.repeat' to "
                      "invoke it, if that's what you need");
        if (strcasecmp(first, ".struct") == 0)
            asm_error(line_no, raw_line,
                      "'.struct' cannot appear inside a '.macro' body -- "
                      "define it before the '.macro' instead");
        if (capturing_macro->body_line_count >= MAX_MACRO_BODY_LINES)
            asm_error(line_no, raw_line, "Macro '%s' body too long (max %d lines)",
                      capturing_macro->name, MAX_MACRO_BODY_LINES);
        strncpy(capturing_macro->body[capturing_macro->body_line_count], line, MAX_LINE_LEN - 1);
        capturing_macro->body[capturing_macro->body_line_count][MAX_LINE_LEN-1] = '\0';
        capturing_macro->body_line_count++;
        return;
    }

    if (capturing_repeat != NULL) {
        char first[MAX_IDENT];
        first_token(trimmed, first, sizeof(first));
        if (strcasecmp(first, ".endrepeat") == 0 || strcasecmp(first, ".enddup") == 0) {
            RepeatCapture *r = capturing_repeat;
            capturing_repeat = NULL;
            expand_repeat(r, filename, line_no);
            return;
        }
        if (strcasecmp(first, ".repeat") == 0 || strcasecmp(first, ".dup") == 0)
            asm_error(line_no, raw_line, "nested '.repeat'/'.dup' blocks are not supported");
        if (strcasecmp(first, ".macro") == 0)
            asm_error(line_no, raw_line,
                      "'.macro' cannot be defined inside a '.repeat'/'.dup' "
                      "block -- define it before the '.repeat' instead");
        if (strcasecmp(first, ".struct") == 0)
            asm_error(line_no, raw_line,
                      "'.struct' cannot appear inside a '.repeat'/'.dup' "
                      "block -- define it before the '.repeat' instead");
        if (capturing_repeat->body_line_count >= MAX_REPEAT_BODY_LINES)
            asm_error(line_no, raw_line, "'.repeat'/'.dup' body too long (max %d lines)",
                      MAX_REPEAT_BODY_LINES);
        strncpy(capturing_repeat->body[capturing_repeat->body_line_count], line, MAX_LINE_LEN - 1);
        capturing_repeat->body[capturing_repeat->body_line_count][MAX_LINE_LEN-1] = '\0';
        capturing_repeat->body_line_count++;
        return;
    }

    if (capturing_struct != NULL) {
        char first[MAX_IDENT];
        first_token(trimmed, first, sizeof(first));
        if (strcasecmp(first, ".endstruct") == 0) {
            StructCapture *s = capturing_struct;
            capturing_struct = NULL;
            expand_struct(s, filename, line_no);
            return;
        }
        if (strcasecmp(first, ".struct") == 0)
            asm_error(line_no, raw_line,
                      "nested '.struct' definitions are not supported "
                      "(already defining '%s')", capturing_struct->name);
        if (strcasecmp(first, ".macro") == 0 || strcasecmp(first, ".repeat") == 0 ||
            strcasecmp(first, ".dup") == 0)
            asm_error(line_no, raw_line,
                      "'%s' cannot appear inside a '.struct' body -- only "
                      "field declarations (.byte, .word, .res) are allowed there",
                      first);
        if (capturing_struct->body_line_count >= MAX_REPEAT_BODY_LINES)
            asm_error(line_no, raw_line, "'.struct' body too long (max %d lines)",
                      MAX_REPEAT_BODY_LINES);
        strncpy(capturing_struct->body[capturing_struct->body_line_count], line, MAX_LINE_LEN - 1);
        capturing_struct->body[capturing_struct->body_line_count][MAX_LINE_LEN-1] = '\0';
        capturing_struct->body_line_count++;
        return;
    }

    if (trimmed[0] == '\0') {
        emit_source_line(raw_line, filename, line_no);
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

    if (strcasecmp(first, ".repeat") == 0 || strcasecmp(first, ".dup") == 0) {
        char rest[MAX_LINE_LEN];
        strncpy(rest, trimmed + strlen(first), sizeof(rest)-1); rest[sizeof(rest)-1]='\0';
        trim(rest);
        if (rest[0] == '\0')
            asm_error(line_no, raw_line,
                "'.repeat'/'.dup' requires a count, e.g. '.repeat 16' or '.repeat 16, i'");
        char pargs[MAX_ARGS][MAX_LINE_LEN];
        int nargs = split_args(rest, pargs, MAX_ARGS);
        if (nargs > 2)
            asm_error(line_no, raw_line,
                "'.repeat'/'.dup' takes at most a count and an index name "
                "(e.g. '.repeat 16, i'), got too many arguments");
        repeat_capture_storage.count = parse_repeat_count(pargs[0], line_no, raw_line);
        repeat_capture_storage.has_index = (nargs > 1);
        if (nargs > 1) {
            char idx[MAX_LINE_LEN];
            strncpy(idx, pargs[1], sizeof(idx)-1); idx[sizeof(idx)-1]='\0';
            trim(idx);
            strncpy(repeat_capture_storage.index_name, idx,
                    sizeof(repeat_capture_storage.index_name) - 1);
            repeat_capture_storage.index_name[sizeof(repeat_capture_storage.index_name) - 1] = '\0';
        }
        repeat_capture_storage.body_line_count = 0;
        capturing_repeat = &repeat_capture_storage;
        return;
    }

    if (strcasecmp(first, ".endrepeat") == 0 || strcasecmp(first, ".enddup") == 0)
        asm_error(line_no, raw_line, "'.endrepeat'/'.enddup' with no matching '.repeat'/'.dup'");

    if (strcasecmp(first, ".struct") == 0) {
        char rest[MAX_LINE_LEN];
        strncpy(rest, trimmed + strlen(first), sizeof(rest)-1); rest[sizeof(rest)-1]='\0';
        trim(rest);
        if (rest[0] == '\0')
            asm_error(line_no, raw_line, "'.struct' requires a name, e.g. '.struct Room'");
        char sname[MAX_LINE_LEN];
        size_t sp = 0;
        while (rest[sp] && !isspace((unsigned char)rest[sp])) sp++;
        memcpy(sname, rest, sp); sname[sp] = '\0';
        if (!is_valid_ident(sname))
            asm_error(line_no, raw_line, "'%s' is not a valid struct name", sname);
        char extra[MAX_LINE_LEN];
        strncpy(extra, rest + sp, sizeof(extra)-1); extra[sizeof(extra)-1]='\0';
        trim(extra);
        if (extra[0] != '\0')
            asm_error(line_no, raw_line,
                "'.struct' takes only a name -- field declarations go on "
                "their own lines, between '.struct' and '.endstruct'");
        strncpy(struct_capture_storage.name, sname, sizeof(struct_capture_storage.name) - 1);
        struct_capture_storage.name[sizeof(struct_capture_storage.name) - 1] = '\0';
        struct_capture_storage.body_line_count = 0;
        capturing_struct = &struct_capture_storage;
        return;
    }

    if (strcasecmp(first, ".endstruct") == 0)
        asm_error(line_no, raw_line, "'.endstruct' with no matching '.struct'");

    if (strcasecmp(first, ".include") == 0) {
        char rest[MAX_LINE_LEN];
        strncpy(rest, trimmed + strlen(first), sizeof(rest)-1); rest[sizeof(rest)-1]='\0';
        trim(rest);
        size_t rlen = strlen(rest);
        if (rlen < 2 || rest[0] != '"' || rest[rlen-1] != '"')
            asm_error(line_no, raw_line, ".include requires a quoted path, e.g. .include \"lib.inc\"");
        rest[rlen-1] = '\0';
        char *path = rest + 1;
        asm_error_note_include_used();
        includes_process_file(path, filename, line_no, raw_line, macro_process_line);
        return;
    }

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
            macro_process_line(substituted, filename, line_no);
        }

        locallabels_pop_scope();
        macro_expansion_depth--;
        return;
    }

    emit_source_line(raw_line, filename, line_no);
}
