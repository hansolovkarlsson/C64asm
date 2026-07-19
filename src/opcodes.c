/*
 * opcodes.c - see opcodes.h.
 */

#include <string.h>
#include <strings.h>   /* strcasecmp - POSIX, available on macOS & Linux */
#include "opcodes.h"
#include "common.h"

const int MODE_SIZE[M_COUNT] = {
    /* IMP */ 1, /* ACC */ 1, /* IMM */ 2, /* ZP */ 2, /* ZPX */ 2,
    /* ZPY */ 2, /* REL */ 2, /* ABS */ 3, /* ABSX */ 3, /* ABSY */ 3,
    /* IND */ 3, /* INDX */ 2, /* INDY */ 2
};

static OpcodeEntry opcode_table[MAX_MNEMONICS];
static int opcode_count = 0;

OpcodeEntry *find_mnemonic(const char *name) {
    for (int i = 0; i < opcode_count; i++)
        if (strcasecmp(opcode_table[i].mnemonic, name) == 0)
            return &opcode_table[i];
    return NULL;
}

/* Internal helper for init_opcodes() below: finds (or, the first time a
 * given mnemonic is mentioned, creates) its table entry with every
 * addressing mode initially marked unsupported (-1). Not exposed via
 * opcodes.h -- nothing outside this file needs to build the table,
 * only look things up in it once it's built. */
static OpcodeEntry *get_or_add_mnemonic(const char *name) {
    OpcodeEntry *e = find_mnemonic(name);
    if (e) return e;
    e = &opcode_table[opcode_count++];
    strncpy(e->mnemonic, name, 4);
    e->mnemonic[4] = '\0';
    for (int m = 0; m < M_COUNT; m++) { e->op[m] = -1; e->illegal[m] = 0; }
    return e;
}

/* Internal helper: records that mnemonic `name`, in addressing mode m,
 * assembles to the single byte `opcode`. Called many times over by
 * init_opcodes() -- once per (mnemonic, mode) combination that actually
 * exists on the 6502. */
static void SETOP(const char *name, Mode m, int opcode) {
    OpcodeEntry *e = get_or_add_mnemonic(name);
    e->op[m] = opcode;
}

/* Like SETOP(), but also marks this (mnemonic, mode) slot as an
 * illegal/undocumented opcode -- see the "Illegal opcodes" comment
 * block near the end of init_opcodes() below, and c64asm-reference.md,
 * for the full explanation. */
static void SETOP_ILLEGAL(const char *name, Mode m, int opcode) {
    OpcodeEntry *e = get_or_add_mnemonic(name);
    e->op[m] = opcode;
    e->illegal[m] = 1;
}

int is_branch_mnemonic(const char *name) {
    static const char *branches[] = {
        "BCC","BCS","BEQ","BMI","BNE","BPL","BVC","BVS", NULL
    };
    for (int i = 0; branches[i]; i++)
        if (strcasecmp(branches[i], name) == 0) return 1;
    return 0;
}

/*
 * The opcode table itself. This is a direct, mechanical transcription
 * of the 6502's documented instruction set -- every line just says
 * "mnemonic X in addressing mode Y assembles to byte Z". There's no
 * cleverness here on purpose; opcode tables are exactly the kind of
 * thing where cleverness (e.g. trying to derive opcodes from some
 * pattern in the bit layout) makes the code harder to verify against a
 * reference than just writing out the 150-odd (mnemonic, mode, byte)
 * facts directly, the same way a printed reference card would.
 */
void init_opcodes(void) {
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
    /* in assembler.c's real-instruction handling.                       */
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

int is_directive(const char *tok) {
    static const char *dirs[] = {
        ".org", ".byte", ".db", ".word", ".dw", ".text", ".asc",
        ".fill", ".ds", ".res", ".basic", ".equ", ".align", ".cpu",
        ".if", ".elif", ".else", ".endif", ".ifdef", ".ifndef", NULL
    };
    for (int i = 0; dirs[i]; i++)
        if (strcasecmp(dirs[i], tok) == 0) return 1;
    return 0;
}
