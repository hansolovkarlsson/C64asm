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
 * split_args() (comma-separated argument list splitting, used below for
 * the .byte/.word/.text/.fill directives) now lives in strutils.h/.c --
 * it moved there once macro.c also needed it, for splitting a macro
 * invocation's own argument list the same way.
 */

/*
 * Conditional assembly: .if/.elif/.else/.endif and
 * .ifdef/.ifndef/.else/.endif. See assembler.h for the full design
 * rationale -- in particular, why this lives here (assembly-time)
 * rather than in macro.c/includes.c (preprocessing-time), and why
 * ".if" forbids forward references while ".ifdef" needs
 * find_symbol_defined_before() instead of a plain find_symbol() check.
 */
typedef struct {
    int is_ifdef_style;    /* true for .ifdef/.ifndef, which don't allow .elif */
    int currently_active;  /* is the branch we're IN right now (if/elif/else)
                               the active one? */
    int condition_met;     /* has ANY branch in this block already been
                               taken? (stops a later .elif/.else from also
                               activating) */
    int opened_line_no;    /* where this block's own .if/.ifdef/.ifndef line
                               was, for a helpful "unclosed at end of file"
                               error message */
    char opened_raw[MAX_LINE_LEN];
} CondFrame;

/* Returns whether the CondFrame at stack[idx] is active, treating an
 * index before the start of the stack (idx < 0) as "active" -- that
 * represents being outside of any .if block at all, i.e. the top
 * level, which is always active. Centralizing this avoids repeating
 * the same "is there even a frame there" check at every place that
 * needs to ask "is my enclosing context active", and keeps that check
 * from ever looking, to a reader or a static analyzer, like an
 * unguarded out-of-bounds index. */
static int cond_frame_active(CondFrame *stack, int idx) {
    return idx < 0 || stack[idx].currently_active;
}

