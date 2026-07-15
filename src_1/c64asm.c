/*
 * c64asm.c - A complete two-pass 6502/6510 assembler for the Commodore 64.
 *
 * Portable C99. Builds with clang on macOS ("cc c64asm.c -o c64asm") or
 * gcc/clang on Linux, using only the standard C library (plus strcasecmp
 * from <strings.h>, which is POSIX and present on both macOS and Linux).
 *
 * Produces a C64 .prg file: a two-byte little-endian load address followed
 * by the assembled machine code.
 *
 * Usage:
 *     cc -O2 -o c64asm c64asm.c
 *     ./c64asm input.asm -o output.prg [--listing out.lst]
 *
 * See the syntax summary in the header comment of the companion README,
 * or run with no arguments for a short usage message. Syntax mirrors the
 * original Python implementation this was ported from:
 *
 *   Labels:      loop:   or   loop  lda #$00   or   SCREEN = $0400
 *   Numbers:     $1234  %01011010  1234  'A'
 *   Operators:   + - * /  ( )   unary < (low byte)  unary > (high byte)
 *   Current PC:  *
 *   Modes:       LDA #$10 / LDA $10 / LDA $1000 / LDA $10,X / LDA $1000,X
 *                LDA $10,Y / LDA $1000,Y / LDA ($10,X) / LDA ($10),Y
 *                JMP ($1000) / ASL A / RTS / BNE loop
 *   Directives:  *=$0801 / .org $0801 / .byte / .db / .word / .dw
 *                .text / .asc / .fill / .ds / .res / .basic / .equ
 *   Comments:    ; to end of line
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <strings.h>   /* strcasecmp - POSIX, available on macOS & Linux */

/* --------------------------------------------------------------------- */
/* Constants and limits                                                  */
/* --------------------------------------------------------------------- */

#define MAX_LINE_LEN   1024
#define MAX_LINES      200000
#define MAX_SYMBOLS    32768
#define MAX_MNEMONICS  64
#define MAX_TOKENS     128
#define MAX_ARGS       64
#define MAX_IDENT      128

typedef enum {
    M_IMP = 0, M_ACC, M_IMM, M_ZP, M_ZPX, M_ZPY, M_REL,
    M_ABS, M_ABSX, M_ABSY, M_IND, M_INDX, M_INDY,
    M_COUNT
} Mode;

static const int MODE_SIZE[M_COUNT] = {
    /* IMP */ 1, /* ACC */ 1, /* IMM */ 2, /* ZP */ 2, /* ZPX */ 2,
    /* ZPY */ 2, /* REL */ 2, /* ABS */ 3, /* ABSX */ 3, /* ABSY */ 3,
    /* IND */ 3, /* INDX */ 2, /* INDY */ 2
};

/* --------------------------------------------------------------------- */
/* Opcode table                                                          */
/* --------------------------------------------------------------------- */

typedef struct {
    char mnemonic[4];
    int  op[M_COUNT];   /* -1 = addressing mode not supported */
} OpcodeEntry;

static OpcodeEntry opcode_table[MAX_MNEMONICS];
static int opcode_count = 0;

static OpcodeEntry *find_mnemonic(const char *name) {
    for (int i = 0; i < opcode_count; i++)
        if (strcasecmp(opcode_table[i].mnemonic, name) == 0)
            return &opcode_table[i];
    return NULL;
}

static OpcodeEntry *get_or_add_mnemonic(const char *name) {
    OpcodeEntry *e = find_mnemonic(name);
    if (e) return e;
    e = &opcode_table[opcode_count++];
    strncpy(e->mnemonic, name, 3);
    e->mnemonic[3] = '\0';
    for (int m = 0; m < M_COUNT; m++) e->op[m] = -1;
    return e;
}

static void SETOP(const char *name, Mode m, int opcode) {
    OpcodeEntry *e = get_or_add_mnemonic(name);
    e->op[m] = opcode;
}

static int is_branch_mnemonic(const char *name) {
    static const char *branches[] = {
        "BCC","BCS","BEQ","BMI","BNE","BPL","BVC","BVS", NULL
    };
    for (int i = 0; branches[i]; i++)
        if (strcasecmp(branches[i], name) == 0) return 1;
    return 0;
}

