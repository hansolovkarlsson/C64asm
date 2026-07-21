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
 *                .text / .asc / .fill / .ds / .res / .basic / .equ / .align
 *                .if / .elif / .else / .endif / .ifdef / .ifndef
 *   Comments:    ; to end of line
 */

/* Requests POSIX.1-2008 and BSD-heritage declarations (realpath(), and,
 * made explicit here for the first time even though it was already
 * relied on before this comment existed, strcasecmp()) from the C
 * library, since -std=c99 alone hides them on some systems -- observed
 * here as an inconsistent "implicit declaration of function 'realpath'"
 * warning that appeared under some compiler flag combinations but not
 * others, and empirically needed *both* macros below to go away
 * reliably on this glibc version (_POSIX_C_SOURCE alone wasn't
 * sufficient). An implicit declaration isn't just a style complaint:
 * the compiler then assumes a default (int-returning) signature, which
 * doesn't match realpath()'s real (pointer-returning) one -- undefined
 * behavior, and a real bug source on any platform where int and
 * pointer sizes actually differ. Must come before any #include. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

/* Must come before any #include: realpath() (used by .include support,
 * further down) is POSIX.1-2008, not standard C99, and without this
 * some libcs (in strict -std=c99 mode) won't declare it from
 * <stdlib.h> at all -- which, left unnoticed, is worse than a missing
 * function: an implicitly-declared function is assumed by the compiler
 * to return int, and on a 64-bit platform where pointers and int are
 * different sizes, that silently corrupts the real (pointer-returning)
 * result rather than just failing to compile. _XOPEN_SOURCE 700
 * (rather than _POSIX_C_SOURCE, which on its own turned out not to be
 * sufficient on glibc) is the portable choice that's recognized
 * consistently by both glibc and macOS's libc. */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <strings.h>   /* strcasecmp - POSIX, available on macOS & Linux */
#include <limits.h>    /* PATH_MAX */
#include <sys/stat.h>  /* stat - used to confirm a resolved include path is a
                           regular file before opening it */

#ifndef PATH_MAX
#define PATH_MAX 4096   /* POSIX systems normally define this; this fallback
                            only matters on the rare system that doesn't */
#endif

/* --------------------------------------------------------------------- */
/* Constants and limits                                                  */
/* --------------------------------------------------------------------- */

#define MAX_LINE_LEN   1024
#define MAX_LINES      200000
#define MAX_SYMBOLS    32768
#define MAX_MNEMONICS  96
#define MAX_TOKENS     128
#define MAX_ARGS       64
#define MAX_IDENT      128
#define MAX_FILENAME_LEN 256   /* longest display filename this assembler will
                                   track per line -- deliberately much smaller
                                   than PATH_MAX, since it's multiplied by
                                   MAX_LINES in SourceLine below; a canonical
                                   path used only for cycle/dedup comparisons
                                   (never stored per-line) gets a full
                                   PATH_MAX-sized buffer instead, see the
                                   Includes section further down */

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
    char mnemonic[5];   /* 4 chars max (USBC, an illegal-opcode mnemonic,
                            is the only one longer than 3) + NUL */
    int  op[M_COUNT];   /* -1 = addressing mode not supported */
    int  illegal[M_COUNT];   /* 1 = this (mnemonic, mode) slot is an
                                 illegal/undocumented opcode that requires
                                 '.cpu 6510x' -- see SETOP_ILLEGAL() and
                                 the "Illegal opcodes" section of
                                 c64asm-reference.md */
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
    strncpy(e->mnemonic, name, 4);
    e->mnemonic[4] = '\0';
    for (int m = 0; m < M_COUNT; m++) { e->op[m] = -1; e->illegal[m] = 0; }
    return e;
}

static void SETOP(const char *name, Mode m, int opcode) {
    OpcodeEntry *e = get_or_add_mnemonic(name);
    e->op[m] = opcode;
}

/* Like SETOP(), but also marks this (mnemonic, mode) slot as an
 * illegal/undocumented opcode -- see the "Illegal opcodes" section
 * near the end of init_opcodes() below, and c64asm-reference.md, for
 * the full explanation. */
static void SETOP_ILLEGAL(const char *name, Mode m, int opcode) {
    OpcodeEntry *e = get_or_add_mnemonic(name);
    e->op[m] = opcode;
    e->illegal[m] = 1;
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

    /* ----------------------------------------------------------------- */
    /* Illegal / undocumented opcodes -- see c64asm-reference.md's       */
    /* "Illegal opcodes" section for the full user-facing explanation.   */
    /* These are real instructions the NMOS 6502/6510 executes (nothing  */
    /* in the silicon actually "traps" an unused opcode byte), but MOS   */
    /* never documented or supported them, and a few of them behave      */
    /* slightly differently between individual chips -- see the notes    */
    /* below on which are considered unstable. Every slot registered     */
    /* here via SETOP_ILLEGAL() (as opposed to plain SETOP()) requires   */
    /* '.cpu 6510x' before it can actually be assembled -- see the gate  */
    /* in run_pass()'s real-instruction handling.                        */
    /*                                                                    */
    /* Mnemonics and opcode assignments follow the widely-used oxyron.de */
    /* table (http://www.oxyron.de/html/opcodes02.html), the standard    */
    /* C64-scene reference for this. A few of these opcodes have more    */
    /* than one valid encoding for the exact same mnemonic+mode (e.g.    */
    /* ANC is both $0B and $2B) -- this assembler always emits the       */
    /* lower/more common of the two, same as every other assembler of    */
    /* this kind; a disassembler would need to preserve the distinction, */
    /* but this is an assembler, not a disassembler.                     */
    /*                                                                    */
    /* $EB is a byte-for-byte functional duplicate of SBC #imm ($E9) --   */
    /* to avoid a collision with the real, documented SBC mnemonic       */
    /* above, it's given the distinct mnemonic USBC here, following the  */
    /* same convention several other illegal-opcode assemblers use.      */
    SETOP_ILLEGAL("SLO",M_ZP,0x07); SETOP_ILLEGAL("SLO",M_ZPX,0x17);
    SETOP_ILLEGAL("SLO",M_INDX,0x03); SETOP_ILLEGAL("SLO",M_INDY,0x13);
    SETOP_ILLEGAL("SLO",M_ABS,0x0F); SETOP_ILLEGAL("SLO",M_ABSX,0x1F);
    SETOP_ILLEGAL("SLO",M_ABSY,0x1B);

    SETOP_ILLEGAL("RLA",M_ZP,0x27); SETOP_ILLEGAL("RLA",M_ZPX,0x37);
    SETOP_ILLEGAL("RLA",M_INDX,0x23); SETOP_ILLEGAL("RLA",M_INDY,0x33);
    SETOP_ILLEGAL("RLA",M_ABS,0x2F); SETOP_ILLEGAL("RLA",M_ABSX,0x3F);
    SETOP_ILLEGAL("RLA",M_ABSY,0x3B);

    SETOP_ILLEGAL("SRE",M_ZP,0x47); SETOP_ILLEGAL("SRE",M_ZPX,0x57);
    SETOP_ILLEGAL("SRE",M_INDX,0x43); SETOP_ILLEGAL("SRE",M_INDY,0x53);
    SETOP_ILLEGAL("SRE",M_ABS,0x4F); SETOP_ILLEGAL("SRE",M_ABSX,0x5F);
    SETOP_ILLEGAL("SRE",M_ABSY,0x5B);

    SETOP_ILLEGAL("RRA",M_ZP,0x67); SETOP_ILLEGAL("RRA",M_ZPX,0x77);
    SETOP_ILLEGAL("RRA",M_INDX,0x63); SETOP_ILLEGAL("RRA",M_INDY,0x73);
    SETOP_ILLEGAL("RRA",M_ABS,0x6F); SETOP_ILLEGAL("RRA",M_ABSX,0x7F);
    SETOP_ILLEGAL("RRA",M_ABSY,0x7B);

    SETOP_ILLEGAL("SAX",M_ZP,0x87); SETOP_ILLEGAL("SAX",M_ZPY,0x97);
    SETOP_ILLEGAL("SAX",M_INDX,0x83); SETOP_ILLEGAL("SAX",M_ABS,0x8F);

    SETOP_ILLEGAL("LAX",M_ZP,0xA7); SETOP_ILLEGAL("LAX",M_ZPY,0xB7);
    SETOP_ILLEGAL("LAX",M_INDX,0xA3); SETOP_ILLEGAL("LAX",M_INDY,0xB3);
    SETOP_ILLEGAL("LAX",M_ABS,0xAF); SETOP_ILLEGAL("LAX",M_ABSY,0xBF);
    SETOP_ILLEGAL("LAX",M_IMM,0xAB);   /* unstable -- see reference doc */

    SETOP_ILLEGAL("DCP",M_ZP,0xC7); SETOP_ILLEGAL("DCP",M_ZPX,0xD7);
    SETOP_ILLEGAL("DCP",M_INDX,0xC3); SETOP_ILLEGAL("DCP",M_INDY,0xD3);
    SETOP_ILLEGAL("DCP",M_ABS,0xCF); SETOP_ILLEGAL("DCP",M_ABSX,0xDF);
    SETOP_ILLEGAL("DCP",M_ABSY,0xDB);

    SETOP_ILLEGAL("ISC",M_ZP,0xE7); SETOP_ILLEGAL("ISC",M_ZPX,0xF7);
    SETOP_ILLEGAL("ISC",M_INDX,0xE3); SETOP_ILLEGAL("ISC",M_INDY,0xF3);
    SETOP_ILLEGAL("ISC",M_ABS,0xEF); SETOP_ILLEGAL("ISC",M_ABSX,0xFF);
    SETOP_ILLEGAL("ISC",M_ABSY,0xFB);

    SETOP_ILLEGAL("ANC",M_IMM,0x0B);
    SETOP_ILLEGAL("ALR",M_IMM,0x4B);
    SETOP_ILLEGAL("ARR",M_IMM,0x6B);
    SETOP_ILLEGAL("XAA",M_IMM,0x8B);    /* highly unstable -- see reference doc */
    SETOP_ILLEGAL("AXS",M_IMM,0xCB);
    SETOP_ILLEGAL("USBC",M_IMM,0xEB);   /* functional duplicate of SBC #imm */

    SETOP_ILLEGAL("AHX",M_INDY,0x93); SETOP_ILLEGAL("AHX",M_ABSY,0x9F);   /* highly unstable */
    SETOP_ILLEGAL("SHY",M_ABSX,0x9C);   /* unstable */
    SETOP_ILLEGAL("SHX",M_ABSY,0x9E);   /* unstable */
    SETOP_ILLEGAL("TAS",M_ABSY,0x9B);   /* unstable */
    SETOP_ILLEGAL("LAS",M_ABSY,0xBB);

    /* Halts the CPU until reset. 11 other opcode bytes ($12,$22,$32,
     * $42,$52,$62,$72,$92,$B2,$D2,$F2) do exactly the same thing, but
     * only one encoding is needed for assembling. */
    SETOP_ILLEGAL("KIL",M_IMP,0x02);

    /* NOP normally only has implied-mode addressing ($EA, set above).
     * The NMOS 6502/6510 also executes several additional opcode bytes
     * as NOP-with-an-ignored-operand, across four more addressing modes
     * -- these extend the *same* mnemonic's mode table rather than
     * needing a distinct name, since they behave exactly like NOP: the
     * operand is fetched (costing the extra byte(s) and cycles) and
     * then discarded. */
    SETOP_ILLEGAL("NOP",M_IMM,0x80);
    SETOP_ILLEGAL("NOP",M_ZP,0x04);
    SETOP_ILLEGAL("NOP",M_ZPX,0x14);
    SETOP_ILLEGAL("NOP",M_ABS,0x0C);
    SETOP_ILLEGAL("NOP",M_ABSX,0x1C);
}

