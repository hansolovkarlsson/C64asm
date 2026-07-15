/*
 * expr.c - see expr.h for the big picture. This file has two parts:
 * a tokenizer (tokenize_expr, turning text into a flat array of Tokens)
 * and a recursive-descent parser/evaluator (parse_expr and friends,
 * turning that token array into a number).
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "expr.h"
#include "common.h"
#include "error.h"
#include "strutils.h"
#include "symtab.h"

typedef enum { TK_HEX, TK_BIN, TK_DEC, TK_CHAR, TK_IDENT, TK_OP, TK_END } TokKind;

typedef struct {
    TokKind kind;
    char text[MAX_IDENT];
} Token;

/* All the state one call to eval_expr() needs to carry around: the
 * token array itself, a read position into it, and a few pieces of
 * context (pc, line number, the original text for error messages, and
 * the "did we see an undefined symbol" flag) that every parsing
 * function below needs access to. Bundling them into one struct that
 * gets passed by pointer everywhere is a common C pattern for this --
 * the alternative would be a pile of global variables, which would work
 * for a single-threaded command-line tool like this one, but would
 * make it impossible to ever evaluate two expressions "at once" (e.g.
 * if this code were ever reused inside something like a language
 * server that evaluates expressions on demand while a user types). */
typedef struct {
    Token toks[MAX_TOKENS];
    int ntoks;
    int pos;
    long pc;
    int line_no;
    int undefined;
    char err_text[MAX_LINE_LEN];
} EParser;

/*
 * Splits `text` into a flat array of tokens (numbers, identifiers,
 * operators). This is the "lexer" or "scanner" stage that almost every
 * parser has as a first step, separate from actually understanding the
 * grammar -- its only job is "what are the individual pieces", not
 * "what do they mean together".
 *
 * Notably, '*' is tokenized the same way regardless of whether it means
 * multiplication or "current PC" -- both are just TK_OP with text "*".
 * Telling them apart is the *parser's* job (see parse_atom() below),
 * not the tokenizer's, because it depends on where the '*' appears
 * relative to other tokens, not on the character itself. This was a
 * real, shipped bug at one point in this project's history: an earlier
 * version gave '*' its own token kind meant only for "current PC",
 * which meant the parser could never recognize it as multiplication at
 * all -- every expression using '*' to multiply silently produced a
 * "trailing text" error instead.
 */
static void tokenize_expr(const char *text, EParser *p) {
    p->ntoks = 0;
    const char *s = text;
    size_t i = 0, len = strlen(text);
    while (i < len) {
        char c = s[i];
        if (isspace((unsigned char)c)) { i++; continue; }
        Token t;
        if (c == '$') {
            size_t j = i + 1;
            while (j < len && isxdigit((unsigned char)s[j])) j++;
            if (j == i + 1) asm_error(p->line_no, text, "Bad hex literal in expression '%s'", text);
            size_t n = j - i; if (n >= MAX_IDENT) n = MAX_IDENT - 1;
            memcpy(t.text, s + i, n); t.text[n] = '\0';
            t.kind = TK_HEX; i = j;
        } else if (c == '%') {
            size_t j = i + 1;
            while (j < len && (s[j] == '0' || s[j] == '1')) j++;
            if (j == i + 1) asm_error(p->line_no, text, "Bad binary literal in expression '%s'", text);
            size_t n = j - i; if (n >= MAX_IDENT) n = MAX_IDENT - 1;
            memcpy(t.text, s + i, n); t.text[n] = '\0';
            t.kind = TK_BIN; i = j;
        } else if (isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < len && isdigit((unsigned char)s[j])) j++;
            size_t n = j - i; if (n >= MAX_IDENT) n = MAX_IDENT - 1;
            memcpy(t.text, s + i, n); t.text[n] = '\0';
            t.kind = TK_DEC; i = j;
        } else if (c == '\'') {
            size_t j = i + 1;
            char inner = '\0';   /* asm_error() below never actually returns
                                     (it always exit()s), but the compiler
                                     has no way to know that -- initializing
                                     this avoids a spurious "used
                                     uninitialized" warning on the
                                     technically-unreachable fallthrough path */
            if (j < len && s[j] == '\\' && j + 1 < len) { inner = s[j+1]; j += 2; }
            else if (j < len) { inner = s[j]; j += 1; }
            else asm_error(p->line_no, text, "Bad character literal in expression '%s'", text);
            if (j >= len || s[j] != '\'')
                asm_error(p->line_no, text, "Unterminated character literal in expression '%s'", text);
            j++;
            t.text[0] = inner; t.text[1] = '\0';
            t.kind = TK_CHAR; i = j;
        } else if (is_ident_start(c)) {
            size_t j = i;
            while (j < len && (is_ident_char(s[j]) || s[j] == '.')) j++;
            size_t n = j - i; if (n >= MAX_IDENT) n = MAX_IDENT - 1;
            memcpy(t.text, s + i, n); t.text[n] = '\0';
            t.kind = TK_IDENT; i = j;
        } else if (c == '(' || c == ')' || c == '+' || c == '-' || c == '/' ||
                   c == '<' || c == '>' || c == '*') {
            t.kind = TK_OP; t.text[0] = c; t.text[1] = '\0'; i++;
        } else {
            asm_error(p->line_no, text, "Bad character '%c' in expression '%s'", c, text);
            return;
        }
        if (p->ntoks >= MAX_TOKENS)
            asm_error(p->line_no, text, "Expression too complex '%s'", text);
        p->toks[p->ntoks++] = t;
    }
}