static void init_opcodes(void) {
    SETOP("ADC",M_IMM,0x69); SETOP("ADC",M_ZP,0x65); SETOP("ADC",M_ZPX,0x75);
    SETOP("ADC",M_ABS,0x6D); SETOP("ADC",M_ABSX,0x7D); SETOP("ADC",M_ABSY,0x79);
    SETOP("ADC",M_INDX,0x61); SETOP("ADC",M_INDY,0x71);

    SETOP("AND",M_IMM,0x29); SETOP("AND",M_ZP,0x25); SETOP("AND",M_ZPX,0x35);
    SETOP("AND",M_ABS,0x2D); SETOP("AND",M_ABSX,0x3D); SETOP("AND",M_ABSY,0x39);
    SETOP("AND",M_INDX,0x21); SETOP("AND",M_INDY,0x31);

    SETOP("ASL",M_ACC,0x0A); SETOP("ASL",M_IMP,0x0A); SETOP("ASL",M_ZP,0x06);
    SETOP("ASL",M_ZPX,0x16); SETOP("ASL",M_ABS,0x0E); SETOP("ASL",M_ABSX,0x1E);

    SETOP("BCC",M_REL,0x90);
    SETOP("BCS",M_REL,0xB0);
    SETOP("BEQ",M_REL,0xF0);

    SETOP("BIT",M_ZP,0x24); SETOP("BIT",M_ABS,0x2C);

    SETOP("BMI",M_REL,0x30);
    SETOP("BNE",M_REL,0xD0);
    SETOP("BPL",M_REL,0x10);

    SETOP("BRK",M_IMP,0x00);

    SETOP("BVC",M_REL,0x50);
    SETOP("BVS",M_REL,0x70);

    SETOP("CLC",M_IMP,0x18);
    SETOP("CLD",M_IMP,0xD8);
    SETOP("CLI",M_IMP,0x58);
    SETOP("CLV",M_IMP,0xB8);

    SETOP("CMP",M_IMM,0xC9); SETOP("CMP",M_ZP,0xC5); SETOP("CMP",M_ZPX,0xD5);
    SETOP("CMP",M_ABS,0xCD); SETOP("CMP",M_ABSX,0xDD); SETOP("CMP",M_ABSY,0xD9);
    SETOP("CMP",M_INDX,0xC1); SETOP("CMP",M_INDY,0xD1);

    SETOP("CPX",M_IMM,0xE0); SETOP("CPX",M_ZP,0xE4); SETOP("CPX",M_ABS,0xEC);
    SETOP("CPY",M_IMM,0xC0); SETOP("CPY",M_ZP,0xC4); SETOP("CPY",M_ABS,0xCC);

    SETOP("DEC",M_ZP,0xC6); SETOP("DEC",M_ZPX,0xD6); SETOP("DEC",M_ABS,0xCE);
    SETOP("DEC",M_ABSX,0xDE);
    SETOP("DEX",M_IMP,0xCA);
    SETOP("DEY",M_IMP,0x88);

    SETOP("EOR",M_IMM,0x49); SETOP("EOR",M_ZP,0x45); SETOP("EOR",M_ZPX,0x55);
    SETOP("EOR",M_ABS,0x4D); SETOP("EOR",M_ABSX,0x5D); SETOP("EOR",M_ABSY,0x59);
    SETOP("EOR",M_INDX,0x41); SETOP("EOR",M_INDY,0x51);

    SETOP("INC",M_ZP,0xE6); SETOP("INC",M_ZPX,0xF6); SETOP("INC",M_ABS,0xEE);
    SETOP("INC",M_ABSX,0xFE);
    SETOP("INX",M_IMP,0xE8);
    SETOP("INY",M_IMP,0xC8);

    SETOP("JMP",M_ABS,0x4C); SETOP("JMP",M_IND,0x6C);
    SETOP("JSR",M_ABS,0x20);

    SETOP("LDA",M_IMM,0xA9); SETOP("LDA",M_ZP,0xA5); SETOP("LDA",M_ZPX,0xB5);
    SETOP("LDA",M_ABS,0xAD); SETOP("LDA",M_ABSX,0xBD); SETOP("LDA",M_ABSY,0xB9);
    SETOP("LDA",M_INDX,0xA1); SETOP("LDA",M_INDY,0xB1);

    SETOP("LDX",M_IMM,0xA2); SETOP("LDX",M_ZP,0xA6); SETOP("LDX",M_ZPY,0xB6);
    SETOP("LDX",M_ABS,0xAE); SETOP("LDX",M_ABSY,0xBE);

    SETOP("LDY",M_IMM,0xA0); SETOP("LDY",M_ZP,0xA4); SETOP("LDY",M_ZPX,0xB4);
    SETOP("LDY",M_ABS,0xAC); SETOP("LDY",M_ABSX,0xBC);

    SETOP("LSR",M_ACC,0x4A); SETOP("LSR",M_IMP,0x4A); SETOP("LSR",M_ZP,0x46);
    SETOP("LSR",M_ZPX,0x56); SETOP("LSR",M_ABS,0x4E); SETOP("LSR",M_ABSX,0x5E);

    SETOP("NOP",M_IMP,0xEA);

    SETOP("ORA",M_IMM,0x09); SETOP("ORA",M_ZP,0x05); SETOP("ORA",M_ZPX,0x15);
    SETOP("ORA",M_ABS,0x0D); SETOP("ORA",M_ABSX,0x1D); SETOP("ORA",M_ABSY,0x19);
    SETOP("ORA",M_INDX,0x01); SETOP("ORA",M_INDY,0x11);

    SETOP("PHA",M_IMP,0x48);
    SETOP("PHP",M_IMP,0x08);
    SETOP("PLA",M_IMP,0x68);
    SETOP("PLP",M_IMP,0x28);

    SETOP("ROL",M_ACC,0x2A); SETOP("ROL",M_IMP,0x2A); SETOP("ROL",M_ZP,0x26);
    SETOP("ROL",M_ZPX,0x36); SETOP("ROL",M_ABS,0x2E); SETOP("ROL",M_ABSX,0x3E);

    SETOP("ROR",M_ACC,0x6A); SETOP("ROR",M_IMP,0x6A); SETOP("ROR",M_ZP,0x66);
    SETOP("ROR",M_ZPX,0x76); SETOP("ROR",M_ABS,0x6E); SETOP("ROR",M_ABSX,0x7E);

    SETOP("RTI",M_IMP,0x40);
    SETOP("RTS",M_IMP,0x60);

    SETOP("SBC",M_IMM,0xE9); SETOP("SBC",M_ZP,0xE5); SETOP("SBC",M_ZPX,0xF5);
    SETOP("SBC",M_ABS,0xED); SETOP("SBC",M_ABSX,0xFD); SETOP("SBC",M_ABSY,0xF9);
    SETOP("SBC",M_INDX,0xE1); SETOP("SBC",M_INDY,0xF1);

    SETOP("SEC",M_IMP,0x38);
    SETOP("SED",M_IMP,0xF8);
    SETOP("SEI",M_IMP,0x78);

    SETOP("STA",M_ZP,0x85); SETOP("STA",M_ZPX,0x95); SETOP("STA",M_ABS,0x8D);
    SETOP("STA",M_ABSX,0x9D); SETOP("STA",M_ABSY,0x99); SETOP("STA",M_INDX,0x81);
    SETOP("STA",M_INDY,0x91);

    SETOP("STX",M_ZP,0x86); SETOP("STX",M_ZPY,0x96); SETOP("STX",M_ABS,0x8E);
    SETOP("STY",M_ZP,0x84); SETOP("STY",M_ZPX,0x94); SETOP("STY",M_ABS,0x8C);

    SETOP("TAX",M_IMP,0xAA);
    SETOP("TAY",M_IMP,0xA8);
    SETOP("TSX",M_IMP,0xBA);
    SETOP("TXA",M_IMP,0x8A);
    SETOP("TXS",M_IMP,0x9A);
    SETOP("TYA",M_IMP,0x98);
}

