/*
 * assembler.c - see assembler.h for the two-pass architecture overview.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
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
#include "includes.h"

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
    int illegal_enabled = 0;   /* toggled by '.cpu 6510x'/'.cpu 6510' --
                                   see that directive's handling below */
    int charset_lower = 0;     /* toggled by '.charset lower'/'.charset
                                   upper' -- see that directive's handling
                                   below */
    int tag_active = 0;        /* '.tag' block currently open? -- see
                                   '.tag'/'.endtag' handling below */
    char tag_name[MAX_IDENT] = "";
    long tag_start_pc = 0;
    int tag_start_li = -1;     /* g_lines index of the '.tag' line itself,
                                   so '.endtag' can report an error against
                                   its exact line_no/raw without needing to
                                   copy those strings out in advance */

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
            if (L->operand[0] != '\0') {
                /* An explicit start label: emit `jmp <label>` right
                 * after the stub, so SYS always lands at the real
                 * entry point even if code-emitting .include lines (a
                 * library's own routines, say) sit between here and
                 * the label -- forgetting this by hand was a
                 * recurring, hard-to-spot bug (SYS silently landing
                 * inside the first included routine instead). */
                int undef = 0;
                long target = eval_expr(L->operand, pc, L->line_no, &undef);
                if (undef && pass_no == 2)
                    asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .basic start operand '%s'", L->operand);
                if (pass_no == 2) {
                    unsigned char jmp_bytes[3];
                    jmp_bytes[0] = 0x4C;
                    jmp_bytes[1] = (unsigned char)(target & 0xFF);
                    jmp_bytes[2] = (unsigned char)((target >> 8) & 0xFF);
                    bb_push_n(output, jmp_bytes, 3);
                    listing_add(pc, L->raw, jmp_bytes, 3);
                }
                pc += 3;
            }
            continue;
        }

        if (strcmp(L->op, ".cpu") == 0) {
            /* Switches illegal/undocumented-opcode support on or off
             * from this point in the file forward (not retroactively --
             * a mnemonic used above this line is checked against
             * whatever '.cpu' setting was active there, not this one).
             * See opcodes.c's SETOP_ILLEGAL() calls and
             * c64asm-reference.md's "Illegal opcodes" section for the
             * full explanation. */
            char mode_name[MAX_LINE_LEN];
            strncpy(mode_name, L->operand, sizeof(mode_name) - 1);
            mode_name[sizeof(mode_name) - 1] = '\0';
            trim(mode_name);
            char mode_upper[MAX_LINE_LEN];
            strncpy(mode_upper, mode_name, sizeof(mode_upper) - 1);
            mode_upper[sizeof(mode_upper) - 1] = '\0';
            for (char *pc2 = mode_upper; *pc2; pc2++) *pc2 = (char)toupper((unsigned char)*pc2);
            if (strcmp(mode_upper, "6510X") == 0) {
                illegal_enabled = 1;
            } else if (strcmp(mode_upper, "6510") == 0 || strcmp(mode_upper, "6502") == 0) {
                illegal_enabled = 0;
            } else {
                asm_error_recoverable(L->line_no, L->raw,
                    "Unknown .cpu mode '%s' -- expected '6510' (standard, the "
                    "default) or '6510x' (enables illegal/undocumented opcodes)",
                    mode_name);
                /* fallback: leave illegal_enabled exactly as it was -- an
                   unrecognized mode isn't a request to change anything,
                   just a mistake to report */
            }
            continue;
        }

        if (strcmp(L->op, ".error") == 0 || strcmp(L->op, ".warning") == 0) {
            /* A source-author-placed diagnostic -- typically paired
             * with .ifdef/.ifndef to check a precondition (a required
             * zero-page symbol defined, say) and fail with a clear,
             * specific message right at the point of the mistake,
             * instead of a confusing "Undefined symbol" buried inside
             * a macro expansion three files away:
             *
             *       .ifndef gfx_ptr
             *       .error "graphics.inc requires gfx_ptr (2-byte zero page)"
             *       .endif
             *
             * '.error' is recoverable, not fatal -- several
             * independent '.error's (e.g. two different missing
             * zero-page symbols in two different included files) can
             * all be collected and reported together in one run.
             * '.warning' never stops assembly or affects the exit
             * status at all; it's only gated to pass 2 here so its
             * message prints exactly once, not twice (once per pass).
             */
            char msg_text[MAX_LINE_LEN];
            strncpy(msg_text, L->operand, sizeof(msg_text) - 1);
            msg_text[sizeof(msg_text) - 1] = '\0';
            trim(msg_text);
            size_t mlen = strlen(msg_text);
            int is_warning = (strcmp(L->op, ".warning") == 0);
            if (mlen >= 2 && msg_text[0] == '"' && msg_text[mlen-1] == '"') {
                msg_text[mlen-1] = '\0';
                memmove(msg_text, msg_text + 1, mlen - 1);
            } else {
                asm_error_recoverable(L->line_no, L->raw,
                    "%s requires a quoted message string, e.g. %s \"message\"",
                    L->op, L->op);
                /* fallback: nothing else to do with a malformed
                   directive -- there's no message to act on */
                continue;
            }
            if (is_warning) {
                if (pass_no == 2) asm_warning(L->line_no, L->raw, "%s", msg_text);
            } else {
                asm_error_recoverable(L->line_no, L->raw, "%s", msg_text);
            }
            continue;
        }

        if (strcmp(L->op, ".assert") == 0) {
            /* Fails assembly (recoverably -- see '.error' just above)
             * if `condition` evaluates to 0, e.g. to catch a struct
             * changing shape out from under code that assumed a
             * specific size:
             *
             *       .assert Exits.size == 4, "compute_room_exits_offset assumes 4 fields"
             *
             * The message is optional; without one, the condition's
             * own source text stands in for it. Only checked on pass
             * 2 -- during pass 1, a symbol the condition depends on
             * may still be an unresolved forward reference, standing
             * in as 0, which would make an otherwise-true condition
             * look spuriously false. */
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            if (nargs < 1) {
                asm_error_recoverable(L->line_no, L->raw,
                    ".assert requires a condition, e.g. .assert Exits.size == 4");
                continue;
            }
            if (nargs > 2) {
                asm_error_recoverable(L->line_no, L->raw,
                    ".assert takes at most a condition and a quoted message");
                continue;
            }
            char msg_text[MAX_LINE_LEN];
            int has_msg = 0;
            if (nargs > 1) {
                strncpy(msg_text, args[1], sizeof(msg_text) - 1);
                msg_text[sizeof(msg_text) - 1] = '\0';
                trim(msg_text);
                size_t mlen = strlen(msg_text);
                if (mlen >= 2 && msg_text[0] == '"' && msg_text[mlen-1] == '"') {
                    msg_text[mlen-1] = '\0';
                    memmove(msg_text, msg_text + 1, mlen - 1);
                    has_msg = 1;
                } else {
                    asm_error_recoverable(L->line_no, L->raw,
                        ".assert's message must be a quoted string, e.g. "
                        ".assert Exits.size == 4, \"message\"");
                    continue;
                }
            }
            int undef = 0;
            long val = eval_expr(args[0], pc, L->line_no, &undef);
            if (undef && pass_no == 2) {
                asm_error_recoverable(L->line_no, L->raw,
                    "Undefined symbol in .assert condition '%s'", args[0]);
                continue;
            }
            if (pass_no == 2 && val == 0) {
                if (has_msg)
                    asm_error_recoverable(L->line_no, L->raw, "%s", msg_text);
                else
                    asm_error_recoverable(L->line_no, L->raw, "Assertion failed: %s", args[0]);
            }
            continue;
        }

        if (strcmp(L->op, ".charset") == 0) {
            /* Switches how .text/.asc/.byte string literals encode
             * letters, from this point in the file forward (not
             * retroactively -- same positional behavior as '.cpu'
             * above). See strutils.c's ascii_to_petscii() and
             * c64asm-reference.md's "Text and PETSCII" section for
             * the full explanation, including the important caveat
             * about mixing '.charset upper' and '.charset lower'
             * text in a program that switches its character set at
             * runtime. */
            char cs_mode[MAX_LINE_LEN];
            strncpy(cs_mode, L->operand, sizeof(cs_mode) - 1);
            cs_mode[sizeof(cs_mode) - 1] = '\0';
            trim(cs_mode);
            char cs_upper[MAX_LINE_LEN];
            strncpy(cs_upper, cs_mode, sizeof(cs_upper) - 1);
            cs_upper[sizeof(cs_upper) - 1] = '\0';
            for (char *pc2 = cs_upper; *pc2; pc2++) *pc2 = (char)toupper((unsigned char)*pc2);
            if (strcmp(cs_upper, "UPPER") == 0) {
                charset_lower = 0;
            } else if (strcmp(cs_upper, "LOWER") == 0) {
                charset_lower = 1;
            } else {
                asm_error_recoverable(L->line_no, L->raw,
                    "Unknown .charset mode '%s' -- expected 'upper' (the "
                    "default) or 'lower'", cs_mode);
                /* fallback: leave charset_lower exactly as it was --
                   same reasoning as '.cpu''s fallback above */
            }
            continue;
        }

        if (strcmp(L->op, ".org") == 0) {
            int undef = 0;
            long val = eval_expr(L->operand, pc, L->line_no, &undef);
            if (undef && pass_no == 2) {
                asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .org expression");
                /* fallback: leave pc wherever it already was -- an unknown
                   target isn't something to guess at, and this is at
                   least deterministic for whatever comes next */
            } else if (origin < 0) {
                /* The very first .org in the file just sets the
                 * program's load address -- there's no "gap" to pad
                 * yet, since nothing has been assembled before it. */
                origin = val;
                pc = val;
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
                 * once loaded). Moving *backward* is rejected
                 * outright, since overwriting already-emitted bytes
                 * isn't something this simple, append-only output
                 * buffer can do at all. */
                long current_abs = origin + (long)output->len;
                long gap = val - current_abs;
                if (gap < 0) {
                    asm_error_recoverable(L->line_no, L->raw,
                        ".org cannot move the program counter backward (from $%04lX to $%04lX) "
                        "-- the assembler can't overwrite bytes already assembled",
                        current_abs, val);
                    /* fallback: same as the undefined-symbol case above --
                       don't move pc to an invalid target */
                } else {
                    for (long i = 0; i < gap; i++) bb_push(output, 0x00);
                    pc = val;
                }
            } else {
                pc = val;
            }
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
                asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .align expression");
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
                if (n <= 0) {
                    asm_error_recoverable(L->line_no, L->raw,
                        ".align requires a positive alignment value (got %ld)", n);
                    target = pc;   /* fallback: don't move pc -- necessary,
                                       not just consistent: n<=0 below would
                                       divide by zero (n==0) or produce a
                                       nonsensical negative gap (n<0) */
                } else {
                    target = ((pc + n - 1) / n) * n;
                }
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

        if (strcmp(L->op, ".tag") == 0) {
            /* Binds this data block to a '.struct', so a mismatch
             * between the block's actual size and the struct's
             * declared size becomes a clear, immediate error instead
             * of silently wrong data:
             *
             *       room_data: .tag Room
             *               .word room0_desc
             *               .byte FOREST, $ff, COTTAGE, $ff
             *       .endtag
             *
             * Doesn't emit anything itself, and doesn't transform the
             * lines between '.tag' and '.endtag' in any way -- unlike
             * '.repeat'/'.struct', which reshape the source during an
             * earlier preprocessing pass, '.tag' just watches how far
             * pc moves while ordinary lines in between assemble
             * completely normally, then compares that against
             * Name.size at '.endtag'. Recoverable, not fatal, the
             * same as '.assert' -- and for the same reason: getting a
             * '.tag' wrong doesn't corrupt anything about the rest of
             * the file the way a malformed '.repeat'/'.struct'/
             * '.macro' would. This runs after the shared
             * label-definition line just above, on purpose:
             * "room_data: .tag Room" needs room_data itself defined
             * at the usual current-pc value, exactly like any other
             * label, before '.tag' does anything of its own. */
            if (tag_active) {
                asm_error_recoverable(L->line_no, L->raw,
                    "nested '.tag' blocks are not supported (already "
                    "tagging as '%s')", tag_name);
                continue;
            }
            char trimmed_name[MAX_LINE_LEN];
            strncpy(trimmed_name, L->operand, sizeof(trimmed_name) - 1);
            trimmed_name[sizeof(trimmed_name) - 1] = '\0';
            trim(trimmed_name);
            int valid_name = trimmed_name[0] && is_ident_start(trimmed_name[0]);
            for (const char *p = trimmed_name + 1; valid_name && *p; p++)
                if (!is_ident_char(*p)) valid_name = 0;
            if (!valid_name) {
                asm_error_recoverable(L->line_no, L->raw,
                    "'.tag' requires a struct name, e.g. '.tag Room'");
                continue;
            }
            strncpy(tag_name, trimmed_name, sizeof(tag_name) - 1);
            tag_name[sizeof(tag_name) - 1] = '\0';
            tag_start_pc = pc;
            tag_start_li = li;
            tag_active = 1;
            continue;
        }

        if (strcmp(L->op, ".endtag") == 0) {
            if (!tag_active) {
                asm_error_recoverable(L->line_no, L->raw,
                    "'.endtag' with no matching '.tag'");
                continue;
            }
            tag_active = 0;
            SourceLine *start_L = &g_lines[tag_start_li];
            char size_expr[MAX_IDENT + 8];
            snprintf(size_expr, sizeof(size_expr), "%s.size", tag_name);
            int size_undef = 0;
            long size_val = eval_expr(size_expr, pc, L->line_no, &size_undef);
            if (size_undef) {
                asm_error_recoverable(start_L->line_no, start_L->raw,
                    "'.tag %s' doesn't match any known .struct -- no "
                    "'%s.size' symbol", tag_name, tag_name);
                continue;
            }
            long actual_size = pc - tag_start_pc;
            if (actual_size != size_val) {
                asm_error_recoverable(L->line_no, L->raw,
                    "data tagged as '%s' is %ld byte%s but %s is %ld byte%s",
                    tag_name, actual_size, actual_size == 1 ? "" : "s",
                    tag_name, size_val, size_val == 1 ? "" : "s");
            }
            continue;
        }

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
                    ascii_to_petscii(s, buf, &blen, charset_lower);
                    if (pass_no == 2) bb_push_n(output, buf, blen);
                    pc += blen;
                } else {
                    /* A numeric argument: evaluate and emit one byte,
                     * truncated to its low 8 bits. */
                    int undef = 0;
                    long v = eval_expr(a, pc, L->line_no, &undef);
                    if (undef && pass_no == 2)
                        asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .byte '%s'", a);
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
                    asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .word '%s'", args[i]);
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
                if (a[0] == '"') {
                    char s[MAX_LINE_LEN]; size_t al = strlen(a);
                    size_t sl = (al >= 2 && a[al-1]=='"') ? al-2 : al-1;
                    memcpy(s, a+1, sl); s[sl]='\0';
                    unsigned char buf[MAX_LINE_LEN]; int blen=0;
                    ascii_to_petscii(s, buf, &blen, charset_lower);
                    if (pass_no == 2) bb_push_n(output, buf, blen);
                    pc += blen;
                } else {
                    int undef = 0;
                    long v = eval_expr(a, pc, L->line_no, &undef);
                    if (undef && pass_no == 2)
                        asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in .text '%s'", a);
                    if (pass_no == 2) bb_push(output, (unsigned char)(v & 0xFF));
                    pc += 1;
                }
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

        if (strcasecmp(L->op, ".incbin") == 0) {
            /* Unlike .byte/.text's undefined-symbol errors, every
             * error path below is fatal, not recoverable -- an
             * .incbin problem means the assembler doesn't know how
             * many bytes this line emits, which (unlike an ordinary
             * .byte's *value* being wrong) would throw off every
             * address computed after it, the same class of problem a
             * missing .include'd file is. */
            char args[MAX_ARGS][MAX_LINE_LEN];
            int nargs = split_args(L->operand, args, MAX_ARGS);
            if (nargs < 1) asm_error(L->line_no, L->raw,
                ".incbin requires a quoted path, e.g. .incbin \"sprite.bin\"");
            trim(args[0]);
            size_t flen = strlen(args[0]);
            if (flen < 2 || args[0][0] != '"' || args[0][flen-1] != '"')
                asm_error(L->line_no, L->raw,
                    ".incbin requires a quoted path, e.g. .incbin \"sprite.bin\"");
            if (nargs > 3) asm_error(L->line_no, L->raw,
                ".incbin takes at most a path, an offset, and a length");

            char path[MAX_LINE_LEN];
            size_t plen = flen - 2;
            memcpy(path, args[0] + 1, plen); path[plen] = '\0';

            long offset = 0, length = 0;
            int length_given = 0;
            if (nargs > 1) {
                int off_undef = 0;
                offset = eval_expr(args[1], pc, L->line_no, &off_undef);
                if (off_undef) asm_error(L->line_no, L->raw,
                    "Undefined symbol in .incbin offset expression");
            }
            if (nargs > 2) {
                int len_undef = 0;
                length_given = 1;
                length = eval_expr(args[2], pc, L->line_no, &len_undef);
                if (len_undef) asm_error(L->line_no, L->raw,
                    "Undefined symbol in .incbin length expression");
            }

            char resolved_display[MAX_FILENAME_LEN];
            char lib_dir_display[MAX_FILENAME_LEN];
            int tried_lib_dir;
            includes_resolve_asset_path(path, L->filename[0] ? L->filename : NULL,
                                         resolved_display, sizeof(resolved_display),
                                         lib_dir_display, sizeof(lib_dir_display), &tried_lib_dir);

            FILE *bf = fopen(resolved_display, "rb");
            if (!bf) {
                if (tried_lib_dir && strcmp(resolved_display, lib_dir_display) != 0)
                    asm_error(L->line_no, L->raw,
                        "Cannot open included binary file '%s' (also tried '%s' via --lib-dir)",
                        resolved_display, lib_dir_display);
                else
                    asm_error(L->line_no, L->raw,
                        "Cannot open included binary file '%s'", resolved_display);
            }
            if (fseek(bf, 0, SEEK_END) != 0)
                asm_error(L->line_no, L->raw, "Cannot determine size of '%s'", resolved_display);
            long filesize = ftell(bf);
            if (filesize < 0)
                asm_error(L->line_no, L->raw, "Cannot determine size of '%s'", resolved_display);

            if (offset < 0 || offset > filesize)
                asm_error(L->line_no, L->raw,
                    ".incbin offset %ld is out of range for '%s' (%ld bytes)",
                    offset, resolved_display, filesize);
            if (!length_given) length = filesize - offset;
            if (length < 0 || offset + length > filesize)
                asm_error(L->line_no, L->raw,
                    ".incbin length %ld (from offset %ld) exceeds the size of '%s' (%ld bytes)",
                    length, offset, resolved_display, filesize);

            if (pass_no == 2 && length > 0) {
                unsigned char *buf = malloc((size_t)length);
                if (!buf) { fprintf(stderr, "Out of memory\n"); exit(1); }
                if (fseek(bf, offset, SEEK_SET) != 0 ||
                    fread(buf, 1, (size_t)length, bf) != (size_t)length) {
                    free(buf);
                    fclose(bf);
                    asm_error(L->line_no, L->raw, "Error reading '%s'", resolved_display);
                }
                bb_push_n(output, buf, (size_t)length);
                free(buf);
            }
            fclose(bf);
            pc += length;
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
                int mode_ok = (e != NULL) && (e->op[mode] != -1);
                int illegal_slot = mode_ok && e->illegal[mode];
                int illegal_blocked = illegal_slot && !illegal_enabled;
                int opcode = (mode_ok && !illegal_blocked) ? e->op[mode] : 0x00;   /* BRK's
                    opcode -- an arbitrary but harmless placeholder; never
                    actually written to the .prg, since a mode_ok failure
                    (or an illegal opcode used without '.cpu 6510x') here
                    always means at least one error was recorded, and
                    main() never writes output once any error exists */
                if (!mode_ok)
                    asm_error_recoverable(L->line_no, L->raw, "Invalid addressing mode for %s", L->op);
                else if (illegal_blocked)
                    asm_error_recoverable(L->line_no, L->raw,
                        "Illegal/undocumented opcode '%s' used without '.cpu 6510x' "
                        "-- see c64asm-reference.md's \"Illegal opcodes\" section", L->op);
                if (undef)
                    asm_error_recoverable(L->line_no, L->raw, "Undefined symbol in operand '%s'", L->operand);

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
                        asm_error_recoverable(L->line_no, L->raw,
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