/* peek() looks at the current token without consuming it; next()
 * consumes and returns it, advancing the read position. This
 * peek-then-decide-then-maybe-consume pattern is the core mechanic of
 * every function below -- a recursive-descent parser is fundamentally
 * just "look at what's next, and based on that, decide which grammar
 * rule (i.e. which function) to call". */
static Token *ep_peek(EParser *p) {
    static Token end_tok = { TK_END, "" };
    if (p->pos < p->ntoks) return &p->toks[p->pos];
    return &end_tok;
}
static Token *ep_next(EParser *p) {
    Token *t = ep_peek(p);
    if (p->pos < p->ntoks) p->pos++;
    return t;
}

static long parse_expr(EParser *p);

/*
 * parse_atom: the innermost grammar rule -- a single, indivisible value
 * with no operators of its own. One of: a number literal (hex/binary/
 * decimal/character), a symbol name, the current PC ('*'), or a fully
 * parenthesized sub-expression.
 *
 * The '*'-means-current-PC case only fires here, in atom position --
 * i.e. only when the parser specifically needs one indivisible value
 * and finds a bare '*' sitting there. An infix '*' meaning multiply is
 * consumed instead by parse_term()'s loop, *before* control ever
 * reaches this function for that token; a lone '*' can only end up here
 * if there's no left-hand operand for it to multiply against, which is
 * exactly the situation where "current PC" is the only sensible
 * reading. Working out this distinction by *position in the grammar*,
 * rather than trying to have the tokenizer guess the meaning up front,
 * is the fix for the multiplication bug mentioned in tokenize_expr()'s
 * comment above.
 */
static long parse_atom(EParser *p) {
    Token *t = ep_next(p);
    switch (t->kind) {
        case TK_HEX: return strtol(t->text + 1, NULL, 16);
        case TK_BIN: return strtol(t->text + 1, NULL, 2);
        case TK_DEC: return strtol(t->text, NULL, 10);
        case TK_CHAR: return (long)(unsigned char)t->text[0];
        case TK_IDENT: {
            Symbol *s = find_symbol(t->text);
            if (s) return s->value;
            p->undefined = 1;
            return 0;  /* placeholder value; see expr.h's note on undefined_out */
        }
        case TK_OP:
            if (t->text[0] == '*') {
                return p->pc;
            }
            if (t->text[0] == '(') {
                long v = parse_expr(p);
                Token *close = ep_next(p);
                if (!(close->kind == TK_OP && close->text[0] == ')'))
                    asm_error(p->line_no, p->err_text, "Missing ')' in expression '%s'", p->err_text);
                return v;
            }
            /* fallthrough to error */
            break;
        default: break;
    }
    asm_error(p->line_no, p->err_text, "Cannot parse expression '%s'", p->err_text);
    return 0;
}