/* --------------------------------------------------------------------- */
/* Directives                                                             */
/* --------------------------------------------------------------------- */

static int is_directive(const char *tok) {
    static const char *dirs[] = {
        ".org", ".byte", ".db", ".word", ".dw", ".text", ".asc",
        ".fill", ".ds", ".res", ".basic", ".equ", NULL
    };
    for (int i = 0; dirs[i]; i++)
        if (strcasecmp(dirs[i], tok) == 0) return 1;
    return 0;
}

/* --------------------------------------------------------------------- */
/* Error handling                                                         */
/* --------------------------------------------------------------------- */

static void asm_error(int line_no, const char *raw, const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (line_no > 0) {
        fprintf(stderr, "Assembly error: %s (line %d", msg, line_no);
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
            fprintf(stderr, ": %s", trimmed + start);
        }
        fprintf(stderr, ")\n");
    } else {
        fprintf(stderr, "Assembly error: %s\n", msg);
    }
    exit(1);
}

/* --------------------------------------------------------------------- */
/* Small string helpers                                                   */
/* --------------------------------------------------------------------- */

static void trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);
}

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_char(char c)  { return isalnum((unsigned char)c) || c == '_'; }

/* ASCII (upper or lower) -> C64 PETSCII screen-appropriate bytes for
 * output via the KERNAL CHROUT routine. */
static void ascii_to_petscii(const char *s, unsigned char *out, int *outlen) {
    int n = 0;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 'a' && ch <= 'z')
            out[n++] = (unsigned char)(ch - 'a' + 'A');   /* fold lower -> upper */
        else
            out[n++] = ch;   /* already correct PETSCII: uppercase A-Z ($41-$5A)
                                 display as uppercase letters on the default C64
                                 charset, exactly like plain ASCII, and so do
                                 digits/punctuation. */
    }
    *outlen = n;
}

/* --------------------------------------------------------------------- */
/* Symbol table                                                           */
/* --------------------------------------------------------------------- */

typedef struct {
    char name[MAX_IDENT];
    long value;
} Symbol;

static Symbol symtab[MAX_SYMBOLS];
static int symtab_count = 0;

