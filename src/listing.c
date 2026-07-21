/*
 * listing.c - see listing.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "listing.h"
#include "symtab.h"
#include "opcodes.h"

/* --------------------------------------------------------------------- */
/* Cycle counts for --listing -- documented (legal) NMOS 6502/6510       */
/* opcodes only.                                                          */
/* --------------------------------------------------------------------- */

/* -1 = no cycle info (unsupported mode, or an illegal/undocumented
 * opcode -- see cycle_str() below, which double-checks against
 * opcode_table's own illegal[] flag regardless of what's written
 * here). A plain positive value is a fixed cycle count. 200 marks the
 * well-known branch case (2 cycles not taken / 3 taken / 4 taken
 * crossing a page). 100+base marks a *read* instruction using an
 * indexed addressing mode that takes one extra cycle if the indexing
 * crosses a page boundary (encoded this way so a plain int array
 * suffices instead of a tagged union) -- shown as base/base+1. Note
 * this "+1 if it crosses a page" rule does NOT apply to writes
 * (STA/STX/STY) or read-modify-write instructions (ASL/LSR/ROL/ROR/
 * INC/DEC) using an indexed mode -- those always take the same fixed
 * cycle count regardless of whether a page is crossed, which is why
 * STA/DEC/etc below are given plain fixed values for ABSX/ABSY, not
 * the 100+base encoding, even though LDA/CMP/etc are.
 *
 * Column order matches Mode exactly: IMP, ACC, IMM, ZP, ZPX, ZPY,
 * REL, ABS, ABSX, ABSY, IND, INDX, INDY. Cross-checked entry-by-entry
 * against 6502.org's published timing table
 * (http://6502.org/tutorials/6502opcodes.html). */
typedef struct {
    char mnemonic[5];
    int cycles[M_COUNT];
} CycleEntry;