/* --------------------------------------------------------------------- */
/* Directives                                                             */
/* --------------------------------------------------------------------- */

static int is_directive(const char *tok) {
    static const char *dirs[] = {
        ".org", ".byte", ".db", ".word", ".dw", ".text", ".asc",
        ".fill", ".ds", ".res", ".basic", ".equ", ".align", ".cpu", ".charset",
        ".error", ".warning", ".incbin", ".assert",
        ".if", ".elif", ".else", ".endif", ".ifdef", ".ifndef", NULL
    };
    for (int i = 0; dirs[i]; i++)
        if (strcasecmp(dirs[i], tok) == 0) return 1;
    return 0;
}

/* --------------------------------------------------------------------- */
/* Error handling                                                         */
/* --------------------------------------------------------------------- */

/*
 * Filename-aware error messages, for .include support (see the Includes
 * section further down for the full design). This is deliberately
 * global, mutable state rather than a filename parameter threaded
 * through eval_expr(), parse_operand(), define_symbol(), and everything
 * else that can call asm_error(): doing that properly would mean
 * touching a large fraction of this file's functions to plumb a value
 * through that, for the overwhelming majority of programs (anything not
 * using .include), is never even read. Reading a small piece of global
 * state at the exact moment an error is actually raised gets the same
 * result with a far smaller footprint -- and it's safe here specifically
 * because assembly is strictly sequential and single-threaded: there is
 * only ever one "currently relevant" file for error-reporting purposes
 * at any given moment, whether during preprocessing or during either
 * assembly pass.
 */
static char g_current_error_file[MAX_FILENAME_LEN] = "";
static int g_multi_file_mode = 0;   /* only becomes true once .include is
                                        actually used; until then, error
                                        messages are byte-for-byte identical
                                        to how they always were, with no
                                        filename shown at all */

static void set_error_file(const char *filename) {
    if (filename) {
        strncpy(g_current_error_file, filename, sizeof(g_current_error_file) - 1);
        g_current_error_file[sizeof(g_current_error_file) - 1] = '\0';
    } else {
        g_current_error_file[0] = '\0';
    }
}

/* Builds the exact "<prefix>: ..." text asm_error() has always printed
 * (prefix is always "Assembly error" there), but into a caller-
 * supplied buffer instead of straight to stderr, and with the prefix
 * itself now a parameter -- shared by the fatal path (asm_error,
 * below), the recoverable path (asm_error_recoverable, further down),
 * and asm_warning() (which passes "Warning" instead), so all three
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

static void asm_error(int line_no, const char *raw, const char *fmt, ...) {
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
 * handling in run_pass()), in the same "(line N: source text)" format
 * asm_error()/asm_error_recoverable() use -- but, unlike either of
 * those, this doesn't count toward the error total, doesn't stop pass
 * 2 from running or output from being written, and doesn't affect the
 * exit status. Nothing else in this assembler currently produces a
 * warning; this exists purely for the '.warning' directive itself to
 * call. */
static void asm_warning(int line_no, const char *raw, const char *fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    char full[MAX_LINE_LEN + 1280];
    format_error_message(full, sizeof(full), line_no, raw, msg, "Warning");
    fprintf(stderr, "%s\n", full);
}

/* --------------------------------------------------------------------- */
/* Multi-error reporting. asm_error() above is for genuinely fatal
 * problems -- a missing file, a circular .include, a macro or
 * conditional-assembly block whose structure is broken -- where the
 * shape of the rest of the source file becomes ambiguous and there's no
 * reasonable way to keep going. Everything else -- an undefined symbol,
 * a malformed expression, an addressing mode a mnemonic doesn't
 * support, a branch out of range, a redefined symbol -- is a
 * self-contained problem with one specific line or operand. For those,
 * asm_error_recoverable() below records the message (in asm_error()'s
 * exact display format) instead of exiting, and returns normally so the
 * calling code can carry on with some sensible fallback value (0 for a
 * broken expression, a plausible addressing mode, the previous value
 * for a symbol redefinition, and so on) -- each call site chooses its
 * own fallback right where it calls this, the same way it already had
 * to choose what to do in the success case.
 *
 * This is what lets one assembly run surface several independent
 * mistakes instead of stopping at the first one. It's an intentional
 * trade-off: a later error's line number and message are still exactly
 * correct, but if an earlier error meant a value or an addressing-mode
 * decision came out different from what the source actually implies, a
 * handful of further messages may be downstream noise from that first
 * real mistake rather than independent problems of their own -- fix the
 * first one and reassemble if the rest look strange.
 * --------------------------------------------------------------------- */

#define MAX_COLLECTED_ERRORS 20
static char *g_collected_errors[MAX_COLLECTED_ERRORS];
static int g_collected_count = 0;
static long g_total_error_count = 0;

static int any_errors_recorded(void) { return g_total_error_count > 0; }

/* Called once per assembly run so re-running the assembler logic more
 * than once in the same process (not that main() currently does)
 * starts with a clean slate. */
static void reset_collected_errors(void) {
    for (int i = 0; i < g_collected_count; i++) { free(g_collected_errors[i]); g_collected_errors[i] = NULL; }
    g_collected_count = 0;
    g_total_error_count = 0;
}

