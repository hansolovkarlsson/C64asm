/*
 * lineparser.c - see lineparser.h.
 */

#include <string.h>
#include <ctype.h>
#include <strings.h>
#include "lineparser.h"
#include "opcodes.h"
#include "strutils.h"
#include "error.h"

SourceLine *g_lines;
int g_line_count = 0;

/* Removes a ';' comment from `line`, in place, stopping at the first
 * ';' that isn't inside a double-quoted string -- so a semicolon inside
 * a .text "..." string doesn't get mistaken for the start of a comment.
 * Not exposed via lineparser.h; only split_line() below needs it. */
static void strip_comment(char *line) {
    int in_str = 0;
    size_t n = strlen(line);
    for (size_t i = 0; i < n; i++) {
        if (line[i] == '"') in_str = !in_str;
        if (line[i] == ';' && !in_str) { line[i] = '\0'; return; }
    }
}

/* Copies the identifier characters starting at s[pos] into `out` (up to
 * outsz-1 of them) and returns the position just past the end of the
 * identifier. Used only while picking a label or the "identifier" side
 * of "identifier = expr" apart from what follows it. */
static size_t scan_ident(const char *s, size_t pos, char *out, size_t outsz) {
    size_t j = pos;
    size_t n = 0;
    while (s[j] && is_ident_char(s[j])) {
        if (n + 1 < outsz) out[n++] = s[j];
        j++;
    }
    out[n] = '\0';
    return j;
}

