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
 *     ./c64asm input.asm -o output.prg [--listing out.lst] [--lib-dir dir]
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
 *                .if / .elif / .else / .endif / .ifdef / .ifndef
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
#include "includes.h"
#include "error.h"
#include "symtab.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "c64asm - a two-pass 6502/6510 assembler for the Commodore 64\n\n"
        "Usage: %s <input.asm> -o <output.prg> [--listing <file.lst>]\n"
        "       [--vice-labels <file>] [--lib-dir <dir>] [--warn-unused]\n\n"
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
        "                   in the program, scoped to the main file (an\n"
        "                   .include'd file's own unused symbols are\n"
        "                   suppressed by default -- see --warn-unused-all).\n"
        "                   Off by default. Never fails the build.\n"
        "  --warn-unused-all  Like --warn-unused (and implies it), but without\n"
        "                   the main-file scoping -- also warns about unused\n"
        "                   symbols defined in .include'd files. Expect a lot\n"
        "                   of library-internal noise unless the program uses\n"
        "                   nearly everything a library it includes defines.\n",
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
 * Scoped to the main file by default, for the same reason: an
 * .include'd file's own unused symbols are overwhelmingly library
 * noise rather than anything about *this* program, so they're
 * suppressed (with a one-line count of how many, so nothing goes
 * missing silently) unless include_library is set -- see
 * --warn-unused-all. main_path is compared against each symbol's
 * defining line's filename (empty for a program that never uses
 * .include, in which case everything is trivially "local") to tell
 * the two apart.
 *
 * "Used" means looked up by name from within an expression -- see
 * expr.c's parse_atom, TK_IDENT case, the only place that sets a
 * Symbol's `used` flag. A symbol only referenced from inside a
 * permanently-false '.if' branch (dead code that pass 2 never actually
 * evaluates) counts as unused here, the same as a real compiler would
 * treat code excluded by an '#ifdef'. */
static void report_unused_symbols(const char *main_path, int include_library) {
    /* alphabetical, matching the --listing/--vice-labels symbol tables */
    int *order = malloc(sizeof(int) * symtab_count);
    for (int i = 0; i < symtab_count; i++) order[i] = i;
    for (int i = 0; i < symtab_count; i++) {
        int min = i;
        for (int j = i + 1; j < symtab_count; j++)
            if (strcmp(symtab[order[j]].name, symtab[order[min]].name) < 0) min = j;
        int t = order[i]; order[i] = order[min]; order[min] = t;
    }
    int suppressed_count = 0;
    for (int i = 0; i < symtab_count; i++) {
        Symbol *s = &symtab[order[i]];
        if (s->used) continue;
        SourceLine *L = (s->first_li >= 0 && s->first_li < g_line_count)
                         ? &g_lines[s->first_li] : NULL;
        int is_local = !L || L->filename[0] == '\0' || strcmp(L->filename, main_path) == 0;
        if (!is_local && !include_library) {
            suppressed_count++;
            continue;
        }
        if (L) {
            asm_error_set_file(L->filename);
            asm_warning(L->line_no, L->raw, "Unused symbol '%s' (never referenced)", s->name);
        } else {
            /* Shouldn't normally happen (every symbol should have a
             * valid first_li from define_symbol()), but degrade to a
             * plain message with no location rather than crash the
             * whole report over it. */
            asm_warning(0, NULL, "Unused symbol '%s' (never referenced)", s->name);
        }
    }
    if (suppressed_count && !include_library) {
        fprintf(stderr, "(%d more unused symbol%s in .include'd files not shown "
                "-- use --warn-unused-all to see them)\n",
                suppressed_count, suppressed_count == 1 ? "" : "s");
    }
    free(order);
}

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *listing_path = NULL;
    const char *vice_labels_path = NULL;
    const char *lib_dir = NULL;
    int warn_unused = 0;
    int warn_unused_all = 0;

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
        } else if (strcmp(argv[i], "--warn-unused-all") == 0) {
            warn_unused_all = 1;
        } else if (strcmp(argv[i], "--warn-unused") == 0) {
            warn_unused = 1;
        } else if (strcmp(argv[i], "--lib-dir") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            lib_dir = argv[++i];
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

    includes_set_lib_dir(lib_dir);  /* NULL is fine -- disables the fallback */
    init_opcodes();          /* build the opcode table (opcodes.c) */
    reset_collected_errors();   /* must happen before load_source() --
        split_line() (called during loading) can itself record
        recoverable errors (e.g. "Unknown mnemonic or directive"), and
        those need to survive into the pass-1/pass-2 checks below rather
        than being wiped out by a reset that runs after loading already
        recorded them */
    load_source(input_path); /* read the whole file into g_lines (fileio.c) */

    /* Pass 1: walk the source once just to build the symbol table --
     * every label's address becomes known, but no machine code is kept
     * (see assembler.h for why two passes are needed at all). */
    ByteBuf dummy; bb_init(&dummy);
    long origin1;
    run_pass(1, &dummy, &origin1);
    free(dummy.data);

    if (any_errors_recorded()) {
        /* Pass 1 (or loading, before it) already found at least one
         * real problem -- don't even attempt pass 2. Pass 2 depends on
         * pass 1 having produced a complete, trustworthy symbol table
         * and a consistent set of addresses; running it anyway on top
         * of a known-broken pass 1 would likely just flood the output
         * with secondary "undefined symbol" noise stemming from the
         * original mistakes, not independent problems worth separately
         * reporting. */
        print_all_collected_errors_and_exit();
    }

    /* Pass 2: walk it again, now that every symbol is resolved, and
     * actually produce the machine code and listing entries. */
    ByteBuf output; bb_init(&output);
    long origin2;
    run_pass(2, &output, &origin2);

    if (any_errors_recorded()) {
        /* Pass 2 found problems pass 1 couldn't see (an addressing mode
         * an opcode doesn't support, a branch out of range, and so on).
         * No .prg or listing gets written -- there is nothing correct
         * to write once any error was recorded. */
        print_all_collected_errors_and_exit();
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) { fprintf(stderr, "Cannot open output file '%s'\n", output_path); return 1; }
    /* A .prg file's only "header" is this: the two-byte load address,
     * little-endian, immediately followed by the raw assembled bytes. */
    unsigned char header[2] = { (unsigned char)(origin2 & 0xFF), (unsigned char)((origin2 >> 8) & 0xFF) };
    fwrite(header, 1, 2, out);
    if (output.len) fwrite(output.data, 1, output.len, out);
    fclose(out);

    printf("Assembled %zu bytes, origin=$%04lX -> %s\n", output.len, origin2, output_path);

    if (warn_unused || warn_unused_all)
        report_unused_symbols(input_path, warn_unused_all);

    if (listing_path) {
        write_listing_file(listing_path, origin2, output.len);
        printf("Listing written to %s\n", listing_path);
    }

    if (vice_labels_path) {
        write_vice_labels_file(vice_labels_path, origin2, output.len);
        printf("VICE labels written to %s\n", vice_labels_path);
    }

    free(output.data);
    free(g_lines);
    free(g_listing);
    return 0;
}