static void print_all_collected_errors_and_exit(void) {
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

/* The recoverable counterpart to asm_error() -- see the note above.
 * Records the message and returns normally instead of exiting; the
 * caller supplies its own fallback return value right after this call,
 * exactly as it already had to decide what to return in the
 * non-error case.
 *
 * If the number of recorded errors reaches MAX_COLLECTED_ERRORS, this
 * prints everything collected so far and exits immediately -- so, like
 * asm_error(), this function may not return, and callers must supply a
 * fallback value as if it always does. */
static void asm_error_recoverable(int line_no, const char *raw, const char *fmt, ...) {
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
 * output via the KERNAL CHROUT routine.
 *
 * lower_mode=0 (the default, and this assembler's overall default via
 * '.charset upper' -- see below): every letter, whatever case it was
 * written in, becomes a PETSCII byte in the $41-$5A range. That range
 * displays as uppercase on the C64's default (power-on) character
 * set, which is the only character set any program using this mode
 * is expected to be running under -- see the caveat below.
 *
 * lower_mode=1 ('.charset lower'): letters keep their original case
 * using PETSCII's actual encoding for it -- lowercase becomes $41-$5A
 * (PETSCII's "unshifted" range, which the hardware displays as
 * lowercase specifically on the *lowercase/uppercase* character set,
 * not the default one) and uppercase becomes $C1-$DA ("shifted",
 * which displays as uppercase on *either* character set). This is
 * what actually produces mixed-case text on screen -- but only once
 * the C64 has been switched to the lowercase/uppercase character set
 * at runtime (e.g. via text.inc's SET_LOWERCASE_CHARSET macro); the
 * assembler has no way to do that switch itself, since it's a runtime
 * hardware state, not something that exists at assembly time.
 *
 * Caveat: text assembled under '.charset upper' is only guaranteed to
 * display as uppercase while the default character set is still
 * active. If a program ever switches to the lowercase/uppercase set
 * for some '.charset lower' text, any '.charset upper' text printed
 * afterward would display as lowercase too, since $41-$5A means
 * something different on that character set. Once a program switches
 * character sets at runtime, use '.charset lower' for everything it
 * prints from that point on -- typed-in-uppercase source text still
 * displays correctly as uppercase either way, since '.charset lower'
 * encodes uppercase letters using the character-set-independent
 * $C1-$DA range specifically so this works. See
 * c64asm-reference.md's "Text and PETSCII" section. */
static void ascii_to_petscii(const char *s, unsigned char *out, int *outlen, int lower_mode) {
    int n = 0;
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (lower_mode) {
            if (ch >= 'a' && ch <= 'z')
                out[n++] = (unsigned char)(ch - 'a' + 0x41);
            else if (ch >= 'A' && ch <= 'Z')
                out[n++] = (unsigned char)(ch - 'A' + 0xC1);
            else
                out[n++] = ch;
        } else {
            if (ch >= 'a' && ch <= 'z')
                out[n++] = (unsigned char)(ch - 'a' + 'A');   /* fold lower -> upper */
            else
                out[n++] = ch;   /* already correct PETSCII: uppercase A-Z ($41-$5A)
                                     display as uppercase letters on the default C64
                                     charset, exactly like plain ASCII, and so do
                                     digits/punctuation. */
        }
    }
    *outlen = n;
}

/* --------------------------------------------------------------------- */
/* Symbol table                                                           */
/* --------------------------------------------------------------------- */

typedef struct {
    char name[MAX_IDENT];
    long value;
    int first_li;   /* g_lines index of this symbol's FIRST definition;
                        used by .ifdef/.ifndef to correctly answer "has
                        this been defined YET" in a way that's consistent
                        across both assembly passes -- see the note above
                        the conditional-assembly handling in run_pass() */
    int used;       /* set the first time this symbol is successfully
                        looked up from within an expression (TK_IDENT in
                        parse_atom) -- see --warn-unused/
                        report_unused_symbols() near main() */
} Symbol;

static Symbol symtab[MAX_SYMBOLS];
static int symtab_count = 0;

static Symbol *find_symbol(const char *name) {
    for (int i = 0; i < symtab_count; i++)
        if (strcmp(symtab[i].name, name) == 0) return &symtab[i];
    return NULL;
}

/* Like find_symbol(), but only counts as found if the symbol's FIRST
 * definition was strictly before g_lines index `li` -- see the note
 * above the conditional-assembly handling in run_pass() for why this,
 * not a plain find_symbol() != NULL check, is what .ifdef/.ifndef need. */
static Symbol *find_symbol_defined_before(const char *name, int li) {
    Symbol *s = find_symbol(name);
    if (s && s->first_li < li) return s;
    return NULL;
}

static void define_symbol(const char *name, long value, int line_no,
                           int pass_no, int allow_redefine, const char *raw, int li) {
    Symbol *s = find_symbol(name);
    if (s) {
        if (pass_no == 1 && !allow_redefine && s->value != value)
            asm_error_recoverable(line_no, raw, "Symbol '%s' already defined", name);
        s->value = value;
        return;
    }
    if (symtab_count >= MAX_SYMBOLS)
        asm_error(line_no, raw, "Too many symbols");
    strncpy(symtab[symtab_count].name, name, MAX_IDENT - 1);
    symtab[symtab_count].name[MAX_IDENT - 1] = '\0';
    symtab[symtab_count].value = value;
    symtab[symtab_count].first_li = li;
    symtab[symtab_count].used = 0;   /* symtab is static storage, so this
                                         is already guaranteed by the C
                                         standard, but spelled out here
                                         to match the other fields above
                                         rather than relying on that
                                         implicitly */
    symtab_count++;
}

/* --------------------------------------------------------------------- */
/* Expression evaluator                                                   */
/* --------------------------------------------------------------------- */

typedef enum { TK_HEX, TK_BIN, TK_DEC, TK_CHAR, TK_IDENT, TK_STAR, TK_OP, TK_CMP, TK_END } TokKind;

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
    int tokenize_failed;   /* set only by tokenize_expr()'s "bad character"
                               path; distinct from `undefined` (also set
                               legitimately for an undefined SYMBOL found
                               during parsing) -- see eval_expr() for why
                               this distinction matters */
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
            if (j == i + 1)
                asm_error_recoverable(p->line_no, text, "Bad hex literal in expression '%s'", text);
                /* fallback: j stays i+1, so the token text is just "$" --
                   parse_atom's TK_HEX case strtol()s the empty digit
                   string after it to 0, a harmless placeholder value */
            size_t n = j - i; if (n >= MAX_IDENT) n = MAX_IDENT - 1;
            memcpy(t.text, s + i, n); t.text[n] = '\0';
            t.kind = TK_HEX; i = j;
        } else if (c == '%') {
            size_t j = i + 1;
            while (j < len && (s[j] == '0' || s[j] == '1')) j++;
            if (j == i + 1)
                asm_error_recoverable(p->line_no, text, "Bad binary literal in expression '%s'", text);
                /* same fallback idea as the hex case above */
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
            char inner = '\0';
            int char_lit_error = 0;
            if (j < len && s[j] == '\\' && j + 1 < len) { inner = s[j+1]; j += 2; }
            else if (j < len) { inner = s[j]; j += 1; }
            else {
                asm_error_recoverable(p->line_no, text, "Bad character literal in expression '%s'", text);
                char_lit_error = 1;   /* nothing left to look for a closing
                                          quote in -- skip that check below
                                          so this single malformed literal
                                          doesn't also report as unterminated */
            }
            if (!char_lit_error) {
                if (j >= len || s[j] != '\'')
                    asm_error_recoverable(p->line_no, text, "Unterminated character literal in expression '%s'", text);
                    /* fallback: don't consume whatever character sits where
                       the closing quote should have been -- leave it for
                       the next tokenize_expr() iteration to reprocess
                       normally, rather than silently swallowing it */
                else
                    j++;
            }
            t.text[0] = inner; t.text[1] = '\0';
            t.kind = TK_CHAR; i = j;
        } else if (is_ident_start(c)) {
            size_t j = i;
            while (j < len && (is_ident_char(s[j]) || s[j] == '.')) j++;
            size_t n = j - i; if (n >= MAX_IDENT) n = MAX_IDENT - 1;
            memcpy(t.text, s + i, n); t.text[n] = '\0';
            t.kind = TK_IDENT; i = j;
        } else if (c == '=' && i + 1 < len && s[i+1] == '=') {
            t.kind = TK_CMP; t.text[0] = '='; t.text[1] = '='; t.text[2] = '\0'; i += 2;
        } else if (c == '!' && i + 1 < len && s[i+1] == '=') {
            t.kind = TK_CMP; t.text[0] = '!'; t.text[1] = '='; t.text[2] = '\0'; i += 2;
        } else if (c == '(' || c == ')' || c == '+' || c == '-' || c == '/' ||
                   c == '<' || c == '>' || c == '*') {
            t.kind = TK_OP; t.text[0] = c; t.text[1] = '\0'; i++;
        } else {
            asm_error_recoverable(p->line_no, text, "Bad character '%c' in expression '%s'", c, text);
            /* give up tokenizing the rest of this expression rather than
               risk tokenizing more of a string already known malformed,
               mirroring the Python implementation's tokenize_failed path */
            p->tokenize_failed = 1;
            p->undefined = 1;
            return;
        }
        if (p->ntoks >= MAX_TOKENS)
            asm_error(p->line_no, text, "Expression too complex '%s'", text);
            /* stays fatal: a C-only fixed-size token buffer limit, in the
               same spirit as MAX_SYMBOLS/MAX_LINES, not a per-expression
               mistake in the source */
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
            if (s) { s->used = 1; return s->value; }
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
                    asm_error_recoverable(p->line_no, p->err_text, "Missing ')' in expression '%s'", p->err_text);
                return v;   /* fallback: whatever was successfully parsed
                               inside the parens */
            }
            /* fallthrough to error */
            break;
        default: break;
    }
    asm_error_recoverable(p->line_no, p->err_text, "Cannot parse expression '%s'", p->err_text);
    return 0;   /* fallback: smallest-footprint placeholder value */
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

/* == and != -- deliberately the loosest-binding operators (lower
 * precedence than +/-, matching the usual convention that
 * "a + b == c + d" means "(a+b) == (c+d)"), and deliberately not
 * chainable the way some languages allow ("a == b == c" is valid here
 * but means "(a==b) == c", not "a==b and b==c"). Mainly meant for
 * '.assert' (c64asm-reference.md), but available in any expression,
 * evaluating to 1 (true) or 0 (false) either way. Deliberately not <,
 * >, <=, >= as binary comparisons -- < and > are already unary
 * low/high-byte operators here, and overloading them for both meanings
 * would be genuinely ambiguous to parse. */
static long parse_equality(EParser *p) {
    long v = parse_expr(p);
    Token *t = ep_peek(p);
    if (t->kind == TK_CMP) {
        ep_next(p);
        long rhs = parse_expr(p);
        int result = (t->text[0] == '=') ? (v == rhs) : (v != rhs);
        v = result ? 1 : 0;
    }
    return v;
}