static Symbol *find_symbol(const char *name) {
    for (int i = 0; i < symtab_count; i++)
        if (strcmp(symtab[i].name, name) == 0) return &symtab[i];
    return NULL;
}

static void define_symbol(const char *name, long value, int line_no,
                           int pass_no, int allow_redefine, const char *raw) {
    Symbol *s = find_symbol(name);
    if (s) {
        if (pass_no == 1 && !allow_redefine && s->value != value)
            asm_error(line_no, raw, "Symbol '%s' already defined", name);
        s->value = value;
        return;
    }
    if (symtab_count >= MAX_SYMBOLS)
        asm_error(line_no, raw, "Too many symbols");
    strncpy(symtab[symtab_count].name, name, MAX_IDENT - 1);
    symtab[symtab_count].name[MAX_IDENT - 1] = '\0';
    symtab[symtab_count].value = value;
    symtab_count++;
}

/* --------------------------------------------------------------------- */
/* Expression evaluator                                                   */
/* --------------------------------------------------------------------- */

typedef enum { TK_HEX, TK_BIN, TK_DEC, TK_CHAR, TK_IDENT, TK_STAR, TK_OP, TK_END } TokKind;

typedef struct {
    TokKind kind;
    char text[MAX_IDENT];
} Token;

typedef struct {
    Token toks[MAX_TOKENS];
    int ntoks;
    int pos;
    long pc;
    int line_no;
    int undefined;
    char err_text[MAX_LINE_LEN];
} EParser;

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
            return 0;
        }
        case TK_OP:
            if (t->text[0] == '*') {
                /* A '*' reached here (needing a single atom) can only mean
                 * the current PC -- an infix multiply would already have
                 * been consumed by parse_term's loop before parse_atom
                 * was called. */
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

static long parse_unary(EParser *p) {
    Token *t = ep_peek(p);
    if (t->kind == TK_OP && t->text[0] == '-') { ep_next(p); return -parse_unary(p); }
    if (t->kind == TK_OP && t->text[0] == '<') { ep_next(p); return parse_unary(p) & 0xFF; }
    if (t->kind == TK_OP && t->text[0] == '>') { ep_next(p); return (parse_unary(p) >> 8) & 0xFF; }
    return parse_atom(p);
}

static long parse_term(EParser *p) {
    long v = parse_unary(p);
    for (;;) {
        Token *t = ep_peek(p);
        if (t->kind == TK_OP && t->text[0] == '*') { ep_next(p); v = v * parse_unary(p); }
        else if (t->kind == TK_OP && t->text[0] == '/') {
            ep_next(p);
            long rhs = parse_unary(p);
            v = (rhs != 0) ? v / rhs : 0;
        } else break;
    }
    return v;
}

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

static long eval_expr(const char *text, long pc, int line_no, int *undefined_out) {
    EParser p;
    memset(&p, 0, sizeof(p));
    p.pc = pc;
    p.line_no = line_no;
    strncpy(p.err_text, text, sizeof(p.err_text) - 1);
    if (text[0] == '\0') asm_error(line_no, text, "Empty expression");
    tokenize_expr(text, &p);
    long v = parse_expr(&p);
    if (p.pos != p.ntoks)
        asm_error(line_no, text, "Unexpected trailing text in expression '%s'", text);
    if (undefined_out) *undefined_out = p.undefined;
    return v;
}

/* --------------------------------------------------------------------- */
/* Comment stripping / line splitting                                     */
/* --------------------------------------------------------------------- */

static void strip_comment(char *line) {
    int in_str = 0;
    size_t n = strlen(line);
    for (size_t i = 0; i < n; i++) {
        if (line[i] == '"') in_str = !in_str;
        if (line[i] == ';' && !in_str) { line[i] = '\0'; return; }
    }
}

typedef struct {
    int line_no;
    char raw[MAX_LINE_LEN];
    int has_label;
    char label[MAX_IDENT];
    int has_op;
    char op[MAX_IDENT];      /* uppercase mnemonic, lowercase directive, or "=" */
    char operand[MAX_LINE_LEN];
} SourceLine;

static SourceLine *g_lines;
static int g_line_count = 0;

/* Scan an identifier starting at s[pos]; returns new position, copies into out. */
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

static void split_line(const char *raw_line, int line_no, SourceLine *out) {
    char line[MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    strip_comment(line);
    /* strip trailing newline/CR */
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

    /* '*=' / '* =' org directive */
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

    /* "identifier = expr" constant assignment */
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

    /* "label:" form */
    if (is_ident_start(rest[0])) {
        char ident[MAX_IDENT];
        size_t after = scan_ident(rest, 0, ident, sizeof(ident));
        if (rest[after] == ':') {
            out->has_label = 1;
            strncpy(out->label, ident, sizeof(out->label) - 1);
            char remainder[MAX_LINE_LEN];
            strncpy(remainder, rest + after + 1, sizeof(remainder) - 1);
            remainder[sizeof(remainder) - 1] = '\0';
            trim(remainder);
            strncpy(rest, remainder, sizeof(rest) - 1);
            rest[sizeof(rest) - 1] = '\0';
        } else {
            /* bare leading identifier: could be a mnemonic/directive itself,
             * or a label with no colon followed by an instruction/directive. */
            char remainder[MAX_LINE_LEN];
            size_t k = after;
            while (rest[k] == ' ' || rest[k] == '\t') k++;
            strncpy(remainder, rest + k, sizeof(remainder) - 1);
            remainder[sizeof(remainder) - 1] = '\0';
            trim(remainder);

            int ident_is_op = (find_mnemonic(ident) != NULL) || is_directive(ident);
            if (!ident_is_op) {
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
            /* else: ident itself is the op; leave rest as full stripped line */
        }
    } else if (rest[0] == '.') {
        /* directive with no label, handled below via first-token split */
    }

    if (rest[0] == '\0') return;

    /* split rest into op + operand on first whitespace */
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
        out->has_op = 1;
        strncpy(out->op, "=", sizeof(out->op) - 1);
        strncpy(out->operand, operand, sizeof(out->operand) - 1);
        return;
    }

    OpcodeEntry *e = find_mnemonic(op_tok);
    if (e) {
        out->has_op = 1;
        strncpy(out->op, e->mnemonic, sizeof(out->op) - 1);
        for (char *pc2 = out->op; *pc2; pc2++) *pc2 = (char)toupper((unsigned char)*pc2);
        strncpy(out->operand, operand, sizeof(out->operand) - 1);
        return;
    }
    if (is_directive(op_tok)) {
        out->has_op = 1;
        char lower[MAX_IDENT];
        strncpy(lower, op_tok, sizeof(lower) - 1); lower[sizeof(lower)-1]='\0';
        for (char *pc2 = lower; *pc2; pc2++) *pc2 = (char)tolower((unsigned char)*pc2);
        strncpy(out->op, lower, sizeof(out->op) - 1);
        strncpy(out->operand, operand, sizeof(out->operand) - 1);
        return;
    }

    asm_error(line_no, raw_line, "Unknown mnemonic or directive '%s'", op_tok);
}

/* --------------------------------------------------------------------- */
/* split_args: comma-separated directive arguments (quote/paren aware)    */
/* --------------------------------------------------------------------- */

static int split_args(const char *operand, char args[][MAX_LINE_LEN], int max_args) {
    int count = 0;
    int depth = 0, in_str = 0;
    char cur[MAX_LINE_LEN]; size_t cn = 0;
    cur[0] = '\0';
    for (const char *p = operand; *p; p++) {
        char ch = *p;
        if (ch == '"') { in_str = !in_str; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == '(' && !in_str) { depth++; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == ')' && !in_str) { depth--; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == ',' && depth == 0 && !in_str) {
            cur[cn] = '\0';
            trim(cur);
            if (count < max_args) strncpy(args[count++], cur, MAX_LINE_LEN - 1);
            cn = 0; cur[0] = '\0';
        } else {
            if (cn+1<sizeof(cur)) cur[cn++]=ch;
        }
    }
    cur[cn] = '\0';
    trim(cur);
    if (cur[0] != '\0' && count < max_args) strncpy(args[count++], cur, MAX_LINE_LEN - 1);
    return count;
}

/* --------------------------------------------------------------------- */
/* Addressing-mode parsing                                                */
/* --------------------------------------------------------------------- */

static int looks_forced_absolute(const char *expr_text) {
    char t[MAX_LINE_LEN];
    strncpy(t, expr_text, sizeof(t) - 1); t[sizeof(t)-1]='\0';
    trim(t);
    if (t[0] == '$') return (int)strlen(t) - 1 > 2;
    return 0;
}

/* Finds the matching ')' for the '(' at s[open_idx]; returns index or -1. */
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

static Mode parse_operand(const char *mnemonic, const char *operand_in,
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
            /* either "(expr,X)" or plain "(expr)" */
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
            /* plain indirect (JMP) */
            if (e->op[M_IND] == -1)
                asm_error(line_no, raw, "%s does not support indirect addressing", mnemonic);
            *val_out = eval_expr(inner, pc, line_no, undef_out);
            return M_IND;
        } else {
            /* expect "),Y" suffix */
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

    /* expr,X or expr,Y (operand doesn't start with '(') */
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

    /* plain expr -> zero page or absolute */
    {
        long v = eval_expr(op, pc, line_no, undef_out);
        *val_out = v;
        int is_zp = (!*undef_out) && v <= 0xFF && !looks_forced_absolute(op);
        if (is_zp && e->op[M_ZP] != -1) return M_ZP;
        if (e->op[M_ABS] != -1) return M_ABS;
        if (e->op[M_ZP] != -1) return M_ZP;
        asm_error(line_no, raw, "%s does not support that addressing mode", mnemonic);
    }
    return M_IMP; /* unreachable */
}

/* --------------------------------------------------------------------- */
/* Dynamic byte buffer for output code                                    */
/* --------------------------------------------------------------------- */

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} ByteBuf;

static void bb_init(ByteBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void bb_push(ByteBuf *b, unsigned char v) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->data = realloc(b->data, b->cap);
        if (!b->data) { fprintf(stderr, "Out of memory\n"); exit(1); }
    }
    b->data[b->len++] = v;
}
static void bb_push_n(ByteBuf *b, const unsigned char *v, size_t n) {
    for (size_t i = 0; i < n; i++) bb_push(b, v[i]);
}

