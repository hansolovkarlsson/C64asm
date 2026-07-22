#!/usr/bin/env python3
"""
c64disasm.py - a 6502/6510 disassembler for Commodore 64 .prg files.

Companion tool to c64asm.py, but a genuinely different kind of problem:
c64asm.py turns text into bytes, deterministically, with a two-pass
process that always knows exactly what it's looking at. A disassembler
runs the other way -- turning bytes back into text -- and hits a
problem an assembler never has to: a raw binary doesn't say which of
its bytes are meant to be *executed* as instructions and which are
*data* (sprite pixels, text strings, a lookup table) that just happens
to sit in the same address space. Guess wrong, and a data byte gets
decoded as a garbage instruction, which can throw off every byte after
it.

This tool handles that with flow-following disassembly, not a blind
byte-by-byte scan: starting from a known (or given) entry point, it
decodes one instruction, then recursively follows every place *that*
instruction could hand control to next -- both branches of a
conditional, a JMP's target, a JSR's target *and* the instruction right
after the JSR (since a JSR returns) -- decoding each newly-reached
address the same way, until there's nowhere left to follow. Only bytes
actually reached this way are shown as code; everything else in the
file is shown as raw .byte data, which is the honest, correct answer
for a data byte, if not always the most informative one -- a hand
disassembler with the original source (or comments, or foreknowledge
of the program's layout) will always do better than this can.

Output is written in c64asm.py's own accepted syntax -- feeding the
output of this tool back into c64asm.py should reproduce the original
.prg's bytes exactly, for every real, working program in this project.
That's this tool's own actual correctness test: not a synthetic one,
but every demo this project ships, disassembled and reassembled, byte
for byte.

Deliberately a single Python tool, not matched across three
implementations the way c64asm.py itself is -- unlike the assembler,
where three interchangeable, verified-identical builds are worth
having for different workflows, a disassembler's value is in being
available and correct, not in having a C port; this is the reference
(and only) implementation.

Known, deliberate limitations -- see the "Known limitations" section of
this file's own header comment further down for the full list:
computed/indirect jumps, self-modifying code, and jump tables (the
RTS-trick pattern this project's own libraries use) can't be followed,
since their real targets exist only as data or as runtime state, not
as a literal JMP/JSR/branch operand this tool can see. Illegal/
undocumented opcodes are not decoded -- encountering one during
flow-following is treated as a sign this path has wandered into data,
not code, and that path stops there.
"""

import argparse
import sys

import c64asm

# ---------------------------------------------------------------------
# Reverse opcode table: opcode byte -> (mnemonic, mode). Built directly
# from c64asm.OPCODES (the assembler's own encode table) rather than a
# second, hand-transcribed copy, so the two can never drift out of
# sync. Four opcodes (ASL/LSR/ROL/ROR with no operand) have both an
# 'acc' and an 'imp' encoding for the exact same byte -- 'acc' wins,
# since "asl a" is clearer output than bare "asl" with nothing after
# it. Illegal/undocumented opcodes (c64asm.ILLEGAL_SLOTS) are
# deliberately left out of this table entirely -- see this file's own
# header comment for why.
# ---------------------------------------------------------------------

DECODE = {}
for _mnem, _modes in c64asm.OPCODES.items():
    for _mode, _opcode in _modes.items():
        if (_mnem, _mode) in c64asm.ILLEGAL_SLOTS:
            continue
        if _opcode in DECODE and DECODE[_opcode][1] == 'acc':
            continue  # already have the preferred 'acc' encoding
        if _mode == 'imp' and _opcode in DECODE:
            continue  # don't let 'imp' overwrite an existing 'acc' entry
        DECODE[_opcode] = (_mnem, _mode)

# Instructions that never fall through to the next instruction --
# flow-following stops here rather than continuing linearly.
NO_FALLTHROUGH = {'JMP', 'RTS', 'RTI', 'BRK'}

# Instructions whose operand is a code address this tool should follow
# (as opposed to LDA/STA/etc., whose operand is just a memory address
# with no reason to assume it's code).
BRANCH_MNEMONICS = {'BCC', 'BCS', 'BEQ', 'BMI', 'BNE', 'BPL', 'BVC', 'BVS'}