static long eval_expr(const char *text, long pc, int line_no, int *undefined_out) {
    EParser p;
    memset(&p, 0, sizeof(p));
    p.pc = pc;
    p.line_no = line_no;
    strncpy(p.err_text, text, sizeof(p.err_text) - 1);
    if (text[0] == '\0') {
        asm_error_recoverable(line_no, text, "Empty expression");
        if (undefined_out) *undefined_out = 1;
        return 0;
    }
    tokenize_expr(text, &p);
    if (p.tokenize_failed) {
        /* tokenize_expr() already recorded its own error and gave up
           partway through -- don't also try to parse the possibly
           incomplete token list, which would just produce a second,
           redundant report for the exact same underlying problem. */
        if (undefined_out) *undefined_out = 1;
        return 0;
    }
    long v = parse_equality(&p);
    if (p.pos != p.ntoks) {
        asm_error_recoverable(line_no, text, "Unexpected trailing text in expression '%s'", text);
        p.undefined = 1;
    }
    if (undefined_out) *undefined_out = p.undefined;
    return v;   /* fallback: whatever was successfully parsed so far */
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
    char filename[MAX_FILENAME_LEN];  /* which file this line came from, for
                                          error messages once .include is used;
                                          empty for a program that never uses it */
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

/* Like scan_ident() above, but also accepts '.' mid-name -- used only
 * for the "identifier = expr" constant-assignment form below, so that
 * a '.struct'-generated field symbol ("Room.north = 2") parses
 * correctly. Deliberately NOT used for scan_ident()'s other call site
 * (ordinary "label:" recognition further down), which stays exactly
 * as strict as it always was -- nothing before '.struct' existed ever
 * produced or relied on a dotted name in either position, so this is
 * purely additive for the one case that actually needs it. */
static size_t scan_ident_dotted(const char *s, size_t pos, char *out, size_t outsz) {
    size_t j = pos;
    size_t n = 0;
    while (s[j] && (is_ident_char(s[j]) || s[j] == '.')) {
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
        size_t after = scan_ident_dotted(stripped, 0, ident, sizeof(ident));
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
                    asm_error_recoverable(line_no, raw_line, "Unknown mnemonic or directive '%s'", ident);
                    /* fallback: treat the whole line as blank (no label,
                       no op) rather than guessing -- ident's role here was
                       already ambiguous (label? mistyped mnemonic?) */
                    out->has_label = 0;
                    out->label[0] = '\0';
                    return;
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

    asm_error_recoverable(line_no, raw_line, "Unknown mnemonic or directive '%s'", op_tok);
    /* fallback: treat the whole line as blank (no label, no op) --
       simple and safe rather than trying to preserve a partial guess */
    out->has_label = 0;
    out->label[0] = '\0';
    out->has_op = 0;
    out->op[0] = '\0';
    out->operand[0] = '\0';
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
/* Macros                                                                  */
/* --------------------------------------------------------------------- */
/*
 * Macro expansion is a preprocessing step over raw source *text*, run
 * before split_line() ever sees a line -- it knows nothing about
 * labels, opcodes, or addressing modes, only about ".macro"/".endmacro"
 * blocks, parameter substitution, and recognizing when a line invokes a
 * macro name instead of a real mnemonic.
 *
 * Syntax:
 *     .macro NAME param1, param2
 *             ; body, referencing \param1, \param2
 *     .endmacro
 * invoked like a pseudo-instruction:
 *     NAME arg1, arg2
 *
 * Local labels use this same preprocessing layer. A label name starting
 * with '@' (e.g. "@loop") is textually rewritten to a scope-specific
 * global name (e.g. "__local5_loop") before split_line() ever sees it --
 * so as far as the rest of the assembler (symbol table, expression
 * evaluator, everything) is concerned, it's just an ordinary label; all
 * the "local" behavior lives entirely in this rewriting step. A new
 * scope begins each time an ordinary ("identifier:") global label is
 * defined, *and* each time a macro invocation begins expanding (with
 * the previous scope restored once that invocation's body is fully
 * processed) -- which is what makes a macro's own @-labels distinct on
 * every separate invocation, automatically, with no suffix parameter or
 * other caller-side bookkeeping required. A reference to an @-label
 * from outside the scope it was defined in mangles to a name that was
 * never actually defined, and so becomes an ordinary "undefined symbol"
 * error at assembly time -- scope violations are caught by the existing
 * machinery for free, without any dedicated scope-checking code.
 *
 * Deliberate limitations (documented in c64asm-reference.md, not
 * oversights):
 *   - Macros must be defined before they're used.
 *   - A macro invocation can't share a line with a label.
 *   - Macro arguments are split the same comma/paren/quote-aware way
 *     directive argument lists are (see split_args() above) -- which
 *     means a full indexed operand like "(ptr),Y" can't be passed as a
 *     single argument, since its comma sits outside any parentheses.
 *     Parameterize the base address and bake the ",Y" into the macro
 *     body instead.
 *   - A new local-label scope is only recognized from the explicit
 *     "identifier:" form -- a bare label with no colon doesn't start a
 *     new scope.
 *   - '@' inside a double-quoted string (e.g. .text "user@example.com")
 *     is left alone, not mangled.
 */

#define MAX_MACROS 128
#define MAX_MACRO_PARAMS 8
#define MAX_MACRO_BODY_LINES 200
#define MAX_MACRO_EXPANSION_DEPTH 16
#define MAX_REPEAT_COUNT 65536   /* guards against a mistyped .repeat/.dup
                                     count (e.g. a stray extra digit)
                                     generating an enormous,
                                     memory-exhausting expansion */
#define MAX_REPEAT_BODY_LINES 200

#define LOCAL_LABEL_PREFIX "__local"

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

/* current_scope: the scope id used to mangle @names right now.
 * next_scope: an ever-increasing counter; a fresh value is handed out
 *             for every new scope, so no two scopes ever share an id.
 * scope_stack: saved current_scope values, for restoring the enclosing
 *              scope after a (possibly nested) macro expansion. Sized
 *              for MAX_MACRO_EXPANSION_DEPTH nested invocations, which
 *              is also the hard limit macro_process_line enforces. */
static int current_scope = 0;
static int next_scope = 1;
static int scope_stack[MAX_MACRO_EXPANSION_DEPTH + 1];
static int scope_stack_top = 0;

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

/* True if `trimmed` starts with an identifier (not starting with '@')
 * immediately followed by ':' -- the one thing that advances the
 * local-label scope for ordinary, non-macro code. See the file header
 * comment above for why only this specific form is recognized. */
static int line_defines_global_label(const char *trimmed) {
    if (trimmed[0] == '\0' || trimmed[0] == '@' || !is_ident_start(trimmed[0]))
        return 0;
    size_t i = 0;
    while (trimmed[i] && is_ident_char(trimmed[i])) i++;
    return trimmed[i] == ':';
}

/* Parses '.repeat'/'.dup's count argument -- a single plain integer
 * literal (decimal, $hex, or %binary), deliberately NOT a full
 * expression: this runs during macro/include preprocessing, entirely
 * before pass 1 even starts building a symbol table, so there's no way
 * to look up a label or forward-declared constant here even if the
 * syntax allowed writing one. */
/* Forward-declared here since expand_repeat() below needs to call it
 * (to process each expanded body line, exactly like a macro
 * invocation's own body lines are processed), but its real definition
 * comes later in this file, same as it already does for ordinary
 * macro invocation. */
static void macro_process_line(const char *raw_line, const char *filename, int line_no);

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
 * macro_substitute() and per-invocation local-label scoping every
 * ordinary macro invocation already gets. */
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
        scope_stack[scope_stack_top++] = current_scope;
        current_scope = next_scope;
        next_scope++;

        char arg[32];
        snprintf(arg, sizeof(arg), "%ld", i);
        char (*args)[MAX_LINE_LEN] = NULL;
        char single_arg[1][MAX_LINE_LEN];
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

        current_scope = scope_stack[--scope_stack_top];
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

/* Replaces every @name in `text` with a scope-specific global name,
 * skipping anything inside a double-quoted string. */
static void mangle_local_labels(const char *text, int scope_id, char *out, size_t outsz) {
    size_t oi = 0;
    size_t n = strlen(text);
    int in_str = 0;
    for (size_t i = 0; i < n && oi + 1 < outsz; ) {
        char c = text[i];
        if (c == '"') {
            in_str = !in_str;
            out[oi++] = c; i++;
        } else if (c == '@' && !in_str && i + 1 < n &&
                   (isalpha((unsigned char)text[i+1]) || text[i+1] == '_')) {
            size_t j = i + 1;
            while (j < n && (isalnum((unsigned char)text[j]) || text[j] == '_')) j++;
            int written = snprintf(out + oi, outsz - oi, "%s%d_%.*s",
                                    LOCAL_LABEL_PREFIX, scope_id, (int)(j - (i+1)), text + i + 1);
            oi += (written > 0) ? (size_t)written : 0;
            if (oi > outsz - 1) oi = outsz - 1;
            i = j;
        } else {
            out[oi++] = c; i++;
        }
    }
    out[oi] = '\0';
}

/* --------------------------------------------------------------------- */
/* Includes                                                                */
/* --------------------------------------------------------------------- */
/*
 * .include "path" splices another file's lines into the source stream
 * at that point, as if they'd been pasted in directly -- resolved
 * relative to the directory of the file *containing* the .include line
 * (not the current working directory), which is what lets a library
 * file .include another library file sitting next to it, regardless of
 * where the assembler itself was invoked from.
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
 * less readable absolute path -- and is also why canonical paths get
 * their own, separate, full PATH_MAX-sized buffers (small, fixed-size
 * arrays here, not multiplied by MAX_LINES) rather than reusing
 * MAX_FILENAME_LEN.
 */

#define MAX_INCLUDE_DEPTH 16
#define MAX_COND_DEPTH 16      /* guards against runaway .if/.ifdef nesting */
#define MAX_INCLUDED_FILES 256   /* total distinct files includable in one run */

static char open_stack_canon[MAX_INCLUDE_DEPTH + 1][PATH_MAX];
static char open_stack_display[MAX_INCLUDE_DEPTH + 1][MAX_FILENAME_LEN];
static int open_stack_top = 0;

static char already_included[MAX_INCLUDED_FILES][PATH_MAX];
static int already_included_count = 0;

/* --lib-dir's value, or NULL if not given -- see process_include_file()
 * for how it's used as a fallback search root. */
static const char *g_lib_dir = NULL;

/* Forward declaration: process_include_file() (below) needs to feed
 * each line it reads back through macro_process_line(), while
 * macro_process_line() (further below) needs to call
 * process_include_file() when it recognizes a ".include" line -- a
 * genuine mutual recursion between the two. */
static void macro_process_line(const char *raw_line, const char *filename, int line_no);

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

/*
 * Resolves, opens, and processes one source file -- the main file
 * (including_file == NULL) or a .include'd one. Every line read is fed
 * to macro_process_line() with `filename` set to this file's resolved
 * display name and `line_no` counted 1-based within it.
 */
/* Resolves a quoted path from '.include'/'.incbin' to an actual file
 * on disk -- shared by process_include_file() below and '.incbin's own
 * handling in run_pass(), so the two follow identical rules: relative
 * to the file containing the directive first (the default, always
 * tried first), then --lib-dir as a fallback.
 *
 * --lib-dir is purely a fallback: the default resolution above is
 * always tried first, and if it finds the file, --lib-dir is never
 * even consulted -- so a project with its own local lib/ next to it
 * keeps working unchanged whether or not --lib-dir is given. Only when
 * that default lookup comes up empty, and --lib-dir was given, and the
 * requested path isn't absolute (an absolute path is never subject to
 * search-path fallback, same as the default resolution above), do we
 * also try requested_path relative to --lib-dir.
 *
 * --lib-dir names the lib/ directory itself (the one holding text.inc,
 * input.inc, ...), not its parent -- so a leading "lib/" in the
 * requested path (this project's own convention, e.g.
 * `.include "lib/text.inc"`) is stripped before joining with
 * --lib-dir, or `--lib-dir /shared/c64lib` would end up looking for
 * /shared/c64lib/lib/text.inc, one "lib" too many. A requested path
 * that doesn't start with "lib/" is joined as-is.
 *
 * resolved_display receives where the file was actually found (or the
 * primary path attempted, if neither location has it). lib_dir_display
 * receives the --lib-dir fallback path that was tried; *tried_lib_dir
 * is set to 1 if it was consulted at all (callers use this, plus a
 * strcmp against resolved_display, to decide whether an error message
 * should mention it), 0 otherwise. */
static void resolve_asset_path(const char *requested_path, const char *including_file,
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

static void process_include_file(const char *requested_path, const char *including_file,
                                  int including_line_no, const char *including_raw) {
    char resolved_display[MAX_FILENAME_LEN];
    char lib_dir_display[MAX_FILENAME_LEN];
    int tried_lib_dir;
    resolve_asset_path(requested_path, including_file, resolved_display, sizeof(resolved_display),
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
    set_error_file(resolved_display);

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
        macro_process_line(buf, resolved_display, line_no);
    }
    fclose(f);

    open_stack_top--;
    set_error_file(open_stack_top > 0 ? open_stack_display[open_stack_top - 1] : NULL);
    if (already_included_count < MAX_INCLUDED_FILES) {
        strncpy(already_included[already_included_count], canon, PATH_MAX - 1);
        already_included[already_included_count][PATH_MAX - 1] = '\0';
        already_included_count++;
    }
}

/* Appends one fully-resolved (non-macro) line to g_lines, the same way
 * load_source()'s loop used to do inline before macro support existed.
 * This is the single choke point every genuinely final line passes
 * through, whether it came from ordinary source text or from expanding
 * a macro body -- so it's also where the local-label scope is advanced
 * (on an ordinary global-label line) and where @name mangling happens. */
static void emit_source_line(const char *raw_line, const char *filename, int line_no) {
    if (g_line_count >= MAX_LINES)
        asm_error(line_no, raw_line, "Too many source lines (max %d)", MAX_LINES);

    char line[MAX_LINE_LEN];
    strncpy(line, raw_line, sizeof(line) - 1); line[sizeof(line)-1] = '\0';
    strip_comment(line);
    size_t ln = strlen(line);
    while (ln > 0 && (line[ln-1]=='\n' || line[ln-1]=='\r')) line[--ln]='\0';
    trim(line);
    if (line_defines_global_label(line)) {
        current_scope = next_scope;
        next_scope++;
    }

    char mangled[MAX_LINE_LEN];
    mangle_local_labels(raw_line, current_scope, mangled, sizeof(mangled));

    split_line(mangled, line_no, &g_lines[g_line_count]);
    strncpy(g_lines[g_line_count].filename, filename ? filename : "", MAX_FILENAME_LEN - 1);
    g_lines[g_line_count].filename[MAX_FILENAME_LEN - 1] = '\0';
    g_line_count++;
}

/*
 * Feeds one raw source line through macro processing: absorbs lines
 * that are part of an in-progress ".macro"/".endmacro" definition,
 * splices in another file's content on ".include" (recursively --
 * see process_include_file() above), expands macro invocations
 * (recursively, via calling itself again on each substituted body
 * line -- which is how nested macro calls and nested ".macro" body
 * content both "just work"), and passes every other line straight
 * through to emit_source_line().
 *
 * filename is threaded alongside line_no everywhere line_no already
 * flows through this function, including through macro expansion --
 * which means an error inside an expanded macro body is attributed to
 * the file *and* line of the invocation, not wherever the macro itself
 * happened to be defined, consistent with how line numbers already
 * worked before .include existed.
 */
static void macro_process_line(const char *raw_line, const char *filename, int line_no) {
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
        g_multi_file_mode = 1;
        process_include_file(path, filename, line_no, raw_line);
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

        /* A fresh, globally-unique scope for this invocation -- every
         * invocation, even of the same macro, even nested calls, gets
         * one it will never share with any other invocation. The stack
         * is sized for MAX_MACRO_EXPANSION_DEPTH entries, matching the
         * depth check just above, so this can never overflow it. */
        scope_stack[scope_stack_top++] = current_scope;
        current_scope = next_scope;
        next_scope++;

        for (int i = 0; i < m->body_line_count; i++) {
            char substituted[MAX_LINE_LEN];
            macro_substitute(m->body[i], m, args, substituted, sizeof(substituted), line_no);
            macro_process_line(substituted, filename, line_no);
        }

        current_scope = scope_stack[--scope_stack_top];
        macro_expansion_depth--;
        return;
    }

    emit_source_line(raw_line, filename, line_no);
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
        asm_error_recoverable(line_no, raw, "%s requires an operand", mnemonic);
        *undef_out = 1;
        return M_IMP;   /* fallback: smallest footprint for "we don't know
                            what was meant" */
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
        if (close < 0) {
            asm_error_recoverable(line_no, raw, "Unbalanced parentheses in operand '%s'", op);
            *undef_out = 1;
            return M_IMP;   /* fallback: don't try to index into `op` using
                                an invalid close-paren position below */
        }
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
                    *val_out = eval_expr(expr, pc, line_no, undef_out);
                    if (e->op[M_INDX] == -1) {
                        asm_error_recoverable(line_no, raw, "%s does not support (zp,X) addressing", mnemonic);
                        *undef_out = 1;
                    }
                    return M_INDX;   /* still returned even when unsupported --
                                        the pass-2 "Invalid addressing mode"
                                        check catches it a second time, same
                                        as the Python implementation */
                }
            }
            /* plain indirect (JMP) */
            *val_out = eval_expr(inner, pc, line_no, undef_out);
            if (e->op[M_IND] == -1) {
                asm_error_recoverable(line_no, raw, "%s does not support indirect addressing", mnemonic);
                *undef_out = 1;
            }
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
                    *val_out = eval_expr(inner, pc, line_no, undef_out);
                    if (e->op[M_INDY] == -1) {
                        asm_error_recoverable(line_no, raw, "%s does not support (zp),Y addressing", mnemonic);
                        *undef_out = 1;
                    }
                    return M_INDY;
                }
            }
            asm_error_recoverable(line_no, raw, "%s does not support that addressing mode ('%s')", mnemonic, op);
            *undef_out = 1;
            return M_IMP;
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
                    asm_error_recoverable(line_no, raw, "%s does not support that addressing mode", mnemonic);
                    *undef_out = 1;
                    if (is_x) return is_zp ? M_ZPX : M_ABSX;   /* fallback: best-guess
                                                                   mode; pass-2's "Invalid
                                                                   addressing mode" check
                                                                   will still catch it if
                                                                   that guess is wrong */
                    return is_zp ? M_ZPY : M_ABSY;
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
        asm_error_recoverable(line_no, raw, "%s does not support that addressing mode", mnemonic);
        *undef_out = 1;
        return is_zp ? M_ZP : M_ABS;   /* fallback: best-guess mode; pass-2's
                                           "Invalid addressing mode" check
                                           will still catch it */
    }
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
/* Conditional assembly                                                   */
/* --------------------------------------------------------------------- */
/*
 * .if expr / .elif expr / .else / .endif
 * .ifdef NAME / .else / .endif
 * .ifndef NAME / .else / .endif
 *
 * Unlike macros and .include, this is an *assembly-time* feature,
 * handled directly in run_pass() below rather than as a preprocessing
 * step -- deliberately, so a condition can see real constants and
 * labels (like a PAL/NTSC flag defined with "="), not just things known
 * before any real parsing happens. The trade-off: .if can gate whether
 * instructions and data get assembled, but it can't gate which .macro
 * gets *defined* or which file gets .include'd, since those are already
 * fully resolved before .if is ever evaluated.
 *
 * Two correctness requirements come directly from this being a two-pass
 * assembler, and are easy to get wrong:
 *
 *  1. ".if"/".elif" conditions must not reference a forward-declared
 *     symbol -- this is an unconditional error, checked the same way on
 *     both passes, *not* deferred to pass 2 the way other expressions'
 *     undefined-symbol checks are (see .org/.align above). The reason:
 *     for an ordinary expression, pass 1 guessing wrong about an
 *     undefined symbol's value only affects a byte *value*, silently
 *     corrected once pass 2 knows better. For "if", a wrong guess
 *     changes which lines exist at all -- which would desynchronize
 *     every address computed after it between the two passes. Requiring
 *     the condition to be fully known equally on both passes is what
 *     keeps that from ever happening.
 *
 *  2. ".ifdef"/".ifndef" must NOT simply check "is this symbol in the
 *     symbol table right now". symtab is never reset between pass 1
 *     and pass 2 (pass 2 needs pass 1's complete table to resolve
 *     forward references) -- which means by the time pass 2 *starts*,
 *     the table already contains every symbol defined anywhere in the
 *     file, including ones that don't textually appear until later. A
 *     plain find_symbol() != NULL check would see "not defined" during
 *     pass 1 (walking forward, symbol not reached yet) but "defined"
 *     during pass 2, for the exact same .ifdef line -- the two passes
 *     would disagree about whether that line's block even exists. The
 *     fix: find_symbol_defined_before() (above) asks "was it defined
 *     strictly before this line's index" rather than "does it exist
 *     right now". Since both passes walk g_lines in the same order,
 *     that question has the same answer on both passes.
 */

