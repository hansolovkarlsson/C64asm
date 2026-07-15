/*
 * operand.c - see operand.h.
 */

#include <string.h>
#include <ctype.h>
#include "operand.h"
#include "common.h"
#include "error.h"
#include "strutils.h"
#include "expr.h"

/* True if expr_text is a hex literal ($....) written with more than two
 * digits, e.g. "$0050". Such a literal is always treated as absolute
 * addressing even though its numeric value (here, 80) would otherwise
 * fit in a single byte and qualify for the shorter zero-page encoding
 * -- this is how you force absolute addressing when you specifically
 * want it, by simply writing the address with its full width. Not
 * exposed via operand.h; only parse_operand() below needs it. */
static int looks_forced_absolute(const char *expr_text) {
    char t[MAX_LINE_LEN];
    strncpy(t, expr_text, sizeof(t) - 1); t[sizeof(t)-1]='\0';
    trim(t);
    if (t[0] == '$') return (int)strlen(t) - 1 > 2;
    return 0;
}

/* Finds the ')' that matches the '(' at s[open_idx], correctly
 * accounting for nested parentheses inside expressions like
 * "((a+b))," -- returns its index, or -1 if there isn't a matching one.
 * Not exposed via operand.h; only parse_operand() below needs it. */
static int find_matching_paren(const char *s, int open_idx) {
    int depth = 0;
    int n = (int)strlen(s);
    for (int i = open_idx; i < n; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

/*
 * See operand.h for the overall contract. This function is one long
 * sequence of "does the operand text look like *this* addressing
 * mode's syntax? If so, handle it and return" checks, roughly in this
 * order:
 *
 *   (empty)          -> implied            RTS
 *   "A"               -> accumulator        ASL A
 *   (any, if branch)  -> relative           BNE loop
 *   "#expr"           -> immediate          LDA #$10
 *   "(expr,X)"        -> indexed indirect   LDA ($10,X)
 *   "(expr),Y"        -> indirect indexed   LDA ($10),Y
 *   "(expr)"          -> indirect           JMP ($1000)
 *   "expr,X"/"expr,Y" -> zero page or absolute, indexed
 *   "expr"            -> zero page or absolute, plain
 *
 * Whenever there's a choice between zero page and absolute for the same
 * expression, the shorter zero-page form wins automatically if the
 * expression's value is already known (not a forward reference) and
 * fits in a byte -- see the is_zp checks below, and
 * looks_forced_absolute() above for how to opt out of that.
 */
Mode parse_operand(const char *mnemonic, const char *operand_in,
                    long pc, int line_no, const char *raw,
                    long *val_out, int *undef_out) {
    OpcodeEntry *e = find_mnemonic(mnemonic);
    char op[MAX_LINE_LEN];
    strncpy(op, operand_in, sizeof(op) - 1); op[sizeof(op)-1] = '\0';
    trim(op);

    *val_out = 0; *undef_out = 0;

    if (op[0] == '\0') {
        if (e->op[M_IMP] != -1) return M_IMP;
        asm_error(line_no, raw, "%s requires an operand", mnemonic);
    }

    if ((op[0]=='A'||op[0]=='a') && op[1]=='\0' && e->op[M_ACC] != -1) {
        return M_ACC;
    }

    /* Branches always use relative addressing -- the operand is just
     * "the target address", evaluated normally; assembler.c is where
     * that address later gets turned into a signed 8-bit offset and
     * range-checked. */
    if (is_branch_mnemonic(mnemonic)) {
        *val_out = eval_expr(op, pc, line_no, undef_out);
        return M_REL;
    }

    if (op[0] == '#') {
        char inner[MAX_LINE_LEN];
        strncpy(inner, op + 1, sizeof(inner) - 1); inner[sizeof(inner)-1]='\0';
        trim(inner);
        *val_out = eval_expr(inner, pc, line_no, undef_out);
        return M_IMM;
    }

    if (op[0] == '(') {
        int close = find_matching_paren(op, 0);
        if (close < 0) asm_error(line_no, raw, "Unbalanced parentheses in operand '%s'", op);
        int oplen = (int)strlen(op);
        if (close == oplen - 1) {
            /* The whole operand is wrapped in one pair of parens, so
             * this is either "(expr,X)" (indexed indirect) or a plain
             * "(expr)" (indirect, JMP only) -- distinguished by whether
             * the text just inside the closing paren ends in ",X". */
            char inner[MAX_LINE_LEN];
            int ilen = close - 1;
            if (ilen < 0) ilen = 0;
            memcpy(inner, op + 1, ilen); inner[ilen] = '\0';
            trim(inner);
            size_t n = strlen(inner);
            if (n >= 2 && (inner[n-1]=='X' || inner[n-1]=='x')) {
                size_t p2 = n - 1;
                while (p2 > 0 && isspace((unsigned char)inner[p2-1])) p2--;
                if (p2 > 0 && inner[p2-1] == ',') {
                    char expr[MAX_LINE_LEN];
                    size_t elen = p2 - 1;
                    memcpy(expr, inner, elen); expr[elen] = '\0';
                    trim(expr);
                    if (e->op[M_INDX] == -1)
                        asm_error(line_no, raw, "%s does not support (zp,X) addressing", mnemonic);
                    *val_out = eval_expr(expr, pc, line_no, undef_out);
                    return M_INDX;
                }
            }
            /* Not ",X)" -- must be plain indirect, e.g. JMP ($1000). */
            if (e->op[M_IND] == -1)
                asm_error(line_no, raw, "%s does not support indirect addressing", mnemonic);
            *val_out = eval_expr(inner, pc, line_no, undef_out);
            return M_IND;
        } else {
            /* The matching ')' isn't the last character, so there's a
             * suffix after it -- the only valid one is ",Y", giving
             * indirect indexed addressing: "(expr),Y". */
            char suffix[MAX_LINE_LEN];
            strncpy(suffix, op + close + 1, sizeof(suffix) - 1);
            suffix[sizeof(suffix)-1] = '\0';
            trim(suffix);
            if (suffix[0] == ',' ) {
                char reg[MAX_LINE_LEN];
                strncpy(reg, suffix + 1, sizeof(reg) - 1); reg[sizeof(reg)-1]='\0';
                trim(reg);
                if ((reg[0]=='Y'||reg[0]=='y') && reg[1]=='\0') {
                    char inner[MAX_LINE_LEN];
                    int ilen = close - 1;
                    if (ilen < 0) ilen = 0;
                    memcpy(inner, op + 1, ilen); inner[ilen] = '\0';
                    trim(inner);
                    if (e->op[M_INDY] == -1)
                        asm_error(line_no, raw, "%s does not support (zp),Y addressing", mnemonic);
                    *val_out = eval_expr(inner, pc, line_no, undef_out);
                    return M_INDY;
                }
            }
            asm_error(line_no, raw, "%s does not support that addressing mode ('%s')", mnemonic, op);
        }
    }

    /* Doesn't start with '(' or '#' -- check for a trailing ",X" or
     * ",Y" (indexed zero-page/absolute) before falling through to plain
     * zero-page/absolute below. */
    {
        size_t n = strlen(op);
        size_t end = n;
        while (end > 0 && isspace((unsigned char)op[end-1])) end--;
        if (end > 0) {
            char reg = op[end-1];
            if (reg=='X'||reg=='x'||reg=='Y'||reg=='y') {
                size_t p2 = end - 1;
                while (p2 > 0 && isspace((unsigned char)op[p2-1])) p2--;
                if (p2 > 0 && op[p2-1] == ',') {
                    char expr[MAX_LINE_LEN];
                    size_t elen = p2 - 1;
                    memcpy(expr, op, elen); expr[elen] = '\0';
                    trim(expr);
                    long v = eval_expr(expr, pc, line_no, undef_out);
                    *val_out = v;
                    /* Prefer the shorter zero-page,X/Y encoding when the
                     * value is both known (not a forward reference) and
                     * small enough to fit -- unless the source forced
                     * absolute with a 4-digit-or-longer hex literal. */
                    int is_zp = (!*undef_out) && v <= 0xFF && !looks_forced_absolute(expr);
                    int is_x = (reg=='X'||reg=='x');
                    if (is_x) {
                        if (is_zp && e->op[M_ZPX] != -1) return M_ZPX;
                        if (e->op[M_ABSX] != -1) return M_ABSX;
                        if (e->op[M_ZPX] != -1) return M_ZPX;
                    } else {
                        if (is_zp && e->op[M_ZPY] != -1) return M_ZPY;
                        if (e->op[M_ABSY] != -1) return M_ABSY;
                        if (e->op[M_ZPY] != -1) return M_ZPY;
                    }
                    asm_error(line_no, raw, "%s does not support that addressing mode", mnemonic);
                }
            }
        }
    }

    /* Plain expression, no indexing or indirection at all -> zero page
     * if it fits and isn't forced absolute, otherwise absolute. */
    {
        long v = eval_expr(op, pc, line_no, undef_out);
        *val_out = v;
        int is_zp = (!*undef_out) && v <= 0xFF && !looks_forced_absolute(op);
        if (is_zp && e->op[M_ZP] != -1) return M_ZP;
        if (e->op[M_ABS] != -1) return M_ABS;
        if (e->op[M_ZP] != -1) return M_ZP;
        asm_error(line_no, raw, "%s does not support that addressing mode", mnemonic);
    }
    return M_IMP; /* unreachable -- asm_error() above always exits */
}