def detect_basic_stub(mem, load_addr):
    """Recognizes the exact byte pattern c64asm.py's own '.basic'
    directive generates (see build_basic_stub() there): a next-line
    pointer, a line number, a $9E (SYS) token, ASCII decimal digits
    for the target address, an end-of-line marker, and an end-of-
    program marker. Returns (stub_length, sys_target) if the bytes at
    load_addr match this pattern, or (0, None) if they don't -- a
    program that doesn't start with a recognizable BASIC stub (raw
    machine code loaded some other way, say) just skips this and uses
    load_addr itself as the only entry point.

    The stub's own bytes are always emitted as literal .byte data (see
    disassemble()) rather than reconstructed as a '.basic' line --
    reproducing the exact bytes this way needs no assumption that this
    tool's own understanding of '.basic's stub-generation logic
    matches c64asm.py's, byte for byte; a plain .byte copy can't
    possibly get that wrong."""
    def byte_at(addr):
        return mem.get(addr)

    p = load_addr
    if byte_at(p) is None or byte_at(p + 1) is None:
        return 0, None
    p += 2  # next-line pointer, not otherwise checked
    if byte_at(p) is None or byte_at(p + 1) is None:
        return 0, None
    p += 2  # line number, not otherwise checked
    if byte_at(p) != 0x9E:
        return 0, None
    p += 1
    digit_start = p
    while byte_at(p) is not None and 0x30 <= byte_at(p) <= 0x39:
        p += 1
    if p == digit_start:
        return 0, None
    digits = ''.join(chr(byte_at(a)) for a in range(digit_start, p))
    if byte_at(p) != 0x00:
        return 0, None
    p += 1  # end of line
    if byte_at(p) != 0x00 or byte_at(p + 1) != 0x00:
        return 0, None
    p += 2  # end of BASIC program
    return (p - load_addr), int(digits)


def disassemble(mem, load_addr, end_addr, entry_points):
    """The flow-following pass itself -- see this file's own header
    comment for the algorithm. Returns (instructions, labels):
    instructions maps each decoded instruction's starting address to
    (mnemonic, mode, operand_value, length, raw_bytes); labels is the
    set of addresses that some branch/JMP/JSR actually targets, which
    is also exactly the set of addresses that get a generated label in
    the output.

    `covered` tracks every byte belonging to some already-decoded
    instruction, not just each instruction's own starting address --
    this is what stops one flow path from decoding an instruction that
    would overlap bytes a different path already claimed, which a
    check against instruction start addresses alone wouldn't catch."""
    instructions = {}
    labels = set()
    covered = set()
    worklist = list(entry_points)

    while worklist:
        cur = worklist.pop()
        if cur in covered or cur < load_addr or cur >= end_addr:
            continue
        opcode = mem.get(cur)
        if opcode is None or opcode not in DECODE:
            continue  # illegal/undocumented opcode, or unmapped byte -- not code
        mnem, mode = DECODE[opcode]
        length = c64asm.MODE_SIZE[mode]
        if cur + length > end_addr:
            continue  # would run off the end of the file -- not really code here
        if any((cur + i) in covered for i in range(length)):
            continue  # would overlap an instruction a different path already claimed

        raw = tuple(mem[cur + i] for i in range(length))
        val = None
        if mode in ('imm', 'zp', 'zpx', 'zpy', 'indx', 'indy'):
            val = raw[1]
        elif mode in ('abs', 'absx', 'absy', 'ind'):
            val = raw[1] | (raw[2] << 8)
        elif mode == 'rel':
            offset = raw[1] - 0x100 if raw[1] >= 0x80 else raw[1]
            val = (cur + 2 + offset) & 0xFFFF

        instructions[cur] = (mnem, mode, val, length, raw)
        for i in range(length):
            covered.add(cur + i)

        if mnem in BRANCH_MNEMONICS:
            if load_addr <= val < end_addr:
                labels.add(val)
            worklist.append(val)
            worklist.append(cur + length)          # falls through too
        elif mnem == 'JMP':
            if mode == 'abs':
                if load_addr <= val < end_addr:
                    labels.add(val)
                worklist.append(val)
            # indirect JMP: real target is runtime data, can't follow;
            # either way JMP itself never falls through
        elif mnem == 'JSR':
            if load_addr <= val < end_addr:
                labels.add(val)
            worklist.append(val)
            worklist.append(cur + length)          # JSR returns, so this does too
        elif mnem in NO_FALLTHROUGH:
            pass                                     # RTS/RTI/BRK: no fallthrough
        else:
            worklist.append(cur + length)

    return instructions, labels