typedef struct {
    int is_ifdef_style;    /* true for .ifdef/.ifndef, which don't allow .elif */
    int currently_active;  /* is the branch we're IN right now (if/elif/else)
                               the active one? */
    int condition_met;     /* has ANY branch in this block already been
                               taken? (stops a later .elif/.else from also
                               activating) */
    int opened_line_no;    /* where this block's own .if/.ifdef/.ifndef line
                               was, for a helpful "unclosed at end of file"
                               error message */
    char opened_raw[MAX_LINE_LEN];
} CondFrame;

/* --------------------------------------------------------------------- */
/* Assembler pass                                                         */
/* --------------------------------------------------------------------- */

/* Returns whether the CondFrame at stack[idx] is active, treating an
 * index before the start of the stack (idx < 0) as "active" -- that
 * represents being outside of any .if block at all, i.e. the top
 * level, which is always active. Centralizing this avoids repeating
 * the same "is there even a frame there" check at every place that
 * needs to ask "is my enclosing context active", and keeps that check
 * from ever looking, to a reader or a static analyzer, like an
 * unguarded out-of-bounds index. */
static int cond_frame_active(CondFrame *stack, int idx) {
    return idx < 0 || stack[idx].currently_active;
}

static long run_pass(int pass_no, ByteBuf *output, long *origin_out) {
    long pc = 0x0801;
    long origin = -1;
    CondFrame cond_stack[MAX_COND_DEPTH + 1];
    int cond_stack_top = 0;
    int illegal_enabled = 0;   /* toggled by '.cpu 6510x'/'.cpu 6510' --
                                   see that directive's handling below */
    int charset_lower = 0;     /* toggled by '.charset lower'/'.charset
                                   upper' -- see that directive's handling
                                   below */

    for (int li = 0; li < g_line_count; li++) {
        SourceLine *L = &g_lines[li];
        set_error_file(L->filename[0] ? L->filename : NULL);
        long entry_pc = pc;

        int parent_active = cond_frame_active(cond_stack, cond_stack_top - 1);

        if (strcasecmp(L->op, ".if") == 0 || strcasecmp(L->op, ".ifdef") == 0 ||
            strcasecmp(L->op, ".ifndef") == 0) {
            if (cond_stack_top >= MAX_COND_DEPTH)
                asm_error(L->line_no, L->raw, "conditional nesting too deep (max %d)", MAX_COND_DEPTH);
            CondFrame *f = &cond_stack[cond_stack_top++];
            f->is_ifdef_style = (strcasecmp(L->op, ".if") != 0);
            f->opened_line_no = L->line_no;
            strncpy(f->opened_raw, L->raw, sizeof(f->opened_raw) - 1);
            f->opened_raw[sizeof(f->opened_raw) - 1] = '\0';
            if (!parent_active) {
                /* An enclosing branch is already false, so this whole
                 * block is dead regardless of its own condition --
                 * don't even evaluate it (it may reference symbols that
                 * don't exist, which would otherwise be a spurious
                 * error for code that was never going to run anyway),
                 * and make sure none of ITS .elif/.else branches can
                 * activate either. */
                f->currently_active = 0;
                f->condition_met = 1;
            } else if (strcasecmp(L->op, ".if") == 0) {
                int undef = 0;
                long v = eval_expr(L->operand, pc, L->line_no, &undef);
                if (undef)
                    asm_error(L->line_no, L->raw,
                        "Undefined symbol in .if/.elif expression -- forward references "
                        "are not allowed in conditional-assembly expressions");
                f->currently_active = (v != 0);
                f->condition_met = f->currently_active;
            } else {
                char name[MAX_LINE_LEN];
                strncpy(name, L->operand, sizeof(name) - 1); name[sizeof(name)-1] = '\0';
                trim(name);
                int is_defined = (find_symbol_defined_before(name, li) != NULL);
                int cond = (strcasecmp(L->op, ".ifdef") == 0) ? is_defined : !is_defined;
                f->currently_active = cond;
                f->condition_met = cond;
            }
            continue;
        }

        if (strcasecmp(L->op, ".elif") == 0) {
            if (cond_stack_top == 0) {
                asm_error(L->line_no, L->raw, "'.elif' with no matching '.if'");
            } else {
                CondFrame *f = &cond_stack[cond_stack_top-1];
                if (f->is_ifdef_style)
                    asm_error(L->line_no, L->raw,
                        "'.elif' is not allowed after '.ifdef'/'.ifndef' (only after '.if')");
                int outer_ok = cond_frame_active(cond_stack, cond_stack_top - 2);
                if (!outer_ok || f->condition_met) {
                    f->currently_active = 0;
                } else {
                    int undef = 0;
                    long v = eval_expr(L->operand, pc, L->line_no, &undef);
                    if (undef)
                        asm_error(L->line_no, L->raw,
                            "Undefined symbol in .if/.elif expression -- forward references "
                            "are not allowed in conditional-assembly expressions");
                    f->currently_active = (v != 0);
                    if (f->currently_active) f->condition_met = 1;
                }
            }
            continue;
        }

        if (strcasecmp(L->op, ".else") == 0) {
            if (cond_stack_top == 0) {
                asm_error(L->line_no, L->raw, "'.else' with no matching '.if'");
            } else {
                CondFrame *f = &cond_stack[cond_stack_top-1];
                int outer_ok = cond_frame_active(cond_stack, cond_stack_top - 2);
                f->currently_active = outer_ok && !f->condition_met;
                f->condition_met = 1;
            }
            continue;
        }

        if (strcasecmp(L->op, ".endif") == 0) {
            if (cond_stack_top == 0)
                asm_error(L->line_no, L->raw, "'.endif' with no matching '.if'/'.ifdef'/'.ifndef'");
            cond_stack_top--;
            continue;
        }

        if (cond_stack_top > 0 && !cond_stack[cond_stack_top-1].currently_active)
            continue;

        if (!L->has_op) {
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
            continue;
        }

        if (strcmp(L->op, ".basic") == 0) {
            unsigned char stub[64];
            long code_start;
            int slen = basic_stub_fixed_point(stub, &code_start);
            if (pass_no == 2) bb_push_n(output, stub, slen);
            origin = 0x0801;
            pc = code_start;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
            if (L->operand[0] != '\0') {
                /* An explicit start label: emit `jmp <label>` right
                 * after the stub, so SYS always lands at the real
                 * entry point even if code-emitting .include lines (a
                 * library's own routines, say) sit between here and
                 * the label -- forgetting this by hand was a
                 * recurring, hard-to-spot bug (SYS silently landing
                 * inside the first included routine instead). */
                int undef = 0;
                long target = eval_expr(L->operand, pc, L->line_no, &undef);
                if (undef && pass_no == 2)
                    asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .basic start operand '%s'", L->operand);
                if (pass_no == 2) {
                    unsigned char jmp_bytes[3];
                    jmp_bytes[0] = 0x4C;
                    jmp_bytes[1] = (unsigned char)(target & 0xFF);
                    jmp_bytes[2] = (unsigned char)((target >> 8) & 0xFF);
                    bb_push_n(output, jmp_bytes, 3);
                    listing_add(pc, L->raw, jmp_bytes, 3);
                }
                pc += 3;
            }
            continue;
        }

        if (strcmp(L->op, ".cpu") == 0) {
            /* Switches illegal/undocumented-opcode support on or off
             * from this point in the file forward (not retroactively --
             * a mnemonic used above this line is checked against
             * whatever '.cpu' setting was active there, not this one).
             * See init_opcodes()'s SETOP_ILLEGAL() calls and
             * c64asm-reference.md's "Illegal opcodes" section for the
             * full explanation. */
            char mode_name[MAX_LINE_LEN];
            strncpy(mode_name, L->operand, sizeof(mode_name) - 1);
            mode_name[sizeof(mode_name) - 1] = '\0';
            trim(mode_name);
            char mode_upper[MAX_LINE_LEN];
            strncpy(mode_upper, mode_name, sizeof(mode_upper) - 1);
            mode_upper[sizeof(mode_upper) - 1] = '\0';
            for (char *pc2 = mode_upper; *pc2; pc2++) *pc2 = (char)toupper((unsigned char)*pc2);
            if (strcmp(mode_upper, "6510X") == 0) {
                illegal_enabled = 1;
            } else if (strcmp(mode_upper, "6510") == 0 || strcmp(mode_upper, "6502") == 0) {
                illegal_enabled = 0;
            } else {
                asm_error_recoverable(L->line_no, L->raw,
                    "Unknown .cpu mode '%s' -- expected '6510' (standard, the "
                    "default) or '6510x' (enables illegal/undocumented opcodes)",
                    mode_name);
                /* fallback: leave illegal_enabled exactly as it was -- an
                   unrecognized mode isn't a request to change anything,
                   just a mistake to report */
            }
            continue;
        }

        if (strcmp(L->op, ".error") == 0 || strcmp(L->op, ".warning") == 0) {
            /* A source-author-placed diagnostic -- typically paired
             * with .ifdef/.ifndef to check a precondition (a required
             * zero-page symbol defined, say) and fail with a clear,
             * specific message right at the point of the mistake,
             * instead of a confusing "Undefined symbol" buried inside
             * a macro expansion three files away:
             *
             *       .ifndef gfx_ptr
             *       .error "graphics.inc requires gfx_ptr (2-byte zero page)"
             *       .endif
             *
             * '.error' is recoverable, not fatal -- several
             * independent '.error's (e.g. two different missing
             * zero-page symbols in two different included files) can
             * all be collected and reported together in one run.
             * '.warning' never stops assembly or affects the exit
             * status at all; it's only gated to pass 2 here so its
             * message prints exactly once, not twice (once per pass).
             */
            char msg_text[MAX_LINE_LEN];
            strncpy(msg_text, L->operand, sizeof(msg_text) - 1);
            msg_text[sizeof(msg_text) - 1] = '\0';
            trim(msg_text);
            size_t mlen = strlen(msg_text);
            int is_warning = (strcmp(L->op, ".warning") == 0);
            if (mlen >= 2 && msg_text[0] == '"' && msg_text[mlen-1] == '"') {
                msg_text[mlen-1] = '\0';
                memmove(msg_text, msg_text + 1, mlen - 1);
            } else {
                asm_error_recoverable(L->line_no, L->raw,
                    "%s requires a quoted message string, e.g. %s \"message\"",
                    L->op, L->op);
                /* fallback: nothing else to do with a malformed
                   directive -- there's no message to act on */
                continue;
            }
            if (is_warning) {
                if (pass_no == 2) asm_warning(L->line_no, L->raw, "%s", msg_text);
            } else {
                asm_error_recoverable(L->line_no, L->raw, "%s", msg_text);
            }
            continue;
        }

        if (strcmp(L->op, ".assert") == 0) {
            /* Fails assembly (recoverably -- see '.error' just above)
             * if `condition` evaluates to 0, e.g. to catch a struct
             * changing shape out from under code that assumed a
             * specific size:
             *
             *       .assert Exits.size == 4, "compute_room_exits_offset assumes 4 fields"
             *
             * The message is optional; without one, the condition's
             * own source text stands in for it. Only checked on pass
             * 2 -- during pass 1, a symbol the condition depends on
             * may still be an unresolved forward reference, standing
             * in as 0, which would make an otherwise-true condition
             * look spuriously false. */
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            if (nargs < 1) {
                asm_error_recoverable(L->line_no, L->raw,
                    ".assert requires a condition, e.g. .assert Exits.size == 4");
                continue;
            }
            if (nargs > 2) {
                asm_error_recoverable(L->line_no, L->raw,
                    ".assert takes at most a condition and a quoted message");
                continue;
            }
            char msg_text[MAX_LINE_LEN];
            int has_msg = 0;
            if (nargs > 1) {
                strncpy(msg_text, args[1], sizeof(msg_text) - 1);
                msg_text[sizeof(msg_text) - 1] = '\0';
                trim(msg_text);
                size_t mlen = strlen(msg_text);
                if (mlen >= 2 && msg_text[0] == '"' && msg_text[mlen-1] == '"') {
                    msg_text[mlen-1] = '\0';
                    memmove(msg_text, msg_text + 1, mlen - 1);
                    has_msg = 1;
                } else {
                    asm_error_recoverable(L->line_no, L->raw,
                        ".assert's message must be a quoted string, e.g. "
                        ".assert Exits.size == 4, \"message\"");
                    continue;
                }
            }
            int undef = 0;
            long val = eval_expr(args[0], pc, L->line_no, &undef);
            if (undef && pass_no == 2) {
                asm_error_recoverable(L->line_no, L->raw,
                    "Undefined symbol in .assert condition '%s'", args[0]);
                continue;
            }
            if (pass_no == 2 && val == 0) {
                if (has_msg)
                    asm_error_recoverable(L->line_no, L->raw, "%s", msg_text);
                else
                    asm_error_recoverable(L->line_no, L->raw, "Assertion failed: %s", args[0]);
            }
            continue;
        }

        if (strcmp(L->op, ".charset") == 0) {
            /* Switches how .text/.asc/.byte string literals encode
             * letters, from this point in the file forward (not
             * retroactively -- same positional behavior as '.cpu'
             * above). See ascii_to_petscii() and c64asm-reference.md's
             * "Text and PETSCII" section for the full explanation,
             * including the important caveat about mixing '.charset
             * upper' and '.charset lower' text in a program that
             * switches its character set at runtime. */
            char cs_mode[MAX_LINE_LEN];
            strncpy(cs_mode, L->operand, sizeof(cs_mode) - 1);
            cs_mode[sizeof(cs_mode) - 1] = '\0';
            trim(cs_mode);
            char cs_upper[MAX_LINE_LEN];
            strncpy(cs_upper, cs_mode, sizeof(cs_upper) - 1);
            cs_upper[sizeof(cs_upper) - 1] = '\0';
            for (char *pc2 = cs_upper; *pc2; pc2++) *pc2 = (char)toupper((unsigned char)*pc2);
            if (strcmp(cs_upper, "UPPER") == 0) {
                charset_lower = 0;
            } else if (strcmp(cs_upper, "LOWER") == 0) {
                charset_lower = 1;
            } else {
                asm_error_recoverable(L->line_no, L->raw,
                    "Unknown .charset mode '%s' -- expected 'upper' (the "
                    "default) or 'lower'", cs_mode);
                /* fallback: leave charset_lower exactly as it was --
                   same reasoning as '.cpu''s fallback above */
            }
            continue;
        }

        if (strcmp(L->op, ".org") == 0) {
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            if (undef && pass_no == 2) {
                asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .org expression");
                /* fallback: leave pc wherever it already was -- an unknown
                   target isn't something to guess at, and this is at
                   least deterministic for whatever comes next */
            } else if (origin < 0) {
                origin = val;
                pc = val;
            } else if (pass_no == 2) {
                long current_abs = origin + (long)output->len;
                long gap = val - current_abs;
                if (gap < 0) {
                    asm_error_recoverable(L->line_no, L->raw,
                        ".org cannot move the program counter backward (from $%04lX to $%04lX) "
                        "-- the assembler can't overwrite bytes already assembled",
                        current_abs, val);
                    /* fallback: same as the undefined-symbol case above --
                       don't move pc to an invalid target */
                } else {
                    for (long i = 0; i < gap; i++) bb_push(output, 0x00);
                    pc = val;
                }
            } else {
                pc = val;
            }
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
            continue;
        }

        if (strcasecmp(L->op, ".align") == 0) {
            /* Advances pc to the next multiple of `n`, padding the
             * skipped bytes with zero -- exactly .org's forward-gap
             * logic above, just with the target computed by rounding
             * up rather than given directly. pc can never move
             * *backward* here (target is always >= pc by construction),
             * so there's no equivalent of .org's "moving backward"
             * error to check for. */
            int undef = 0;
            long n = eval_expr(L->operand, pc, L->line_no, &undef);
            if (undef && pass_no == 2)
                asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .align expression");
            long target;
            if (undef) {
                /* Forward-referenced alignment value, pass 1 only (pass
                 * 2 would already have raised above). n is just
                 * eval_expr's undefined-symbol placeholder (0) here,
                 * not a real value -- validating its sign or dividing
                 * by it would be meaningless (and, for 0 specifically,
                 * actual division-by-zero undefined behavior in C), so
                 * pc simply doesn't advance this pass. That never
                 * produces incorrect output: pass 2 always catches the
                 * undefined symbol and aborts before anything computed
                 * from a wrong pass-1 address could ship. */
                target = pc;
            } else if (n <= 0) {
                asm_error_recoverable(L->line_no, L->raw,
                    ".align requires a positive alignment value (got %ld)", n);
                target = pc;   /* fallback: don't move pc -- necessary, not
                                  just consistent: n<=0 below would divide
                                  by zero (n==0) or produce a nonsensical
                                  negative gap (n<0) */
            } else {
                target = ((pc + n - 1) / n) * n;
            }
            if (pass_no == 2) {
                long gap = target - pc;
                for (long i = 0; i < gap; i++) bb_push(output, 0x00);
            }
            pc = target;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
            continue;
        }

        if (strcmp(L->op, "=") == 0) {
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            define_symbol(L->label, val, L->line_no, pass_no, 1, L->raw, li);
            continue;
        }

        if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);

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
                    ascii_to_petscii(s, buf, &blen, charset_lower);
                    if (pass_no == 2) bb_push_n(output, buf, blen);
                    pc += blen;
                } else {
                    int undef = 0;
                    long v = eval_expr(a, pc, L->line_no, &undef);
                    if (undef && pass_no == 2)
                        asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .byte '%s'", a);
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
                    asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .word '%s'", args[i]);
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
                if (a[0] == '"') {
                    char s[MAX_LINE_LEN]; size_t al = strlen(a);
                    size_t sl = (al >= 2 && a[al-1]=='"') ? al-2 : al-1;
                    memcpy(s, a+1, sl); s[sl]='\0';
                    unsigned char buf[MAX_LINE_LEN]; int blen=0;
                    ascii_to_petscii(s, buf, &blen, charset_lower);
                    if (pass_no == 2) bb_push_n(output, buf, blen);
                    pc += blen;
                } else {
                    int undef = 0;
                    long v = eval_expr(a, pc, L->line_no, &undef);
                    if (undef && pass_no == 2)
                        asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .text '%s'", a);
                    if (pass_no == 2) bb_push(output, (unsigned char)(v & 0xFF));
                    pc += 1;
                }
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

        if (strcasecmp(L->op, ".incbin") == 0) {
            /* Unlike .byte/.text's undefined-symbol errors, every
             * error path below is fatal, not recoverable -- an
             * .incbin problem means the assembler doesn't know how
             * many bytes this line emits, which (unlike an ordinary
             * .byte's *value* being wrong) would throw off every
             * address computed after it, the same class of problem a
             * missing .include'd file is. */
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            if (nargs < 1) asm_error(L->line_no, L->raw,
                ".incbin requires a quoted path, e.g. .incbin \"sprite.bin\"");
            trim(args[0]);
            size_t flen = strlen(args[0]);
            if (flen < 2 || args[0][0] != '"' || args[0][flen-1] != '"')
                asm_error(L->line_no, L->raw,
                    ".incbin requires a quoted path, e.g. .incbin \"sprite.bin\"");
            if (nargs > 3) asm_error(L->line_no, L->raw,
                ".incbin takes at most a path, an offset, and a length");

            char path[MAX_LINE_LEN];
            size_t plen = flen - 2;
            memcpy(path, args[0] + 1, plen); path[plen] = '\0';

            long offset = 0, length = 0;
            int length_given = 0;
            if (nargs > 1) {
                int off_undef = 0;
                offset = eval_expr(args[1], pc, L->line_no, &off_undef);
                if (off_undef) asm_error(L->line_no, L->raw,
                    "Undefined symbol in .incbin offset expression");
            }
            if (nargs > 2) {
                int len_undef = 0;
                length_given = 1;
                length = eval_expr(args[2], pc, L->line_no, &len_undef);
                if (len_undef) asm_error(L->line_no, L->raw,
                    "Undefined symbol in .incbin length expression");
            }

            char resolved_display[MAX_FILENAME_LEN];
            char lib_dir_display[MAX_FILENAME_LEN];
            int tried_lib_dir;
            resolve_asset_path(path, L->filename[0] ? L->filename : NULL,
                                resolved_display, sizeof(resolved_display),
                                lib_dir_display, sizeof(lib_dir_display), &tried_lib_dir);

            FILE *bf = fopen(resolved_display, "rb");
            if (!bf) {
                if (tried_lib_dir && strcmp(resolved_display, lib_dir_display) != 0)
                    asm_error(L->line_no, L->raw,
                        "Cannot open included binary file '%s' (also tried '%s' via --lib-dir)",
                        resolved_display, lib_dir_display);
                else
                    asm_error(L->line_no, L->raw,
                        "Cannot open included binary file '%s'", resolved_display);
            }
            if (fseek(bf, 0, SEEK_END) != 0)
                asm_error(L->line_no, L->raw, "Cannot determine size of '%s'", resolved_display);
            long filesize = ftell(bf);
            if (filesize < 0)
                asm_error(L->line_no, L->raw, "Cannot determine size of '%s'", resolved_display);

            if (offset < 0 || offset > filesize)
                asm_error(L->line_no, L->raw,
                    ".incbin offset %ld is out of range for '%s' (%ld bytes)",
                    offset, resolved_display, filesize);
            if (!length_given) length = filesize - offset;
            if (length < 0 || offset + length > filesize)
                asm_error(L->line_no, L->raw,
                    ".incbin length %ld (from offset %ld) exceeds the size of '%s' (%ld bytes)",
                    length, offset, resolved_display, filesize);

            if (pass_no == 2 && length > 0) {
                unsigned char *buf = malloc((size_t)length);
                if (!buf) { fprintf(stderr, "Out of memory\n"); exit(1); }
                if (fseek(bf, offset, SEEK_SET) != 0 ||
                    fread(buf, 1, (size_t)length, bf) != (size_t)length) {
                    free(buf);
                    fclose(bf);
                    asm_error(L->line_no, L->raw, "Error reading '%s'", resolved_display);
                }
                bb_push_n(output, buf, (size_t)length);
                free(buf);
            }
            fclose(bf);
            pc += length;
            continue;
        }

        /* Real instruction */
        {
            long val = 0; int undef = 0;
            Mode mode = parse_operand(L->op, L->operand, pc, L->line_no, L->raw, &val, &undef);
            int size = MODE_SIZE[mode];

            if (pass_no == 2) {
                OpcodeEntry *e = find_mnemonic(L->op);
                int mode_ok = (e != NULL) && (e->op[mode] != -1);
                int illegal_slot = mode_ok && e->illegal[mode];
                int illegal_blocked = illegal_slot && !illegal_enabled;
                int opcode = (mode_ok && !illegal_blocked) ? e->op[mode] : 0x00;   /* BRK's
                    opcode -- an arbitrary but harmless placeholder; never
                    actually written to the .prg, since a mode_ok failure
                    (or an illegal opcode used without '.cpu 6510x') here
                    always means at least one error was recorded, and
                    main() never writes output once any error exists */
                if (!mode_ok)
                    asm_error_recoverable(L->line_no, L->raw, "Invalid addressing mode for %s", L->op);
                else if (illegal_blocked)
                    asm_error_recoverable(L->line_no, L->raw,
                        "Illegal/undocumented opcode '%s' used without '.cpu 6510x' "
                        "-- see c64asm-reference.md's \"Illegal opcodes\" section", L->op);
                if (undef)
                    asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in operand '%s'", L->operand);

                unsigned char bytes[3]; int nb = 0;
                if (mode == M_REL) {
                    long offset = val - (entry_pc + 2);
                    if (offset < -128 || offset > 127)
                        asm_error_recoverable(L->line_no, L->raw,
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

    if (cond_stack_top > 0)
        asm_error(cond_stack[cond_stack_top-1].opened_line_no,
                  cond_stack[cond_stack_top-1].opened_raw,
                  "unclosed '.if'/'.ifdef'/'.ifndef' at end of file (missing '.endif')");

    if (origin < 0) origin = 0x0801;
    *origin_out = origin;
    return pc;
}

/* --------------------------------------------------------------------- */
/* File loading                                                           */
/* --------------------------------------------------------------------- */

static void load_source(const char *path) {
    g_lines = malloc(sizeof(SourceLine) * MAX_LINES);
    if (!g_lines) { fprintf(stderr, "Out of memory\n"); exit(1); }

    process_include_file(path, NULL, 0, NULL);
}

/* --------------------------------------------------------------------- */
/* main                                                                    */
/* --------------------------------------------------------------------- */

static void usage(const char *prog) {
    fprintf(stderr,
        "c64asm - a two-pass 6502/6510 assembler for the Commodore 64\n\n"
        "Usage: %s <input.asm> -o <output.prg> [--listing <file.lst>]\n"
        "       [--vice-labels <file>] [--lib-dir <dir>]\n\n"
        "  --lib-dir <dir>  Fallback directory to also search when resolving\n"
        "                   .include paths that aren't found relative to the\n"
        "                   including file (the default, unaffected if this\n"
        "                   isn't given). Lets a common library directory be\n"
        "                   shared across separate project directories instead\n"
        "                   of each needing its own copy.\n"
        "  --vice-labels <file>  Write a VICE monitor label file (add_label\n"
        "                   commands for every symbol) -- load it with 'll\n"
        "                   \"<file>\"' in the VICE monitor to debug by name.\n"
        "  --warn-unused    After assembling, warn about every symbol (label\n"
        "                   or constant) defined but never referenced anywhere\n"
        "                   in the program. Off by default: a typical program\n"
        "                   that only uses part of an .include'd library will\n"
        "                   have plenty of expected, harmless unused\n"
        "                   library-internal symbols. Never fails the build.\n",
        prog);
}

/* Prints a warning for every symbol defined but never referenced
 * anywhere in the program -- see --warn-unused. Opt-in, not automatic:
 * a typical program that only uses part of an .include'd library will
 * have plenty of genuinely-fine unused library-internal symbols (a
 * constant defined for a routine the program never calls, say), which
 * would otherwise bury any warning actually worth looking at under a
 * pile of expected noise.
 *
 * "Used" means looked up by name from within an expression -- see
 * parse_atom's TK_IDENT case, the only place that sets a Symbol's
 * `used` flag. A symbol only referenced from inside a permanently-
 * false '.if' branch (dead code that pass 2 never actually evaluates)
 * counts as unused here, the same as a real compiler would treat code
 * excluded by an '#ifdef'. */
static void report_unused_symbols(void) {
    /* alphabetical, matching the --listing/--vice-labels symbol tables */
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
        if (s->used) continue;
        if (s->first_li >= 0 && s->first_li < g_line_count) {
            SourceLine *L = &g_lines[s->first_li];
            set_error_file(L->filename);
            asm_warning(L->line_no, L->raw, "Unused symbol '%s' (never referenced)", s->name);
        } else {
            /* Shouldn't normally happen (every symbol should have a
             * valid first_li from define_symbol()), but degrade to a
             * plain message with no location rather than crash the
             * whole report over it. */
            asm_warning(0, NULL, "Unused symbol '%s' (never referenced)", s->name);
        }
    }
    free(order);
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *listing_path = NULL;
    const char *vice_labels_path = NULL;
    int warn_unused = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--listing") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            listing_path = argv[++i];
        } else if (strcmp(argv[i], "--vice-labels") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            vice_labels_path = argv[++i];
        } else if (strcmp(argv[i], "--warn-unused") == 0) {
            warn_unused = 1;
        } else if (strcmp(argv[i], "--lib-dir") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            g_lib_dir = argv[++i];
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
    reset_collected_errors();   /* must happen before load_source() --
        split_line() (called during loading, via process_include_file())
        can itself record recoverable errors (e.g. "Unknown mnemonic or
        directive"), and those need to survive into the pass-1/pass-2
        checks below rather than being wiped out by a reset that runs
        after loading already recorded them */
    load_source(input_path);

    ByteBuf dummy; bb_init(&dummy);
    long origin1;
    run_pass(1, &dummy, &origin1);   /* build symbol table */
    free(dummy.data);

    if (any_errors_recorded()) {
        /* Pass 1 already found at least one real problem -- don't even
         * attempt pass 2. Pass 2 depends on pass 1 having produced a
         * complete, trustworthy symbol table and a consistent set of
         * addresses; running it anyway on top of a known-broken pass 1
         * would likely just flood the output with secondary "undefined
         * symbol" noise stemming from the original mistakes, not
         * independent problems worth separately reporting. */
        print_all_collected_errors_and_exit();
    }

    ByteBuf output; bb_init(&output);
    long origin2;
    run_pass(2, &output, &origin2);  /* generate code */

    if (any_errors_recorded()) {
        /* Pass 2 found problems pass 1 couldn't see (an addressing mode
         * an opcode doesn't support, a branch out of range, and so on).
         * No .prg or listing gets written -- there is nothing correct
         * to write once any error was recorded. */
        print_all_collected_errors_and_exit();
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) { fprintf(stderr, "Cannot open output file '%s'\n", output_path); return 1; }
    unsigned char header[2] = { (unsigned char)(origin2 & 0xFF), (unsigned char)((origin2 >> 8) & 0xFF) };
    fwrite(header, 1, 2, out);
    if (output.len) fwrite(output.data, 1, output.len, out);
    fclose(out);

    printf("Assembled %zu bytes, origin=$%04lX -> %s\n", output.len, origin2, output_path);

    if (warn_unused) report_unused_symbols();

    /* Shared alphabetical symbol order for --listing's "Symbol table:"
     * section and/or --vice-labels below -- simple insertion-sort-free
     * selection sort (symbol counts are small for typical programs). */
    int *order = NULL;
    if (listing_path || vice_labels_path) {
        order = malloc(sizeof(int) * symtab_count);
        for (int i = 0; i < symtab_count; i++) order[i] = i;
        for (int i = 0; i < symtab_count; i++) {
            int min = i;
            for (int j = i + 1; j < symtab_count; j++)
                if (strcmp(symtab[order[j]].name, symtab[order[min]].name) < 0) min = j;
            int t = order[i]; order[i] = order[min]; order[min] = t;
        }
    }

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
        for (int i = 0; i < symtab_count; i++) {
            Symbol *s = &symtab[order[i]];
            fprintf(lf, "  %-20s = $%04lX\n", s->name, s->value);
        }
        fclose(lf);
        printf("Listing written to %s\n", listing_path);
    }

    if (vice_labels_path) {
        FILE *vf = fopen(vice_labels_path, "w");
        if (!vf) { fprintf(stderr, "Cannot open VICE label file '%s'\n", vice_labels_path); return 1; }
        fprintf(vf, "; c64asm VICE label export  (origin $%04lX, %zu bytes)\n", origin2, output.len);
        fprintf(vf, "; load in the VICE monitor with: ll \"%s\"\n", vice_labels_path);
        for (int i = 0; i < symtab_count; i++) {
            Symbol *s = &symtab[order[i]];
            fprintf(vf, "add_label $%04lX .%s\n", s->value, s->name);
        }
        fclose(vf);
        printf("VICE labels written to %s\n", vice_labels_path);
    }

    free(order);

    free(output.data);
    free(g_lines);
    free(g_listing);
    return 0;
}