/*                    IMP ACC IMM  ZP ZPX ZPY REL ABS ABSX ABSY IND INDX INDY */
static const CycleEntry cycle_table[] = {
    {"ADC", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"AND", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"ASL", {   2,  2, -1,  5,  6, -1, -1,  6,   7,  -1, -1,  -1,  -1}},
    {"BCC", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BCS", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BEQ", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BIT", {  -1, -1, -1,  3, -1, -1, -1,  4,  -1,  -1, -1,  -1,  -1}},
    {"BMI", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BNE", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BPL", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BRK", {   7, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"BVC", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"BVS", { 200, -1, -1, -1, -1, -1, 200, -1,  -1,  -1, -1,  -1,  -1}},
    {"CLC", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"CLD", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"CLI", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"CLV", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"CMP", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"CPX", {  -1, -1,  2,  3, -1, -1, -1,  4,  -1,  -1, -1,  -1,  -1}},
    {"CPY", {  -1, -1,  2,  3, -1, -1, -1,  4,  -1,  -1, -1,  -1,  -1}},
    {"DEC", {  -1, -1, -1,  5,  6, -1, -1,  6,   7,  -1, -1,  -1,  -1}},
    {"DEX", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"DEY", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"EOR", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"INC", {  -1, -1, -1,  5,  6, -1, -1,  6,   7,  -1, -1,  -1,  -1}},
    {"INX", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"INY", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"JMP", {  -1, -1, -1, -1, -1, -1, -1,  3,  -1,  -1,  5,  -1,  -1}},
    {"JSR", {  -1, -1, -1, -1, -1, -1, -1,  6,  -1,  -1, -1,  -1,  -1}},
    {"LDA", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"LDX", {  -1, -1,  2,  3, -1,  4, -1,  4,  -1, 104, -1,  -1,  -1}},
    {"LDY", {  -1, -1,  2,  3,  4, -1, -1,  4, 104,  -1, -1,  -1,  -1}},
    {"LSR", {   2,  2, -1,  5,  6, -1, -1,  6,   7,  -1, -1,  -1,  -1}},
    {"NOP", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"ORA", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"PHA", {   3, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"PHP", {   3, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"PLA", {   4, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"PLP", {   4, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"ROL", {   2,  2, -1,  5,  6, -1, -1,  6,   7,  -1, -1,  -1,  -1}},
    {"ROR", {   2,  2, -1,  5,  6, -1, -1,  6,   7,  -1, -1,  -1,  -1}},
    {"RTI", {   6, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"RTS", {   6, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"SBC", {  -1, -1,  2,  3,  4, -1, -1,  4, 104, 104, -1,   6, 105}},
    {"SEC", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"SED", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"SEI", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"STA", {  -1, -1, -1,  3,  4, -1, -1,  4,   5,   5, -1,   6,   6}},
    {"STX", {  -1, -1, -1,  3, -1,  4, -1,  4,  -1,  -1, -1,  -1,  -1}},
    {"STY", {  -1, -1, -1,  3,  4, -1, -1,  4,  -1,  -1, -1,  -1,  -1}},
    {"TAX", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"TAY", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"TSX", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"TXA", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"TXS", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
    {"TYA", {   2, -1, -1, -1, -1, -1, -1, -1,  -1,  -1, -1,  -1,  -1}},
};
static const int cycle_table_count = sizeof(cycle_table) / sizeof(cycle_table[0]);

/* Formats cycle_table's entry for (mnemonic, mode) the way --listing
 * shows it: a plain number for a fixed count, "base/base+1" for a
 * page-crossable read, "2/3/4" for a branch, or "" if there's no
 * entry at all (an illegal/undocumented opcode). Also blanks the
 * result if opcode_table itself marks this (mnemonic, mode) slot as
 * illegal, regardless of what cycle_table says -- opcode_table's own
 * illegal[] flag is the authoritative source for that question, not
 * a second copy of it here. */
void cycle_str(const char *mnemonic, Mode mode, char *out, size_t outsz) {
    out[0] = '\0';
    OpcodeEntry *oe = find_mnemonic(mnemonic);
    if (oe && oe->illegal[mode]) return;
    for (int i = 0; i < cycle_table_count; i++) {
        if (strcasecmp(cycle_table[i].mnemonic, mnemonic) != 0) continue;
        int code = cycle_table[i].cycles[mode];
        if (code < 0) return;
        if (code == 200) { strncpy(out, "2/3/4", outsz - 1); return; }
        if (code >= 100) {
            int base = code - 100;
            snprintf(out, outsz, "%d/%d", base, base + 1);
            return;
        }
        snprintf(out, outsz, "%d", code);
        return;
    }
}

ListEntry *g_listing;
int g_listing_count = 0;
static int g_listing_cap = 0;

void listing_add(long addr, const char *raw, const unsigned char *bytes,
                  int nbytes, const char *cycles) {
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
    strncpy(le->cycles, cycles ? cycles : "", sizeof(le->cycles) - 1);
    le->cycles[sizeof(le->cycles) - 1] = '\0';
}

void write_listing_file(const char *path, long origin, size_t output_len) {
    FILE *lf = fopen(path, "w");
    if (!lf) { fprintf(stderr, "Cannot open listing file '%s'\n", path); exit(1); }

    fprintf(lf, "; c64asm listing  (origin $%04lX, %zu bytes)\n", origin, output_len);
    fprintf(lf, "; CYCLES column: a plain number is a fixed cycle count; "
            "\"b/b+1\" means b cycles, +1 more if this instruction's "
            "indexed addressing crosses a page boundary; \"2/3/4\" means "
            "a branch's usual 2 (not taken) / 3 (taken) / 4 (taken, "
            "crossing a page). Blank means an illegal/undocumented "
            "opcode (.cpu 6510x) -- see c64asm-reference.md \"Listing "
            "file format\".\n\n");
    fprintf(lf, "%-6s%-11s%-8sSOURCE\n", "ADDR", "BYTES", "CYCLES");
    for (int i = 0; i < g_listing_count; i++) {
        ListEntry *le = &g_listing[i];
        char hexb[16] = "";
        char tmp[4];
        hexb[0] = '\0';
        for (int b = 0; b < le->nbytes; b++) {
            snprintf(tmp, sizeof(tmp), "%02X ", le->bytes[b]);
            strncat(hexb, tmp, sizeof(hexb) - strlen(hexb) - 1);
        }
        /* trim the trailing space so column alignment below looks right */
        size_t hl = strlen(hexb);
        while (hl > 0 && hexb[hl-1] == ' ') hexb[--hl] = '\0';
        char rawtrim[MAX_LINE_LEN];
        strncpy(rawtrim, le->raw, sizeof(rawtrim) - 1); rawtrim[sizeof(rawtrim)-1]='\0';
        size_t rl = strlen(rawtrim);
        while (rl > 0 && isspace((unsigned char)rawtrim[rl-1])) rawtrim[--rl]='\0';
        fprintf(lf, "%04lX  %-11s%-8s%s\n", le->addr, hexb, le->cycles, rawtrim);
    }

    fprintf(lf, "\nSymbol table:\n");
    /* A naive O(n^2) selection sort, building a sorted array of indices
     * rather than reordering symtab[] itself (so the assembler's own
     * lookups elsewhere, which don't care about ordering, are
     * unaffected). Symbol counts in a typical hobbyist program are
     * small enough (tens to low hundreds) that the quadratic cost here
     * is not something you'd ever notice -- the same "simple and
     * correct beats clever" trade-off as find_symbol()'s linear scan
     * in symtab.c. */
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
}

void write_vice_labels_file(const char *path, long origin, size_t output_len) {
    FILE *vf = fopen(path, "w");
    if (!vf) { fprintf(stderr, "Cannot open VICE label file '%s'\n", path); exit(1); }

    fprintf(vf, "; c64asm VICE label export  (origin $%04lX, %zu bytes)\n", origin, output_len);
    fprintf(vf, "; load in the VICE monitor with: ll \"%s\"\n", path);

    /* Same sort as write_listing_file() above -- kept as its own copy
     * rather than factored out, since symbol counts here are small
     * enough that a second O(n^2) sort costs nothing worth avoiding,
     * and it keeps this function fully self-contained. */
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
        fprintf(vf, "add_label $%04lX .%s\n", s->value, s->name);
    }
    free(order);
    fclose(vf);
}