def format_operand(mode, val):
    """Formats an operand the way c64asm.py's own syntax expects it --
    feeding this back into c64asm.py should always re-encode to the
    exact same bytes."""
    if mode == 'imp':
        return ''
    if mode == 'acc':
        return 'a'
    if mode == 'imm':
        return f'#${val:02X}'
    if mode == 'zp':
        return f'${val:02X}'
    if mode == 'zpx':
        return f'${val:02X},x'
    if mode == 'zpy':
        return f'${val:02X},y'
    if mode in ('abs', 'rel'):
        return f'${val:04X}'
    if mode == 'absx':
        return f'${val:04X},x'
    if mode == 'absy':
        return f'${val:04X},y'
    if mode == 'ind':
        return f'(${val:04X})'
    if mode == 'indx':
        return f'(${val:02X},x)'
    if mode == 'indy':
        return f'(${val:02X}),y'
    raise ValueError(f'unknown addressing mode {mode!r}')


def format_instruction(addr, mnem, mode, val, labels):
    """Same as format_operand(), except a branch target or a JMP(abs)/
    JSR target that's also in `labels` gets the generated label name
    instead of a raw address -- every other operand (an ordinary LDA/
    STA/etc. address, or an indirect JMP's own pointer address) always
    stays a plain hex literal; see disassemble()'s own docstring for
    why only control-flow targets get this treatment."""
    is_flow_target = (mode == 'rel') or (mnem in ('JMP', 'JSR') and mode == 'abs')
    if is_flow_target and val in labels:
        operand = f'L{val:04X}'
    else:
        operand = format_operand(mode, val)
    text = mnem.lower()
    if operand:
        text += ' ' + operand
    return text


def guess_text_run(byte_list):
    """Tries to read `byte_list` as PETSCII-encoded text (assuming
    '.charset upper', the default -- there's no way to know from the
    bytes alone whether a program used '.charset lower' for any given
    string, so this only ever guesses the default encoding). Returns
    the guessed string if every byte in it is a plausible printable
    character AND re-encoding that exact string with c64asm.py's own
    ascii_to_petscii() reproduces byte_list exactly, or None if either
    check fails -- this is what makes emitting a '.text' line safe: a
    wrong guess simply doesn't get used, and the byte in question
    falls back to plain '.byte' data instead, which is always
    correct, if less readable, no matter what it turns out to be."""
    chars = []
    for b in byte_list:
        if not (0x20 <= b <= 0x7E) or b in (0x22, 0x5C):  # '"' and '\' both excluded --
            return None                                     # '"' breaks .text syntax, and
                                                              # '\' has no escaping meaning in
                                                              # a .text string at all (see this
                                                              # function's own docstring), so
                                                              # there's no safe way to include
                                                              # a literal one in the output
        chars.append(chr(b))
    guess = ''.join(chars)
    if c64asm.ascii_to_petscii(guess, False) == bytes(byte_list):
        return guess
    return None  # verification failed -- don't use this guess for anything


MIN_TEXT_RUN = 4  # shorter runs aren't worth a dedicated .text line


