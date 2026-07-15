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
    strncpy(e->mnemonic, name, 3);
    e->mnemonic[3] = '\0';
    for (int m = 0; m < M_COUNT; m++) e->op[m] = -1;
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
}

int is_directive(const char *tok) {
    static const char *dirs[] = {
        ".org", ".byte", ".db", ".word", ".dw", ".text", ".asc",
        ".fill", ".ds", ".res", ".basic", ".equ", NULL
    };
    for (int i = 0; dirs[i]; i++)
        if (strcasecmp(dirs[i], tok) == 0) return 1;
    return 0;
}