long run_pass(int pass_no, ByteBuf *output, long *origin_out) {
    long pc = 0x0801;     /* the conventional C64 BASIC-program start address */
    long origin = -1;     /* -1 means "not set yet" */
    CondFrame cond_stack[MAX_COND_DEPTH + 1];
    int cond_stack_top = 0;

    for (int li = 0; li < g_line_count; li++) {
        SourceLine *L = &g_lines[li];
        asm_error_set_file(L->filename[0] ? L->filename : NULL);
        long entry_pc = pc;   /* this line's own address, before anything
                                  on it changes pc -- needed for relative
                                  branch offsets and listing entries */

        int parent_active = cond_frame_active(cond_stack, cond_stack_top - 1);

        if (strcasecmp(L->op, ".if") == 0 || strcasecmp(L->op, ".ifdef") == 0 ||
            strcasecmp(L->op, ".ifndef") == 0) {
            if (cond_stack_top >= MAX_COND_DEPTH)
                asm_error(L->line_no, L->raw, "conditional nesting too deep (max %d)", MAX_COND_DEPTH);
            CondFrame *f = &cond_stack[cond_stack_top++];
            f->is_ifdef_style = (strcasecmp(L->op, ".if") != 0);
            f->opened_line_no = L->line_no;
            strncpy(f->opened_raw, L->raw, sizeof(f->opened_raw) - 1);
            f->opened_raw[sizeof(f->opened_raw) - 1] = '\0';
            if (!parent_active) {
                /* An enclosing branch is already false, so this whole
                 * block is dead regardless of its own condition --
                 * don't even evaluate it (it may reference symbols
                 * that don't exist, which would otherwise be a
                 * spurious error for code that was never going to run
                 * anyway), and make sure none of ITS .elif/.else
                 * branches can activate either. */
                f->currently_active = 0;
                f->condition_met = 1;
            } else if (strcasecmp(L->op, ".if") == 0) {
                int undef = 0;
                long v = eval_expr(L->operand, pc, L->line_no, &undef);
                if (undef)
                    asm_error(L->line_no, L->raw,
                        "Undefined symbol in .if/.elif expression -- forward references "
                        "are not allowed in conditional-assembly expressions");
                f->currently_active = (v != 0);
                f->condition_met = f->currently_active;
            } else {
                char name[MAX_LINE_LEN];
                strncpy(name, L->operand, sizeof(name) - 1); name[sizeof(name)-1] = '\0';
                trim(name);
                int is_defined = (find_symbol_defined_before(name, li) != NULL);
                int cond = (strcasecmp(L->op, ".ifdef") == 0) ? is_defined : !is_defined;
                f->currently_active = cond;
                f->condition_met = cond;
            }
            continue;
        }

        if (strcasecmp(L->op, ".elif") == 0) {
            if (cond_stack_top == 0) {
                asm_error(L->line_no, L->raw, "'.elif' with no matching '.if'");
            } else {
                CondFrame *f = &cond_stack[cond_stack_top-1];
                if (f->is_ifdef_style)
                    asm_error(L->line_no, L->raw,
                        "'.elif' is not allowed after '.ifdef'/'.ifndef' (only after '.if')");
                int outer_ok = cond_frame_active(cond_stack, cond_stack_top - 2);
                if (!outer_ok || f->condition_met) {
                    f->currently_active = 0;
                } else {
                    int undef = 0;
                    long v = eval_expr(L->operand, pc, L->line_no, &undef);
                    if (undef)
                        asm_error(L->line_no, L->raw,
                            "Undefined symbol in .if/.elif expression -- forward references "
                            "are not allowed in conditional-assembly expressions");
                    f->currently_active = (v != 0);
                    if (f->currently_active) f->condition_met = 1;
                }
            }
            continue;
        }

        if (strcasecmp(L->op, ".else") == 0) {
            if (cond_stack_top == 0) {
                asm_error(L->line_no, L->raw, "'.else' with no matching '.if'");
            } else {
                CondFrame *f = &cond_stack[cond_stack_top-1];
                int outer_ok = cond_frame_active(cond_stack, cond_stack_top - 2);
                f->currently_active = outer_ok && !f->condition_met;
                f->condition_met = 1;
            }
            continue;
        }

        if (strcasecmp(L->op, ".endif") == 0) {
            if (cond_stack_top == 0)
                asm_error(L->line_no, L->raw, "'.endif' with no matching '.if'/'.ifdef'/'.ifndef'");
            cond_stack_top--;
            continue;
        }

        if (cond_stack_top > 0 && !cond_stack[cond_stack_top-1].currently_active)
            continue;

        if (!L->has_op) {
            /* A blank line, or a bare label with nothing else on it:
             * still needs to define the label at the current address,
             * but there's nothing to assemble. */
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
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
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
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
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
            continue;
        }

        if (strcasecmp(L->op, ".align") == 0) {
            /* Advances pc to the next multiple of `n`, padding the
             * skipped bytes with zero -- exactly .org's forward-gap
             * logic above, just with the target computed by rounding
             * up rather than given directly. pc can never move
             * *backward* here (target is always >= pc by construction),
             * so there's no equivalent of .org's "moving backward"
             * error to check for. */
            int undef = 0;
            long n = eval_expr(L->operand, pc, L->line_no, &undef);
            if (undef && pass_no == 2)
                asm_error(L->line_no, L->raw, "Undefined symbol in .align expression");
            long target;
            if (undef) {
                /* Forward-referenced alignment value, pass 1 only (pass
                 * 2 would already have raised above). n is just
                 * eval_expr's undefined-symbol placeholder (0) here,
                 * not a real value -- validating its sign or dividing
                 * by it would be meaningless (and, for 0 specifically,
                 * actual division-by-zero undefined behavior in C), so
                 * pc simply doesn't advance this pass. That never
                 * produces incorrect output: pass 2 always catches the
                 * undefined symbol and aborts before anything computed
                 * from a wrong pass-1 address could ship. */
                target = pc;
            } else {
                if (n <= 0)
                    asm_error(L->line_no, L->raw,
                        ".align requires a positive alignment value (got %ld)", n);
                target = ((pc + n - 1) / n) * n;
            }
            if (pass_no == 2) {
                long gap = target - pc;
                for (long i = 0; i < gap; i++) bb_push(output, 0x00);
            }
            pc = target;
            if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);
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
            define_symbol(L->label, val, L->line_no, pass_no, 1, L->raw, li);
            continue;
        }

        /* Every other kind of line defines its label (if it has one)
         * at the *current* address, then falls through to handle
         * whatever instruction or directive follows. */
        if (L->has_label) define_symbol(L->label, pc, L->line_no, pass_no, 0, L->raw, li);

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

    if (cond_stack_top > 0)
        asm_error(cond_stack[cond_stack_top-1].opened_line_no,
                  cond_stack[cond_stack_top-1].opened_raw,
                  "unclosed '.if'/'.ifdef'/'.ifndef' at end of file (missing '.endif')");

    if (origin < 0) origin = 0x0801;   /* source never set one explicitly */
    *origin_out = origin;
    return pc;
}