/*
 * parse_unary: handles the three prefix (unary) operators -- negation
 * (-x), low byte (<x), and high byte (>x) -- then falls through to
 * parse_atom() for anything that isn't one of those. Unary operators
 * bind tighter than any binary operator (so "-x + 1" is "(-x) + 1", not
 * "-(x + 1)"), which is why this sits *between* parse_term() and
 * parse_atom() in the call chain below, rather than being folded into
 * either of them.
 */
static long parse_unary(EParser *p) {
    Token *t = ep_peek(p);
    if (t->kind == TK_OP && t->text[0] == '-') { ep_next(p); return -parse_unary(p); }
    if (t->kind == TK_OP && t->text[0] == '<') { ep_next(p); return parse_unary(p) & 0xFF; }
    if (t->kind == TK_OP && t->text[0] == '>') { ep_next(p); return (parse_unary(p) >> 8) & 0xFF; }
    return parse_atom(p);
}

/*
 * parse_term: multiplication and division, left-associative. This is
 * where operator *precedence* actually comes from in a recursive-
 * descent parser: parse_term() is built out of parse_unary() calls, and
 * is itself one layer *inside* parse_expr() (addition/subtraction)
 * below -- so by construction, "2 + 3*4" always groups as "2 + (3*4)"
 * and never "(2+3) * 4", without the code ever explicitly comparing
 * operator priorities. Each precedence level is just another rung in
 * the call ladder; the grammar *is* the precedence table.
 */
static long parse_term(EParser *p) {
    long v = parse_unary(p);
    for (;;) {
        Token *t = ep_peek(p);
        if (t->kind == TK_OP && t->text[0] == '*') { ep_next(p); v = v * parse_unary(p); }
        else if (t->kind == TK_OP && t->text[0] == '/') {
            ep_next(p);
            long rhs = parse_unary(p);
            v = (rhs != 0) ? v / rhs : 0;  /* divide-by-zero silently yields 0
                                               rather than crashing the assembler */
        } else break;
    }
    return v;
}

/*
 * parse_expr: addition and subtraction, left-associative -- the
 * lowest-precedence, outermost grammar rule, and the entry point every
 * other function here is ultimately in service of. eval_expr() below is
 * just this function with tokenizing and setup/teardown wrapped around it.
 */
static long parse_expr(EParser *p) {
    long v = parse_term(p);
    for (;;) {
        Token *t = ep_peek(p);
        if (t->kind == TK_OP && t->text[0] == '+') { ep_next(p); v = v + parse_term(p); }
        else if (t->kind == TK_OP && t->text[0] == '-') { ep_next(p); v = v - parse_term(p); }
        else break;
    }
    return v;
}

long eval_expr(const char *text, long pc, int line_no, int *undefined_out) {
    EParser p;
    memset(&p, 0, sizeof(p));
    p.pc = pc;
    p.line_no = line_no;
    strncpy(p.err_text, text, sizeof(p.err_text) - 1);
    if (text[0] == '\0') asm_error(line_no, text, "Empty expression");
    tokenize_expr(text, &p);
    long v = parse_expr(&p);
    /* If parse_expr() returned without consuming every token, there's
     * leftover text after what should have been the end of the
     * expression (e.g. "5 5", or a stray character the grammar has no
     * rule for) -- that's always a syntax error, never a valid partial
     * result. */
    if (p.pos != p.ntoks)
        asm_error(line_no, text, "Unexpected trailing text in expression '%s'", text);
    if (undefined_out) *undefined_out = p.undefined;
    return v;
}
