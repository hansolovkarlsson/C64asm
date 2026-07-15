/*
 * assembler.c - see assembler.h for the two-pass architecture overview.
 */

#include <string.h>
#include <strings.h>
#include "assembler.h"
#include "common.h"
#include "error.h"
#include "strutils.h"
#include "symtab.h"
#include "expr.h"
#include "opcodes.h"
#include "operand.h"
#include "lineparser.h"
#include "basicstub.h"
#include "listing.h"

/*
 * Splits a directive's comma-separated argument list (e.g. the operand
 * of ".byte 1, 2, \"hi\", 3") into individual trimmed argument strings,
 * respecting quotes and parentheses -- a comma inside a quoted string
 * or inside parentheses doesn't count as a separator. Not exposed via
 * assembler.h; only run_pass() below needs it, for the .byte/.word/
 * .text/.fill directives.
 */
static int split_args(const char *operand, char args[][MAX_LINE_LEN], int max_args) {
    int count = 0;
    int depth = 0, in_str = 0;
    char cur[MAX_LINE_LEN]; size_t cn = 0;
    cur[0] = '\0';
    for (const char *p = operand; *p; p++) {
        char ch = *p;
        if (ch == '"') { in_str = !in_str; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == '(' && !in_str) { depth++; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == ')' && !in_str) { depth--; if (cn+1<sizeof(cur)) cur[cn++]=ch; }
        else if (ch == ',' && depth == 0 && !in_str) {
            cur[cn] = '\0';
            trim(cur);
            if (count < max_args) strncpy(args[count++], cur, MAX_LINE_LEN - 1);
            cn = 0; cur[0] = '\0';
        } else {
            if (cn+1<sizeof(cur)) cur[cn++]=ch;
        }
    }
    cur[cn] = '\0';
    trim(cur);
    if (cur[0] != '\0' && count < max_args) strncpy(args[count++], cur, MAX_LINE_LEN - 1);
    return count;
}