/* --------------------------------------------------------------------- */
/* BASIC loader stub ("10 SYS xxxx")                                      */
/* --------------------------------------------------------------------- */

static int build_basic_stub(long sys_target, unsigned char *out) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%ld", sys_target);
    int dlen = (int)strlen(digits);
    /* body: 0x9E <digits> 0x00 */
    int body_len = 1 + dlen + 1;
    int line_len = 2 + 2 + body_len;
    long addr = 0x0801;
    long next_addr = addr + line_len;
    int n = 0;
    out[n++] = (unsigned char)(next_addr & 0xFF);
    out[n++] = (unsigned char)((next_addr >> 8) & 0xFF);
    out[n++] = 10 & 0xFF;
    out[n++] = (10 >> 8) & 0xFF;
    out[n++] = 0x9E;
    for (int i = 0; i < dlen; i++) out[n++] = (unsigned char)digits[i];
    out[n++] = 0x00;
    out[n++] = 0x00;
    out[n++] = 0x00;
    return n;
}

static int basic_stub_fixed_point(unsigned char *stub_out, long *code_start_out) {
    long target = 0x0801 + 13;
    unsigned char tmp[64];
    int len = 0;
    for (int iter = 0; iter < 4; iter++) {
        len = build_basic_stub(target, tmp);
        long new_target = 0x0801 + len;
        if (new_target == target) break;
        target = new_target;
    }
    memcpy(stub_out, tmp, len);
    *code_start_out = target;
    return len;
}

