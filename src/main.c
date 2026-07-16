/*
 * main.c - c64asm: a two-pass 6502/6510 assembler for the Commodore 64.
 *
 * This file is deliberately short: its only job is command-line
 * argument handling and calling the other modules in the right order.
 * If you're new to this codebase, this is the best place to start
 * reading -- it's a roadmap of the whole assembly process, and every
 * function it calls is one module you can go read in isolation:
 *
 *   opcodes.h     the 6502 instruction set
 *   fileio.h      reading the source file
 *   lineparser.h  splitting each line into label/op/operand
 *   expr.h        evaluating expressions like "SCREEN + 40"
 *   operand.h     working out addressing modes
 *   symtab.h      the symbol table (labels and constants)
 *   assembler.h   the two-pass loop that ties parsing + evaluation +
 *                 addressing-mode logic together into actual bytes
 *   bytebuf.h     the growable buffer assembled bytes go into
 *   basicstub.h   the ".basic" directive's "10 SYS xxxx" loader
 *   listing.h     the optional --listing output file
 *
 * Portable C99. Builds with clang on macOS ("cc *.c -o c64asm") or
 * gcc/clang on Linux, using only the standard C library (plus
 * strcasecmp from <strings.h>, which is POSIX and present on both
 * macOS and Linux).
 *
 * Produces a C64 .prg file: a two-byte little-endian load address
 * followed by the assembled machine code.
 *
 * Usage:
 *     cc -O2 -o c64asm *.c
 *     ./c64asm input.asm -o output.prg [--listing out.lst]
 *
 * See c64asm-reference.md for the full syntax this assembler accepts:
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
 *   Comments:    ; to end of line
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opcodes.h"
#include "fileio.h"
#include "bytebuf.h"
#include "assembler.h"
#include "lineparser.h"
#include "listing.h"

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

    init_opcodes();          /* build the opcode table (opcodes.c) */
    load_source(input_path); /* read the whole file into g_lines (fileio.c) */

    /* Pass 1: walk the source once just to build the symbol table --
     * every label's address becomes known, but no machine code is kept
     * (see assembler.h for why two passes are needed at all). */
    ByteBuf dummy; bb_init(&dummy);
    long origin1;
    run_pass(1, &dummy, &origin1);
    free(dummy.data);

    /* Pass 2: walk it again, now that every symbol is resolved, and
     * actually produce the machine code and listing entries. */
    ByteBuf output; bb_init(&output);
    long origin2;
    run_pass(2, &output, &origin2);

    FILE *out = fopen(output_path, "wb");
    if (!out) { fprintf(stderr, "Cannot open output file '%s'\n", output_path); return 1; }
    /* A .prg file's only "header" is this: the two-byte load address,
     * little-endian, immediately followed by the raw assembled bytes. */
    unsigned char header[2] = { (unsigned char)(origin2 & 0xFF), (unsigned char)((origin2 >> 8) & 0xFF) };
    fwrite(header, 1, 2, out);
    if (output.len) fwrite(output.data, 1, output.len, out);
    fclose(out);

    printf("Assembled %zu bytes, origin=$%04lX -> %s\n", output.len, origin2, output_path);

    if (listing_path) {
        write_listing_file(listing_path, origin2, output.len);
        printf("Listing written to %s\n", listing_path);
    }

    free(output.data);
    free(g_lines);
    free(g_listing);
    return 0;
}