long run_pass(int pass_no, ByteBuf *output, long *origin_out) {
    long pc = 0x0801;     /* the conventional C64 BASIC-program start address */
    long origin = -1;     /* -1 means "not set yet" */

    for (int li = 0; li < g_line_count; li++) {
        SourceLine *L = &g_lines[li];
        long entry_pc = pc;   /* this line's own address, before anything
                                  on it changes pc -- needed for relative
                                  branch offsets and listing entries */

        if (!L->has_op) {
            /* A blank line, or a bare label with nothing else on it:
             * still needs to define the label at the current address,
             * but there's nothing to assemble. */
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);
            continue;
        }

        if (strcmp(L->op, ".basic") == 0) {
            /* Emit the "10 SYS <addr>" loader stub (see basicstub.h)
             * and continue assembling normal code immediately after it
             * -- basic_stub_fixed_point() works out exactly where
             * "immediately after it" is. */
            unsigned char stub[64];
            long code_start;
            int slen = basic_stub_fixed_point(stub, &code_start);
            if (pass_no == 2) bb_push_n(output, stub, slen);
            origin = 0x0801;
            pc = code_start;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);
            continue;
        }

        if (strcmp(L->op, ".org") == 0) {
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            if (undef && pass_no == 2)
                asm_error(L->line_no, L->raw, "Undefined symbol in .org expression");
            if (origin < 0) {
                /* The very first .org in the file just sets the
                 * program's load address -- there's no "gap" to pad
                 * yet, since nothing has been assembled before it. */
                origin = val;
            } else if (pass_no == 2) {
                /* A *later* .org moves the program counter forward
                 * (typically to place data at a specific aligned
                 * address). Because the output is a flat .prg file --
                 * just a load address followed by contiguous bytes,
                 * with no way to represent "and then skip ahead" --
                 * the gap between where output currently ends and
                 * where .org wants to jump to has to be physically
                 * filled with zero bytes, or everything assembled
                 * after this point would silently end up at the wrong
                 * file offset (and therefore the wrong real address
                 * once loaded). This padding step was missing in an
                 * earlier version of this assembler; without it, a
                 * forward .org produced a shorter-than-expected file
                 * that quietly corrupted the addresses of everything
                 * after the jump. Moving *backward* is rejected
                 * outright, since overwriting already-emitted bytes
                 * isn't something this simple, append-only output
                 * buffer can do at all. */
                long current_abs = origin + (long)output->len;
                long gap = val - current_abs;
                if (gap < 0)
                    asm_error(L->line_no, L->raw,
                        ".org cannot move the program counter backward (from $%04lX to $%04lX) "
                        "-- the assembler can't overwrite bytes already assembled",
                        current_abs, val);
                for (long i = 0; i < gap; i++) bb_push(output, 0x00);
            }
            pc = val;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);
            continue;
        }

        if (strcmp(L->op, "=") == 0) {
            /* A constant assignment ("label = expr" or ".equ"). Unlike
             * a normal label, this binds the name to the expression's
             * *value*, not to the current program counter -- and
             * allows redefinition (allow_redefine=1), since re-running
             * the same "=" line is a completely ordinary situation
             * (e.g. it's evaluated fresh on both passes). */
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            define_symbol(L->label, val, L->line_no, pass_no, 1, L->raw);
            continue;
        }

        /* Every other kind of line defines its label (if it has one)
         * at the *current* address, then falls through to handle
         * whatever instruction or directive follows. */
        if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw);

        if (strcasecmp(L->op, ".byte") == 0 || strcasecmp(L->op, ".db") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            for (int i = 0; i < nargs; i++) {
                char *a = args[i];
                if (a[0] == '"') {
                    /* A quoted string argument: PETSCII-encode it and
                     * emit one byte per character. */
                    char s[MAX_LINE_LEN]; size_t al = strlen(a);
                    size_t sl = (al >= 2 && a[al-1]=='"') ? al-2 : al-1;
                    memcpy(s, a+1, sl); s[sl]='\0';
                    unsigned char buf[MAX_LINE_LEN]; int blen=0;
                    ascii_to_petscii(s, buf, &blen);
                    if (pass_no == 2) bb_push_n(output, buf, blen);
                    pc += blen;
                } else {
                    /* A numeric argument: evaluate and emit one byte,
                     * truncated to its low 8 bits. */
                    int undef = 0;
                    long v = eval_expr(a, pc, L->line_no, &undef);
                    if (undef && pass_no == 2)
                        asm_error(L->line_no, L->raw, "Undefined symbol in .byte '%s'", a);
                    if (pass_no == 2) bb_push(output, (unsigned char)(v & 0xFF));
                    pc += 1;
                }
            }
            continue;
        }

        if (strcasecmp(L->op, ".word") == 0 || strcasecmp(L->op, ".dw") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            for (int i = 0; i < nargs; i++) {
                int undef = 0;
                long v = eval_expr(args[i], pc, L->line_no, &undef);
                if (undef && pass_no == 2)
                    asm_error(L->line_no, L->raw, "Undefined symbol in .word '%s'", args[i]);
                if (pass_no == 2) {
                    /* 16-bit values are stored little-endian on the
                     * 6502, low byte first -- same as every multi-byte
                     * address an instruction operand encodes. */
                    bb_push(output, (unsigned char)(v & 0xFF));
                    bb_push(output, (unsigned char)((v >> 8) & 0xFF));
                }
                pc += 2;
            }
            continue;
        }

        if (strcasecmp(L->op, ".text") == 0 || strcasecmp(L->op, ".asc") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            for (int i = 0; i < nargs; i++) {
                char *a = args[i];
                char s[MAX_LINE_LEN];
                size_t al = strlen(a);
                if (al >= 2 && a[0]=='"' && a[al-1]=='"') {
                    memcpy(s, a+1, al-2); s[al-2]='\0';
                } else {
                    strncpy(s, a, sizeof(s)-1); s[sizeof(s)-1]='\0';
                }
                unsigned char buf[MAX_LINE_LEN]; int blen=0;
                ascii_to_petscii(s, buf, &blen);
                if (pass_no == 2) bb_push_n(output, buf, blen);
                pc += blen;
            }
            continue;
        }

        if (strcasecmp(L->op, ".fill") == 0 || strcasecmp(L->op, ".ds") == 0 ||
            strcasecmp(L->op, ".res") == 0) {
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            int undef = 0;
            long count = eval_expr(args[0], pc, L->line_no, &undef);
            long fill_val = 0;
            if (nargs > 1) { int u2=0; fill_val = eval_expr(args[1], pc, L->line_no, &u2); }
            if (pass_no == 2) {
                for (long i = 0; i < count; i++) bb_push(output, (unsigned char)(fill_val & 0xFF));
            }
            pc += count;
            continue;
        }

        /* Anything left at this point is a real 6502 instruction, not
         * a directive. */
        {
            long val = 0; int undef = 0;
            Mode mode = parse_operand(L->op, L->operand, pc, L->line_no, L->raw, &val, &undef);
            int size = MODE_SIZE[mode];

            /* Both passes compute `size` the same way, from the same
             * operand text -- this has to produce the *same* result on
             * pass 1 (when a forward-referenced label's real value
             * isn't known yet) and pass 2 (when it is), or every
             * address calculated on pass 1 would be wrong. That's
             * exactly why parse_operand()'s zero-page-vs-absolute
             * choice only trusts an operand's value when it's already
             * fully resolved (not a forward reference) -- an unresolved
             * forward reference always assumes the longer absolute
             * encoding on *both* passes, even on pass 2 once the value
             * turns out to fit in zero page after all. In the (very
             * rare, for typical programs) case where that assumption
             * costs you a byte you didn't need, forcing the absolute
             * form explicitly and consistently is that price -- see
             * the note on this in c64asm-reference.md's "known
             * limitations" section. */
            if (pass_no == 2) {
                OpcodeEntry *e = find_mnemonic(L->op);
                if (!e || e->op[mode] == -1)
                    asm_error(L->line_no, L->raw, "Invalid addressing mode for %s", L->op);
                int opcode = e->op[mode];
                if (undef)
                    asm_error(L->line_no, L->raw, "Undefined symbol in operand '%s'", L->operand);

                unsigned char bytes[3]; int nb = 0;
                if (mode == M_REL) {
                    /* Relative addressing (branches): the operand's
                     * *absolute* target address gets converted here
                     * into a signed 8-bit offset from the address right
                     * after this two-byte instruction -- which is
                     * exactly how the 6502's program counter will
                     * compute the actual jump target at runtime. */
                    long offset = val - (entry_pc + 2);
                    if (offset < -128 || offset > 127)
                        asm_error(L->line_no, L->raw,
                                  "Branch target out of range (%+ld) for %s %s",
                                  offset, L->op, L->operand);
                    bytes[nb++] = (unsigned char)opcode;
                    bytes[nb++] = (unsigned char)(offset & 0xFF);
                } else if (size == 1) {
                    bytes[nb++] = (unsigned char)opcode;
                } else if (size == 2) {
                    bytes[nb++] = (unsigned char)opcode;
                    bytes[nb++] = (unsigned char)(val & 0xFF);
                } else {
                    bytes[nb++] = (unsigned char)opcode;
                    bytes[nb++] = (unsigned char)(val & 0xFF);
                    bytes[nb++] = (unsigned char)((val >> 8) & 0xFF);
                }
                bb_push_n(output, bytes, nb);
                listing_add(entry_pc, L->raw, bytes, nb);
            }
            pc += size;
        }
    }

    if (origin < 0) origin = 0x0801;   /* source never set one explicitly */
    *origin_out = origin;
    return pc;
}