/* --------------------------------------------------------------------- */
/* Listing entries                                                        */
/* --------------------------------------------------------------------- */

typedef struct {
    long addr;
    char raw[MAX_LINE_LEN];
    unsigned char bytes[3];
    int nbytes;
} ListEntry;

static ListEntry *g_listing;
static int g_listing_count = 0;
static int g_listing_cap = 0;

static void listing_add(long addr, const char *raw, const unsigned char *bytes, int nbytes) {
    if (g_listing_count >= g_listing_cap) {
        g_listing_cap = g_listing_cap ? g_listing_cap * 2 : 1024;
        g_listing = realloc(g_listing, g_listing_cap * sizeof(ListEntry));
        if (!g_listing) { fprintf(stderr, "Out of memory\n"); exit(1); }
    }
    ListEntry *le = &g_listing[g_listing_count++];
    le->addr = addr;
    strncpy(le->raw, raw, sizeof(le->raw) - 1);
    le->raw[sizeof(le->raw) - 1] = '\0';
    le->nbytes = nbytes;
    for (int i = 0; i < nbytes && i < 3; i++) le->bytes[i] = bytes[i];
}

/* --------------------------------------------------------------------- */
/* Assembler pass                                                         */
/* --------------------------------------------------------------------- */

static long run_pass(int pass_no, ByteBuf *output, long *origin_out) {
    long pc = 0x0801;
    long origin = -1;

    for (int li = 0; li < g_line_count; li++) {
        SourceLine *L = &g_lines[li];
        long entry_pc = pc;

        if (!L->has_op) {
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);
            continue;
        }

        if (strcmp(L->op, ".basic") == 0) {
            unsigned char stub[64];
            long code_start;
            int slen = basic_stub_fixed_point(stub, &code_start);
            if (pass_no == 2) bb_push_n(output, stub, slen);
            origin = 0x0801;
            pc = code_start;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);
            continue;
        }

        if (strcmp(L->op, ".org") == 0) {
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            if (undef && pass_no == 2)
                asm_error(L->line_no, L->raw, "Undefined symbol in .org expression");
            if (origin < 0) {
                origin = val;
            } else if (pass_no == 2) {
                long current_abs = origin + (long)output->len;
                long gap = val - current_abs;
                if (gap < 0)
                    asm_error(L->line_no, L->raw,
                        ".org cannot move the program counter backward (from $%04lX to $%04lX) "
                        "-- the assembler can't overwrite bytes already assembled",
                        current_abs, val);
                for (long i = 0; i < gap; i++) bb_push(output, 0x00);
            }
            pc = val;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);
            continue;
        }

        if (strcmp(L->op, "=") == 0) {
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            define_symbol(L->label, val, L->line_no, pass_no, 1, L->raw);
            continue;
        }

        if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);

        if (strcasecmp(L->op, ".byte") == 0 || strcasecmp(L->op, ".db") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            for (int i = 0; i < nargs; i++) {
                char *a = args[i];
                if (a[0] == '"') {
                    char s[MAX_LINE_LEN]; size_t al = strlen(a);
                    size_t sl = (al >= 2 && a[al-1]=='"') ? al-2 : al-1;
                    memcpy(s, a+1, sl); s[sl]='\0';
                    unsigned char buf[MAX_LINE_LEN]; int blen=0;
                    ascii_to_petscii(s, buf, &blen);
                    if (pass_no == 2) bb_push_n(output, buf, blen);
                    pc += blen;
                } else {
                    int undef = 0;
                    long v = eval_expr(a, pc, L->line_no, &undef);
                    if (undef && pass_no == 2)
                        asm_error(L->line_no, L->raw, "Undefined symbol in .byte '%s'", a);
                    if (pass_no == 2) bb_push(output, (unsigned char)(v & 0xFF));
                    pc += 1;
                }
            }
            continue;
        }

        if (strcasecmp(L->op, ".word") == 0 || strcasecmp(L->op, ".dw") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            for (int i = 0; i < nargs; i++) {
                int undef = 0;
                long v = eval_expr(args[i], pc, L->line_no, &undef);
                if (undef && pass_no == 2)
                    asm_error(L->line_no, L->raw, "Undefined symbol in .word '%s'", args[i]);
                if (pass_no == 2) {
                    bb_push(output, (unsigned char)(v & 0xFF));
                    bb_push(output, (unsigned char)((v >> 8) & 0xFF));
                }
                pc += 2;
            }
            continue;
        }

        if (strcasecmp(L->op, ".text") == 0 || strcasecmp(L->op, ".asc") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            for (int i = 0; i < nargs; i++) {
                char *a = args[i];
                char s[MAX_LINE_LEN];
                size_t al = strlen(a);
                if (al >= 2 && a[0]=='"' && a[al-1]=='"') {
                    memcpy(s, a+1, al-2); s[al-2]='\0';
                } else {
                    strncpy(s, a, sizeof(s)-1); s[sizeof(s)-1]='\0';
                }
                unsigned char buf[MAX_LINE_LEN]; int blen=0;
                ascii_to_petscii(s, buf, &blen);
                if (pass_no == 2) bb_push_n(output, buf, blen);
                pc += blen;
            }
            continue;
        }

        if (strcasecmp(L->op, ".fill") == 0 || strcasecmp(L->op, ".ds") == 0 ||
            strcasecmp(L->op, ".res") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            int undef = 0;
            long count = eval_expr(args[0], pc, L->line_no, &undef);
            long fill_val = 0;
            if (nargs > 1) { int u2=0; fill_val = eval_expr(args[1], pc, L->line_no, &u2); }
            if (pass_no == 2) {
                for (long i = 0; i < count; i++) bb_push(output, (unsigned char)(fill_val & 0xFF));
            }
            pc += count;
            continue;
        }

        /* Real instruction */
        {
            long val = 0; int undef = 0;
            Mode mode = parse_operand(L->op, L->operand, pc, L->line_no, L->raw, &val, &undef);
            int size = MODE_SIZE[mode];

            if (pass_no == 2) {
                OpcodeEntry *e = find_mnemonic(L->op);
                if (!e || e->op[mode] == -1)
                    asm_error(L->line_no, L->raw, "Invalid addressing mode for %s", L->op);
                int opcode = e->op[mode];
                if (undef)
                    asm_error(L->line_no, L->raw, "Undefined symbol in operand '%s'", L->operand);

                unsigned char bytes[3]; int nb = 0;
                if (mode == M_REL) {
                    long offset = val - (entry_pc + 2);
                    if (offset < -128 || offset > 127)
                        asm_error(L->line_no, L->raw,
                                  "Branch target out of range (%+ld) for %s %s",
                                  offset, L->op, L->operand);
                    bytes[nb++] = (unsigned char)opcode;
                    bytes[nb++] = (unsigned char)(offset & 0xFF);
                } else if (size == 1) {
                    bytes[nb++] = (unsigned char)opcode;
                } else if (size == 2) {
                    bytes[nb++] = (unsigned char)opcode;
                    bytes[nb++] = (unsigned char)(val & 0xFF);
                } else {
                    bytes[nb++] = (unsigned char)opcode;
                    bytes[nb++] = (unsigned char)(val & 0xFF);
                    bytes[nb++] = (unsigned char)((val >> 8) & 0xFF);
                }
                bb_push_n(output, bytes, nb);
                listing_add(entry_pc, L->raw, bytes, nb);
            }
            pc += size;
        }
    }

    if (origin < 0) origin = 0x0801;
    *origin_out = origin;
    return pc;
}

