/*
 * listing.c - see listing.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "listing.h"
#include "symtab.h"

ListEntry *g_listing;
int g_listing_count = 0;
static int g_listing_cap = 0;

void listing_add(long addr, const char *raw, const unsigned char *bytes, int nbytes) {
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

void write_listing_file(const char *path, long origin, size_t output_len) {
    FILE *lf = fopen(path, "w");
    if (!lf) { fprintf(stderr, "Cannot open listing file '%s'\n", path); exit(1); }

    fprintf(lf, "; c64asm listing  (origin $%04lX, %zu bytes)\n\n", origin, output_len);
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
        while (rl > 0 && (rawtrim[rl-1]=='\n' || rawtrim[rl-1]=='\r')) rawtrim[--rl]='\0';
        fprintf(lf, "%04lX  %-9s %s\n", le->addr, hexb, rawtrim);
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
