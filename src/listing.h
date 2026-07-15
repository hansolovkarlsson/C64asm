/*
 * listing.h - records one entry per assembled instruction (address,
 * encoded bytes, original source line) during pass 2, and writes them
 * out, plus a final alphabetized symbol table, to the optional
 * --listing file.
 *
 * Only real 6502 instructions get a listing entry -- directives like
 * .byte or .fill that emit data don't show up individually, only the
 * bytes an actual mnemonic assembled to. See run_pass() in
 * assembler.c for where listing_add() gets called.
 */

#ifndef C64ASM_LISTING_H
#define C64ASM_LISTING_H

#include "common.h"

typedef struct {
    long addr;
    char raw[MAX_LINE_LEN];
    unsigned char bytes[3];   /* an instruction is at most 3 bytes */
    int nbytes;
} ListEntry;

/* The listing itself and its current size, exposed directly for the
 * same reason as symtab[]/symtab_count in symtab.h: there's exactly one
 * listing per assembler run, and write_listing_file() below is the only
 * other thing that needs to read it. */
extern ListEntry *g_listing;
extern int g_listing_count;

/* Records one more listing entry. Grows the backing array (by doubling,
 * same strategy as bytebuf.c) if it's full. */
void listing_add(long addr, const char *raw, const unsigned char *bytes, int nbytes);

/*
 * Writes the full listing file: one line per recorded instruction in
 * the format "ADDR  BYTES  source", followed by a "Symbol table:"
 * section listing every defined symbol and its value, sorted
 * alphabetically by name.
 *
 * path:       output file path.
 * origin:     the assembled program's load address, for the header line.
 * output_len: the assembled program's total size in bytes, for the
 *             header line.
 *
 * Exits the program via asm_error()-style fprintf+exit if the file
 * can't be opened for writing (this one case doesn't go through
 * asm_error() itself since it's not tied to a specific source line).
 */
void write_listing_file(const char *path, long origin, size_t output_len);

#endif