/* --------------------------------------------------------------------- */
/* File loading                                                           */
/* --------------------------------------------------------------------- */

static void load_source(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open input file '%s'\n", path); exit(1); }

    g_lines = malloc(sizeof(SourceLine) * MAX_LINES);
    if (!g_lines) { fprintf(stderr, "Out of memory\n"); exit(1); }

    char buf[MAX_LINE_LEN];
    int line_no = 0;
    while (fgets(buf, sizeof(buf), f)) {
        line_no++;
        if (g_line_count >= MAX_LINES) {
            fprintf(stderr, "Too many source lines (max %d)\n", MAX_LINES);
            exit(1);
        }
        split_line(buf, line_no, &g_lines[g_line_count]);
        g_line_count++;
    }
    fclose(f);
}

/* --------------------------------------------------------------------- */
/* main                                                                    */
/* --------------------------------------------------------------------- */

static void usage(const char *prog) {
    fprintf(stderr,
        "c64asm - a two-pass 6502/6510 assembler for the Commodore 64\n\n"
        "Usage: %s <input.asm> -o <output.prg> [--listing <file.lst>]\n",
        prog);
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *listing_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--listing") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            listing_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-' && !input_path) {
            input_path = argv[i];
        } else {
            fprintf(stderr, "Unrecognized argument '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!input_path || !output_path) {
        usage(argv[0]);
        return 1;
    }

    init_opcodes();
    load_source(input_path);

    ByteBuf dummy; bb_init(&dummy);
    long origin1;
    run_pass(1, &dummy, &origin1);   /* build symbol table */
    free(dummy.data);

    ByteBuf output; bb_init(&output);
    long origin2;
    run_pass(2, &output, &origin2);  /* generate code */

    FILE *out = fopen(output_path, "wb");
    if (!out) { fprintf(stderr, "Cannot open output file '%s'\n", output_path); return 1; }
    unsigned char header[2] = { (unsigned char)(origin2 & 0xFF), (unsigned char)((origin2 >> 8) & 0xFF) };
    fwrite(header, 1, 2, out);
    if (output.len) fwrite(output.data, 1, output.len, out);
    fclose(out);

    printf("Assembled %zu bytes, origin=$%04lX -> %s\n", output.len, origin2, output_path);

    if (listing_path) {
        FILE *lf = fopen(listing_path, "w");
        if (!lf) { fprintf(stderr, "Cannot open listing file '%s'\n", listing_path); return 1; }
        fprintf(lf, "; c64asm listing  (origin $%04lX, %zu bytes)\n\n", origin2, output.len);
        for (int i = 0; i < g_listing_count; i++) {
            ListEntry *le = &g_listing[i];
            char hexb[16] = "";
            char tmp[4];
            hexb[0] = '\0';
            for (int b = 0; b < le->nbytes; b++) {
                snprintf(tmp, sizeof(tmp), "%02X ", le->bytes[b]);
                strncat(hexb, tmp, sizeof(hexb) - strlen(hexb) - 1);
            }
            /* trim trailing space for alignment control below */
            size_t hl = strlen(hexb);
            while (hl > 0 && hexb[hl-1] == ' ') hexb[--hl] = '\0';
            char rawtrim[MAX_LINE_LEN];
            strncpy(rawtrim, le->raw, sizeof(rawtrim) - 1); rawtrim[sizeof(rawtrim)-1]='\0';
            size_t rl = strlen(rawtrim);
            while (rl > 0 && (rawtrim[rl-1]=='\n' || rawtrim[rl-1]=='\r')) rawtrim[--rl]='\0';
            fprintf(lf, "%04lX  %-9s %s\n", le->addr, hexb, rawtrim);
        }
        fprintf(lf, "\nSymbol table:\n");
        /* simple insertion-sort-free alphabetical print via naive selection sort
         * (symbol counts are small for typical programs) */
        int *order = malloc(sizeof(int) * symtab_count);
        for (int i = 0; i < symtab_count; i++) order[i] = i;
        for (int i = 0; i < symtab_count; i++) {
            int min = i;
            for (int j = i + 1; j < symtab_count; j++)
                if (strcmp(symtab[order[j]].name, symtab[order[min]].name) < 0) min = j;
            int t = order[i]; order[i] = order[min]; order[min] = t;
        }
        for (int i = 0; i < symtab_count; i++) {
            Symbol *s = &symtab[order[i]];
            fprintf(lf, "  %-20s = $%04lX\n", s->name, s->value);
        }
        free(order);
        fclose(lf);
        printf("Listing written to %s\n", listing_path);
    }

    free(output.data);
    free(g_lines);
    free(g_listing);
    return 0;
}