def format_data_bytes(byte_list):
    """Formats a run of data bytes as one or more output lines,
    preferring '.text "..."' for stretches of MIN_TEXT_RUN or more
    bytes that guess_text_run() can verify round-trip exactly, and
    falling back to '.byte' (8 per line) for everything else."""
    lines = []
    i = 0
    n = len(byte_list)
    while i < n:
        best_len = 0
        best_guess = None
        max_try = min(n - i, 64)  # cap how far a single .text guess reaches
        for length in range(max_try, MIN_TEXT_RUN - 1, -1):
            guess = guess_text_run(byte_list[i:i + length])
            if guess is not None:
                best_len = length
                best_guess = guess
                break
        if best_guess is not None:
            lines.append(f'        .text "{best_guess}"')
            i += best_len
        else:
            chunk = byte_list[i:i + 8]
            lines.append('        .byte ' + ', '.join(f'${b:02X}' for b in chunk))
            i += len(chunk)
    return lines


def format_output(filename, mem, load_addr, end_addr, stub_len, sys_target,
                   instructions, labels):
    out = []
    out.append(f'; c64disasm.py output for {filename}')
    out.append(f'; load address ${load_addr:04X}, {end_addr - load_addr} bytes')
    if sys_target is not None:
        out.append(f'; BASIC stub detected -- SYS target ${sys_target:04X}')
    out.append('; Disassembled by following code flow from the entry point(s)')
    out.append('; below; anything not reached that way is shown as raw .byte')
    out.append('; data, which is the correct, honest answer for a data byte,')
    out.append('; if not always the most informative one. See c64disasm.py\'s')
    out.append('; own header comment for what this can and can\'t follow.')
    out.append('')
    out.append(f'* = ${load_addr:04X}')
    out.append('')

    pos = load_addr
    if stub_len:
        out.append('; BASIC stub')
        stub_bytes = [mem[load_addr + i] for i in range(stub_len)]
        for i in range(0, len(stub_bytes), 8):
            chunk = stub_bytes[i:i + 8]
            out.append('        .byte ' + ', '.join(f'${b:02X}' for b in chunk))
        pos = load_addr + stub_len

    data_run = []

    def flush_data_run(start_addr):
        if not data_run:
            return
        out.extend(format_data_bytes(data_run))
        data_run.clear()

    while pos < end_addr:
        if pos in labels:
            flush_data_run(pos)
            out.append(f'L{pos:04X}:')
        if pos in instructions:
            flush_data_run(pos)
            mnem, mode, val, length, raw = instructions[pos]
            out.append('        ' + format_instruction(pos, mnem, mode, val, labels))
            pos += length
        else:
            data_run.append(mem[pos])
            pos += 1
    flush_data_run(pos)

    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(
        description='6502/6510 disassembler for Commodore 64 .prg files. '
                     'Companion to c64asm.py -- see this file\'s own header '
                     'comment for the flow-following algorithm and its '
                     'known limitations.')
    parser.add_argument('input', help='Input .prg file')
    parser.add_argument('-o', '--output', required=True, help='Output .asm file')
    parser.add_argument('--entry', action='append', default=[],
                         help='Additional entry point (hex, e.g. --entry $1000) '
                              'to flow-follow from, alongside the load address '
                              'or an auto-detected BASIC stub\'s SYS target. '
                              'May be given more than once.')
    args = parser.parse_args()

    with open(args.input, 'rb') as f:
        data = f.read()
    if len(data) < 2:
        sys.exit(f"'{args.input}' is too short to be a .prg file")
    load_addr = data[0] | (data[1] << 8)
    end_addr = load_addr + len(data) - 2
    mem = {load_addr + i: b for i, b in enumerate(data[2:])}

    stub_len, sys_target = detect_basic_stub(mem, load_addr)

    entry_points = []
    if sys_target is not None:
        entry_points.append(sys_target)
    else:
        entry_points.append(load_addr)
    for e in args.entry:
        entry_points.append(int(e.lstrip('$'), 16))

    instructions, labels = disassemble(mem, load_addr, end_addr, entry_points)
    labels |= set(entry_points) & set(range(load_addr, end_addr))

    output = format_output(args.input, mem, load_addr, end_addr,
                            stub_len, sys_target, instructions, labels)
    with open(args.output, 'w') as f:
        f.write(output)

    n_code = len(instructions)
    n_data = (end_addr - load_addr) - sum(i[3] for i in instructions.values())
    print(f'Disassembled {args.input}: {n_code} instructions, '
          f'{n_data} data bytes -> {args.output}')


if __name__ == '__main__':
    main()


