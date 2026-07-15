/*
 * bytebuf.c - see bytebuf.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include "bytebuf.h"

void bb_init(ByteBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

void bb_push(ByteBuf *b, unsigned char v) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->data = realloc(b->data, b->cap);
        if (!b->data) { fprintf(stderr, "Out of memory\n"); exit(1); }
    }
    b->data[b->len++] = v;
}

void bb_push_n(ByteBuf *b, const unsigned char *v, size_t n) {
    for (size_t i = 0; i < n; i++) bb_push(b, v[i]);
}
