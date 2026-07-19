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

/*
 * Writes a VICE monitor label file: one "add_label $ADDR .name" command
 * per defined symbol, sorted alphabetically by name -- the exact same
 * symbols --listing's own "Symbol table:" section shows, just
 * reformatted for VICE's "ll" (load_labels) monitor command instead of
 * plain text. Load it in the VICE monitor with:
 *
 *     ll "path/to/file"
 *
 * to debug by name -- "break .main_loop" instead of "break $0a60", and
 * disassembly shows label names instead of bare addresses.
 *
 * Every symbol gets exported, including plain numeric constants that
 * aren't really addresses at all (e.g. a compile-time bound like
 * "XMIN = 24") -- this assembler's symbol table doesn't distinguish
 * "this is a real code/data address" from "this just happens to be a
 * small number," so neither does this export. VICE doesn't mind either
 * way (a label is just an optional name-to-address annotation, not
 * something that changes behavior), and other assemblers' own VICE
 * label exports (cc65, for one) have the same characteristic.
 *
 * path:       output file path.
 * origin:     the assembled program's load address, for the header line.
 * output_len: the assembled program's total size in bytes, for the
 *             header line.
 *
 * Exits the program via fprintf+exit if the file can't be opened for
 * writing, the same as write_listing_file().
 */
void write_vice_labels_file(const char *path, long origin, size_t output_len);

#endif
