/*
 * basicstub.h - builds the tiny tokenized BASIC program (just the line
 * "10 SYS <address>") that the .basic directive emits at $0801, so that
 * LOADing and RUNning a .prg file jumps straight into the assembled
 * machine code.
 */

#ifndef C64ASM_BASICSTUB_H
#define C64ASM_BASICSTUB_H

/*
 * Computes the BASIC stub and where the machine code following it will
 * actually start, writing the stub's bytes into stub_out (the caller
 * must provide at least 16 bytes) and the resulting start address into
 * *code_start_out.
 *
 * This sounds like it should be simple -- "SYS <the address right after
 * the stub>" -- but it has a genuine chicken-and-egg problem: the stub
 * contains the target address written out as decimal digits (BASIC
 * tokenizes keywords like SYS, but numbers are stored as literal text),
 * so the stub's own length depends on how many digits that address
 * has, while the address itself depends on the stub's length. This is
 * resolved by iterating to a fixed point: guess a target, build the
 * stub for that guess, see where the code would actually end up given
 * that stub's real length, and repeat with the new guess until a guess
 * and its result agree. In practice this always converges in at most
 * two iterations for any address that fits on a C64 (there's no
 * realistic case where the digit count itself keeps changing forever).
 *
 * Returns the stub's length in bytes.
 */
int basic_stub_fixed_point(unsigned char *stub_out, long *code_start_out);

#endif