void split_line(const char *raw_line, int line_no, SourceLine *out) {
    char line[MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    strip_comment(line);
    /* strip trailing newline/CR left over from fgets() */
    size_t ln = strlen(line);
    while (ln > 0 && (line[ln-1] == '\n' || line[ln-1] == '\r')) line[--ln] = '\0';

    out->line_no = line_no;
    strncpy(out->raw, raw_line, sizeof(out->raw) - 1);
    out->raw[sizeof(out->raw) - 1] = '\0';
    out->has_label = 0;
    out->has_op = 0;
    out->label[0] = '\0';
    out->op[0] = '\0';
    out->operand[0] = '\0';

    char stripped[MAX_LINE_LEN];
    strncpy(stripped, line, sizeof(stripped) - 1);
    stripped[sizeof(stripped) - 1] = '\0';
    trim(stripped);

    if (stripped[0] == '\0') return; /* blank line */

    /* '*=' / '* =' org directive. Checked first and specially because
     * '*' isn't a legal identifier character, so this can't be
     * confused with any of the label/mnemonic forms below. */
    if (stripped[0] == '*') {
        size_t i = 1;
        while (stripped[i] == ' ' || stripped[i] == '\t') i++;
        if (stripped[i] == '=') {
            char rhs[MAX_LINE_LEN];
            strncpy(rhs, stripped + i + 1, sizeof(rhs) - 1);
            rhs[sizeof(rhs) - 1] = '\0';
            trim(rhs);
            out->has_op = 1;
            strncpy(out->op, ".org", sizeof(out->op) - 1);
            strncpy(out->operand, rhs, sizeof(out->operand) - 1);
            return;
        }
    }

    /* "identifier = expr" constant assignment. Checked before the
     * general label/mnemonic handling below because an identifier
     * followed by '=' is unambiguous -- no mnemonic or directive name
     * is ever followed directly by '=', so there's no case where this
     * could misfire on something that was meant to be a label or
     * instruction instead. */
    if (is_ident_start(stripped[0])) {
        char ident[MAX_IDENT];
        size_t after = scan_ident(stripped, 0, ident, sizeof(ident));
        size_t i = after;
        while (stripped[i] == ' ' || stripped[i] == '\t') i++;
        if (stripped[i] == '=' && stripped[i+1] != '=') {
            char rhs[MAX_LINE_LEN];
            strncpy(rhs, stripped + i + 1, sizeof(rhs) - 1);
            rhs[sizeof(rhs) - 1] = '\0';
            trim(rhs);
            out->has_label = 1;
            strncpy(out->label, ident, sizeof(out->label) - 1);
            out->has_op = 1;
            strncpy(out->op, "=", sizeof(out->op) - 1);
            strncpy(out->operand, rhs, sizeof(out->operand) - 1);
            return;
        }
    }

    char rest[MAX_LINE_LEN];
    strncpy(rest, stripped, sizeof(rest) - 1);
    rest[sizeof(rest) - 1] = '\0';

    /* "label:" form, or the ambiguous "bare word at the start of the
     * line" case -- see the big comment in lineparser.h for how the
     * ambiguous case is resolved. */
    if (is_ident_start(rest[0])) {
        char ident[MAX_IDENT];
        size_t after = scan_ident(rest, 0, ident, sizeof(ident));
        if (rest[after] == ':') {
            /* Unambiguous: the colon settles it, this is a label. */
            out->has_label = 1;
            strncpy(out->label, ident, sizeof(out->label) - 1);
            char remainder[MAX_LINE_LEN];
            strncpy(remainder, rest + after + 1, sizeof(remainder) - 1);
            remainder[sizeof(remainder) - 1] = '\0';
            trim(remainder);
            strncpy(rest, remainder, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = '\0';
        } else {
            /* No colon: `ident` could be a label with no colon,
             * *or* it could itself be the mnemonic/directive for this
             * line (e.g. a bare "RTS" with nothing before it). Ask the
             * opcode table and directive list which one it is. */
            char remainder[MAX_LINE_LEN];
            size_t k = after;
            while (rest[k] == ' ' || rest[k] == '\t') k++;
            strncpy(remainder, rest + k, sizeof(remainder) - 1);
            remainder[sizeof(remainder) - 1] = '\0';
            trim(remainder);

            int ident_is_op = (find_mnemonic(ident) != NULL) || is_directive(ident);
            if (!ident_is_op) {
                /* `ident` isn't a known mnemonic/directive, so it must
                 * be a label. If nothing follows it, it's a bare label
                 * line (just marking an address, no instruction here).
                 * Otherwise, whatever comes after it had better itself
                 * be a real mnemonic or directive -- if it isn't,
                 * that's an error, not a silent guess. */
                if (remainder[0] == '\0') {
                    out->has_label = 1;
                    strncpy(out->label, ident, sizeof(out->label) - 1);
                    return; /* bare label line */
                }
                char first_tok[MAX_IDENT];
                size_t sp = 0;
                while (remainder[sp] && !isspace((unsigned char)remainder[sp])) sp++;
                size_t cn = sp < sizeof(first_tok) - 1 ? sp : sizeof(first_tok) - 1;
                memcpy(first_tok, remainder, cn); first_tok[cn] = '\0';
                if (find_mnemonic(first_tok) != NULL || is_directive(first_tok) ||
                    first_tok[0] == '.') {
                    out->has_label = 1;
                    strncpy(out->label, ident, sizeof(out->label) - 1);
                    strncpy(rest, remainder, sizeof(rest) - 1);
                    rest[sizeof(rest) - 1] = '\0';
                } else {
                    asm_error(line_no, raw_line, "Unknown mnemonic or directive '%s'", ident);
                }
            }
            /* else: ident itself is the op; leave rest as the full
             * stripped line, to be split into op + operand below. */
        }
    }
    /* (a line starting with '.', i.e. a directive with no label, falls
     * through to the generic op/operand split below unchanged) */

    if (rest[0] == '\0') return;

    /* Split whatever's left into the op token and its operand, on the
     * first whitespace. */
    size_t sp = 0;
    while (rest[sp] && !isspace((unsigned char)rest[sp])) sp++;
    char op_tok[MAX_IDENT];
    size_t cn = sp < sizeof(op_tok) - 1 ? sp : sizeof(op_tok) - 1;
    memcpy(op_tok, rest, cn); op_tok[cn] = '\0';
    char operand[MAX_LINE_LEN];
    strncpy(operand, rest + sp, sizeof(operand) - 1);
    operand[sizeof(operand) - 1] = '\0';
    trim(operand);

    if (strcasecmp(op_tok, ".equ") == 0) {
        /* .equ is just an alternate spelling of "label = expr" --
         * normalize it to the same internal representation ("=") so
         * assembler.c only ever has to handle one case. */
        out->has_op = 1;
        strncpy(out->op, "=", sizeof(out->op) - 1);
        strncpy(out->operand, operand, sizeof(out->operand) - 1);
        return;
    }

    OpcodeEntry *e = find_mnemonic(op_tok);
    if (e) {
        out->has_op = 1;
        /* Normalize to uppercase so assembler.c can compare against
         * "LDA" etc. with a simple strcmp() rather than strcasecmp()
         * everywhere -- the case-insensitivity is handled once, here,
         * rather than repeatedly at every later comparison site. */
        strncpy(out->op, e->mnemonic, sizeof(out->op) - 1);
        for (char *pc2 = out->op; *pc2; pc2++) *pc2 = (char)toupper((unsigned char)*pc2);
        strncpy(out->operand, operand, sizeof(out->operand) - 1);
        return;
    }
    if (is_directive(op_tok)) {
        out->has_op = 1;
        /* Same idea, but normalized to lowercase, matching how
         * assembler.c compares directive names (".byte", ".org", etc). */
        char lower[MAX_IDENT];
        strncpy(lower, op_tok, sizeof(lower) - 1); lower[sizeof(lower)-1]='\0';
        for (char *pc2 = lower; *pc2; pc2++) *pc2 = (char)tolower((unsigned char)*pc2);
        strncpy(out->op, lower, sizeof(out->op) - 1);
        strncpy(out->operand, operand, sizeof(out->operand) - 1);
        return;
    }

    asm_error(line_no, raw_line, "Unknown mnemonic or directive '%s'", op_tok);
}
