/*
 * basicstub.c - see basicstub.h.
 */

#include <string.h>
#include <stdio.h>
#include "basicstub.h"

/* Builds one specific stub, for one specific guess at the SYS target
 * address, and returns its length. Not exposed via basicstub.h -- only
 * basic_stub_fixed_point() below needs to call this, repeatedly, while
 * converging on the real target address.
 *
 * The tokenized BASIC line format is:
 *   [pointer to next line, 2 bytes] [line number, 2 bytes]
 *   [SYS token: $9E] [target address as ASCII digits] [$00: end of line]
 * followed by a final $00 $00 (a null "next line" pointer, marking the
 * end of the whole BASIC program -- a program with more than one line
 * would instead point to where that next line starts).
 */
static int build_basic_stub(long sys_target, unsigned char *out) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%ld", sys_target);
    int dlen = (int)strlen(digits);
    int body_len = 1 + dlen + 1;         /* $9E + digits + $00 */
    int line_len = 2 + 2 + body_len;     /* next-line ptr + line# + body */
    long addr = 0x0801;
    long next_addr = addr + line_len;
    int n = 0;
    out[n++] = (unsigned char)(next_addr & 0xFF);
    out[n++] = (unsigned char)((next_addr >> 8) & 0xFF);
    out[n++] = 10 & 0xFF;          /* line number 10, low byte */
    out[n++] = (10 >> 8) & 0xFF;   /* line number 10, high byte */
    out[n++] = 0x9E;                /* the SYS token */
    for (int i = 0; i < dlen; i++) out[n++] = (unsigned char)digits[i];
    out[n++] = 0x00;   /* end of this BASIC line */
    out[n++] = 0x00;   /* end of the BASIC program: */
    out[n++] = 0x00;   /* a null pointer to "the next line" */
    return n;
}

int basic_stub_fixed_point(unsigned char *stub_out, long *code_start_out) {
    long target = 0x0801 + 13;   /* initial guess: typical stub length */
    unsigned char tmp[64];
    int len = 0;
    for (int iter = 0; iter < 4; iter++) {
        len = build_basic_stub(target, tmp);
        long new_target = 0x0801 + len;
        if (new_target == target) break;   /* fixed point reached */
        target = new_target;
    }
    memcpy(stub_out, tmp, len);
    *code_start_out = target;
    return len;
}
