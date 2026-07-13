#!/usr/bin/env python3
"""
c64asm.py - A complete two-pass 6502/6510 assembler for the Commodore 64.

Reads 6502 assembly source and produces a C64 .prg file: a two-byte
little-endian load address followed by the assembled machine code,
exactly the format the C64 KERNAL LOAD routine (and every C64 emulator)
expects.

------------------------------------------------------------------------
SUPPORTED SYNTAX
------------------------------------------------------------------------

Labels:
    loop:               ; label on its own line
    loop   lda #$00      ; label followed by an instruction
    label = $d020        ; constant / symbol assignment
    label .equ $d020      ; same thing, alternate spelling

Numbers:
    $1234    hexadecimal
    %01011010 binary
    1234     decimal
    'A'      character literal (PETSCII-mapped)

Operators in expressions: + - * / ( )  and unary < (low byte), > (high byte)
The current program counter can be referenced with '*'.

Addressing modes (standard 6502 assembler syntax):
    LDA #$10        immediate
    LDA $10         zero page (auto-selected when value fits in a byte)
    LDA $1000       absolute
    LDA $10,X       zero page,X
    LDA $1000,X     absolute,X
    LDA $10,Y       zero page,Y
    LDA $1000,Y     absolute,Y
    LDA ($10,X)     indexed indirect
    LDA ($10),Y     indirect indexed
    JMP ($1000)     indirect (JMP only)
    ASL A / ASL     accumulator / implied
    RTS             implied
    BNE loop        relative (branches)

Directives:
    * = $0801            set the program counter (org)
    .org $0801           same
    .byte $01,$02,3       emit raw bytes (alias: .db)
    .word $1000,label     emit little-endian 16-bit words (alias: .dw)
    .text "HELLO"        emit ASCII->PETSCII-mapped text bytes (alias: .asc)
    .fill 10, $00        emit N bytes of the given value (alias: .ds, .res)
    .basic               emit a standard BASIC "10 SYS <org>" stub
                          (must appear before the first .org/code)

Comments: ';' to end of line.

------------------------------------------------------------------------
USAGE
------------------------------------------------------------------------
    python3 c64asm.py input.asm -o output.prg
    python3 c64asm.py input.asm -o output.prg --listing out.lst
"""

import sys
import re
import argparse

# ---------------------------------------------------------------------------
# Opcode table: mnemonic -> {addressing_mode: opcode_byte}
# ---------------------------------------------------------------------------

OPCODES = {
    'ADC': {'imm':0x69,'zp':0x65,'zpx':0x75,'abs':0x6D,'absx':0x7D,'absy':0x79,'indx':0x61,'indy':0x71},
    'AND': {'imm':0x29,'zp':0x25,'zpx':0x35,'abs':0x2D,'absx':0x3D,'absy':0x39,'indx':0x21,'indy':0x31},
    'ASL': {'acc':0x0A,'zp':0x06,'zpx':0x16,'abs':0x0E,'absx':0x1E,'imp':0x0A},
    'BCC': {'rel':0x90},
    'BCS': {'rel':0xB0},
    'BEQ': {'rel':0xF0},
    'BIT': {'zp':0x24,'abs':0x2C},
    'BMI': {'rel':0x30},
    'BNE': {'rel':0xD0},
    'BPL': {'rel':0x10},
    'BRK': {'imp':0x00},
    'BVC': {'rel':0x50},
    'BVS': {'rel':0x70},
    'CLC': {'imp':0x18},
    'CLD': {'imp':0xD8},
    'CLI': {'imp':0x58},
    'CLV': {'imp':0xB8},
    'CMP': {'imm':0xC9,'zp':0xC5,'zpx':0xD5,'abs':0xCD,'absx':0xDD,'absy':0xD9,'indx':0xC1,'indy':0xD1},
    'CPX': {'imm':0xE0,'zp':0xE4,'abs':0xEC},
    'CPY': {'imm':0xC0,'zp':0xC4,'abs':0xCC},
    'DEC': {'zp':0xC6,'zpx':0xD6,'abs':0xCE,'absx':0xDE},
    'DEX': {'imp':0xCA},
    'DEY': {'imp':0x88},
    'EOR': {'imm':0x49,'zp':0x45,'zpx':0x55,'abs':0x4D,'absx':0x5D,'absy':0x59,'indx':0x41,'indy':0x51},
    'INC': {'zp':0xE6,'zpx':0xF6,'abs':0xEE,'absx':0xFE},
    'INX': {'imp':0xE8},
    'INY': {'imp':0xC8},
    'JMP': {'abs':0x4C,'ind':0x6C},
    'JSR': {'abs':0x20},
    'LDA': {'imm':0xA9,'zp':0xA5,'zpx':0xB5,'abs':0xAD,'absx':0xBD,'absy':0xB9,'indx':0xA1,'indy':0xB1},
    'LDX': {'imm':0xA2,'zp':0xA6,'zpy':0xB6,'abs':0xAE,'absy':0xBE},
    'LDY': {'imm':0xA0,'zp':0xA4,'zpx':0xB4,'abs':0xAC,'absx':0xBC},
    'LSR': {'acc':0x4A,'zp':0x46,'zpx':0x56,'abs':0x4E,'absx':0x5E,'imp':0x4A},
    'NOP': {'imp':0xEA},
    'ORA': {'imm':0x09,'zp':0x05,'zpx':0x15,'abs':0x0D,'absx':0x1D,'absy':0x19,'indx':0x01,'indy':0x11},
    'PHA': {'imp':0x48},
    'PHP': {'imp':0x08},
    'PLA': {'imp':0x68},
    'PLP': {'imp':0x28},
    'ROL': {'acc':0x2A,'zp':0x26,'zpx':0x36,'abs':0x2E,'absx':0x3E,'imp':0x2A},
    'ROR': {'acc':0x6A,'zp':0x66,'zpx':0x76,'abs':0x6E,'absx':0x7E,'imp':0x6A},
    'RTI': {'imp':0x40},
    'RTS': {'imp':0x60},
    'SBC': {'imm':0xE9,'zp':0xE5,'zpx':0xF5,'abs':0xED,'absx':0xFD,'absy':0xF9,'indx':0xE1,'indy':0xF1},
    'SEC': {'imp':0x38},
    'SED': {'imp':0xF8},
    'SEI': {'imp':0x78},
    'STA': {'zp':0x85,'zpx':0x95,'abs':0x8D,'absx':0x9D,'absy':0x99,'indx':0x81,'indy':0x91},
    'STX': {'zp':0x86,'zpy':0x96,'abs':0x8E},
    'STY': {'zp':0x84,'zpx':0x94,'abs':0x8C},
    'TAX': {'imp':0xAA},
    'TAY': {'imp':0xA8},
    'TSX': {'imp':0xBA},
    'TXA': {'imp':0x8A},
    'TXS': {'imp':0x9A},
    'TYA': {'imp':0x98},
}

# Number of bytes an instruction occupies for each addressing mode.
MODE_SIZE = {'imp':1,'acc':1,'imm':2,'zp':2,'zpx':2,'zpy':2,'rel':2,
             'abs':3,'absx':3,'absy':3,'ind':3,'indx':2,'indy':2}

BRANCHES = {'BCC','BCS','BEQ','BMI','BNE','BPL','BVC','BVS'}

DIRECTIVES = {'.org', '*=', '.byte', '.db', '.word', '.dw', '.text', '.asc',
              '.fill', '.ds', '.res', '.basic', '.equ'}


class AsmError(Exception):
    def __init__(self, message, line_no=None, text=None):
        self.message = message
        self.line_no = line_no
        self.text = text
        super().__init__(str(self))

    def __str__(self):
        if self.line_no is not None:
            loc = f"line {self.line_no}"
            if self.text is not None:
                loc += f": {self.text.strip()}"
            return f"{self.message} ({loc})"
        return self.message


# ---------------------------------------------------------------------------
# ASCII -> PETSCII mapping for .text / .asc
# ---------------------------------------------------------------------------

def ascii_to_petscii(s):
    """Map an ASCII string to C64 PETSCII bytes suitable for use with
    standard KERNAL CHROUT ($FFD2) output on the default (uppercase)
    character set. Uppercase letters, digits, and punctuation are already
    correct PETSCII values (identical to ASCII in that range); only
    lowercase input needs folding up to display as uppercase."""
    out = bytearray()
    for ch in s:
        if 'a' <= ch <= 'z':
            out.append(ord(ch) - ord('a') + ord('A'))
        else:
            out.append(ord(ch) & 0xFF)
    return bytes(out)


# ---------------------------------------------------------------------------
# Tokenizing a single source line into (label, mnemonic_or_directive, operand)
# ---------------------------------------------------------------------------

LINE_RE = re.compile(r'^(?P<label>[A-Za-z_.][A-Za-z0-9_]*:?)?\s*'
                      r'(?P<rest>.*)$')


def strip_comment(line):
    """Remove a ';' comment, but not one that's inside a quoted string."""
    out = []
    in_str = False
    for ch in line:
        if ch == '"':
            in_str = not in_str
        if ch == ';' and not in_str:
            break
        out.append(ch)
    return ''.join(out)


def split_line(raw_line, line_no):
    """Return (label_or_None, mnemonic_or_directive_or_None, operand_str)."""
    line = strip_comment(raw_line).rstrip('\n')
    stripped = line.strip()
    if not stripped:
        return None, None, ''

    # '*=' / '*' + '=' org directive can start without whitespace
    if stripped.startswith('*') and re.match(r'^\*\s*=', stripped):
        rhs = stripped.split('=', 1)[1].strip()
        return None, '.org', rhs

    label = None
    rest = stripped

    # label = value  (constant assignment)   e.g.  SCREEN = $0400
    m = re.match(r'^([A-Za-z_.][A-Za-z0-9_]*)\s*=\s*(.+)$', stripped)
    if m:
        return m.group(1), '=', m.group(2).strip()

    # Leading label (with or without ':' , with or without following whitespace)
    m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*):', rest)
    if m:
        label = m.group(1)
        rest = rest[m.end():].strip()
    else:
        # A label with no colon must be followed by whitespace (or be the
        # whole line) and must NOT itself be a known mnemonic/directive.
        m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)(\s+(.*))?$', rest)
        if m:
            first = m.group(1)
            if first.upper() not in OPCODES and first.lower() not in DIRECTIVES:
                # Could still be "label" alone, or "label mnemonic operand"
                remainder = m.group(3) or ''
                if remainder == '' or remainder.split()[0].upper() in OPCODES \
                   or remainder.split()[0].lower() in DIRECTIVES \
                   or remainder.startswith('.'):
                    label = first
                    rest = remainder.strip()

    if rest == '':
        return label, None, ''

    parts = rest.split(None, 1)
    op = parts[0]
    operand = parts[1].strip() if len(parts) > 1 else ''

    op_upper = op.upper()
    op_lower = op.lower()
    if op_upper in OPCODES:
        return label, op_upper, operand
    if op_lower in DIRECTIVES:
        return label, op_lower, operand
    if op_lower == '.equ':
        return label, '=', operand

    raise AsmError(f"Unknown mnemonic or directive '{op}'", line_no, raw_line)


# ---------------------------------------------------------------------------
# Expression evaluation
# ---------------------------------------------------------------------------

class ExprParser:
    """A small recursive-descent parser/evaluator for assembler expressions.
    Supports + - * / ( ) unary - , unary < (low byte) and > (high byte),
    hex ($), binary (%), decimal, character literals, '*' (current PC),
    and symbol references."""

    TOKEN_RE = re.compile(r"""
        \s*(?:
            (?P<hex>\$[0-9A-Fa-f]+)
          | (?P<bin>%[01]+)
          | (?P<char>'(\\.|[^'])')
          | (?P<dec>[0-9]+)
          | (?P<ident>[A-Za-z_.][A-Za-z0-9_]*)
          | (?P<star>\*)
          | (?P<op>[()+\-*/<>])
        )
    """, re.VERBOSE)

    def __init__(self, text, symbols, pc, line_no):
        self.text = text
        self.symbols = symbols
        self.pc = pc
        self.line_no = line_no
        self.tokens = self._tokenize(text)
        self.pos = 0
        self.undefined = False

    def _tokenize(self, text):
        toks = []
        i = 0
        while i < len(text):
            m = self.TOKEN_RE.match(text, i)
            if not m or m.end() == i:
                if text[i].isspace():
                    i += 1
                    continue
                raise AsmError(f"Bad character '{text[i]}' in expression '{text}'",
                                self.line_no)
            i = m.end()
            kind = m.lastgroup
            val = m.group()
            toks.append((kind, val.strip()))
        return toks

    def peek(self):
        return self.tokens[self.pos] if self.pos < len(self.tokens) else (None, None)

    def next(self):
        tok = self.peek()
        self.pos += 1
        return tok

    def parse(self):
        if not self.tokens:
            raise AsmError("Empty expression", self.line_no, self.text)
        val = self.parse_expr()
        if self.pos != len(self.tokens):
            raise AsmError(f"Unexpected trailing text in expression '{self.text}'",
                            self.line_no)
        return val

    def parse_expr(self):
        val = self.parse_term()
        while True:
            kind, tok = self.peek()
            if kind == 'op' and tok in ('+', '-'):
                self.next()
                rhs = self.parse_term()
                val = val + rhs if tok == '+' else val - rhs
            else:
                break
        return val

    def parse_term(self):
        val = self.parse_unary()
        while True:
            kind, tok = self.peek()
            if kind == 'op' and tok in ('*', '/'):
                self.next()
                rhs = self.parse_unary()
                if tok == '*':
                    val = val * rhs
                else:
                    val = val // rhs if rhs != 0 else 0
            else:
                break
        return val

    def parse_unary(self):
        kind, tok = self.peek()
        if kind == 'op' and tok == '-':
            self.next()
            return -self.parse_unary()
        if kind == 'op' and tok == '<':
            self.next()
            return self.parse_unary() & 0xFF
        if kind == 'op' and tok == '>':
            self.next()
            return (self.parse_unary() >> 8) & 0xFF
        return self.parse_atom()

    def parse_atom(self):
        kind, tok = self.next()
        if kind == 'hex':
            return int(tok[1:], 16)
        if kind == 'bin':
            return int(tok[1:], 2)
        if kind == 'dec':
            return int(tok, 10)
        if kind == 'char':
            inner = tok[1:-1]
            if inner.startswith('\\'):
                inner = inner[1:]
            return ord(inner)
        if kind == 'star':
            return self.pc
        if kind == 'ident':
            name = tok
            if name in self.symbols:
                return self.symbols[name]
            self.undefined = True
            return 0  # placeholder during pass 1 / forward reference
        if kind == 'op' and tok == '(':
            val = self.parse_expr()
            k2, t2 = self.next()
            if not (k2 == 'op' and t2 == ')'):
                raise AsmError(f"Missing ')' in expression '{self.text}'", self.line_no)
            return val
        raise AsmError(f"Cannot parse expression '{self.text}'", self.line_no)


def eval_expr(text, symbols, pc, line_no):
    p = ExprParser(text, symbols, pc, line_no)
    val = p.parse()
    return val, p.undefined


def split_args(operand):
    """Split a comma-separated directive argument list, respecting quotes
    and parentheses."""
    args = []
    depth = 0
    in_str = False
    cur = ''
    for ch in operand:
        if ch == '"':
            in_str = not in_str
            cur += ch
        elif ch == '(' and not in_str:
            depth += 1
            cur += ch
        elif ch == ')' and not in_str:
            depth -= 1
            cur += ch
        elif ch == ',' and depth == 0 and not in_str:
            args.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip() != '':
        args.append(cur.strip())
    return args


# ---------------------------------------------------------------------------
# Addressing mode parsing for a single instruction operand
# ---------------------------------------------------------------------------

def parse_operand(mnemonic, operand, symbols, pc, line_no):
    """Return (mode, expr_text_or_None, undefined_flag, value_or_None)."""
    modes = OPCODES[mnemonic]
    op = operand.strip()

    if op == '':
        if 'imp' in modes:
            return 'imp', None, False, None
        raise AsmError(f"{mnemonic} requires an operand", line_no)

    if op.upper() == 'A' and ('acc' in modes):
        return 'acc', None, False, None

    if mnemonic in BRANCHES:
        val, undef = eval_expr(op, symbols, pc, line_no)
        return 'rel', op, undef, val

    # Immediate: #expr
    if op.startswith('#'):
        val, undef = eval_expr(op[1:].strip(), symbols, pc, line_no)
        return 'imm', op[1:].strip(), undef, val

    # Indexed indirect: (expr,X)
    m = re.match(r'^\(\s*(.+?)\s*,\s*[Xx]\s*\)$', op)
    if m:
        val, undef = eval_expr(m.group(1), symbols, pc, line_no)
        mode = 'indx' if 'indx' in modes else None
        if mode is None:
            raise AsmError(f"{mnemonic} does not support (zp,X) addressing", line_no)
        return mode, m.group(1), undef, val

    # Indirect indexed: (expr),Y
    m = re.match(r'^\(\s*(.+?)\s*\)\s*,\s*[Yy]\s*$', op)
    if m:
        val, undef = eval_expr(m.group(1), symbols, pc, line_no)
        mode = 'indy' if 'indy' in modes else None
        if mode is None:
            raise AsmError(f"{mnemonic} does not support (zp),Y addressing", line_no)
        return mode, m.group(1), undef, val

    # Indirect (JMP only): (expr)
    m = re.match(r'^\(\s*(.+?)\s*\)$', op)
    if m:
        val, undef = eval_expr(m.group(1), symbols, pc, line_no)
        mode = 'ind' if 'ind' in modes else None
        if mode is None:
            raise AsmError(f"{mnemonic} does not support indirect addressing", line_no)
        return mode, m.group(1), undef, val

    # expr,X  or  expr,Y
    m = re.match(r'^(.+?)\s*,\s*([XxYy])$', op)
    if m:
        expr_text, reg = m.group(1), m.group(2).upper()
        val, undef = eval_expr(expr_text, symbols, pc, line_no)
        force_abs = expr_text.strip().startswith('$') and len(expr_text.strip()) > 3 and val > 0xFF
        is_zp = (not undef) and val <= 0xFF and not _looks_forced_absolute(expr_text)
        if reg == 'X':
            if is_zp and 'zpx' in modes:
                return 'zpx', expr_text, undef, val
            if 'absx' in modes:
                return 'absx', expr_text, undef, val
            if 'zpx' in modes:
                return 'zpx', expr_text, undef, val
        else:
            if is_zp and 'zpy' in modes:
                return 'zpy', expr_text, undef, val
            if 'absy' in modes:
                return 'absy', expr_text, undef, val
            if 'zpy' in modes:
                return 'zpy', expr_text, undef, val
        raise AsmError(f"{mnemonic} does not support that addressing mode", line_no)

    # Plain expr -> zero page or absolute
    val, undef = eval_expr(op, symbols, pc, line_no)
    is_zp = (not undef) and val <= 0xFF and not _looks_forced_absolute(op)
    if is_zp and 'zp' in modes:
        return 'zp', op, undef, val
    if 'abs' in modes:
        return 'abs', op, undef, val
    if 'zp' in modes:
        return 'zp', op, undef, val
    raise AsmError(f"{mnemonic} does not support that addressing mode", line_no)


def _looks_forced_absolute(expr_text):
    """A 4-digit-or-more hex literal like $0400 should stay absolute even
    though its value might coincidentally be < 256 (it won't be, but this
    also protects '$00,X' style zero-page forcing edge cases)."""
    t = expr_text.strip()
    if t.startswith('$'):
        return len(t) - 1 > 2
    return False


# ---------------------------------------------------------------------------
# Assembler core (two-pass)
# ---------------------------------------------------------------------------

class Assembler:
    def __init__(self):
        self.symbols = {}
        self.lines = []       # (line_no, raw_text, label, op, operand)
        self.listing = []

    def load(self, source_text):
        for i, raw in enumerate(source_text.splitlines(), start=1):
            label, op, operand = split_line(raw, i)
            self.lines.append((i, raw, label, op, operand))

    def assemble(self):
        self._pass(pass_no=1)
        result = self._pass(pass_no=2)
        return result

    def _pass(self, pass_no):
        pc = 0x0801
        origin = None
        output = bytearray()

        for (line_no, raw, label, op, operand) in self.lines:
            entry_pc = pc

            if op == '.basic':
                stub, code_start = self._basic_stub_fixed_point()
                if pass_no == 2:
                    output.extend(stub)
                origin = 0x0801
                pc = code_start
                if label:
                    self._define_symbol(label, pc, line_no, pass_no)
                continue

            if op == '.org':
                val, undef = eval_expr(operand, self.symbols, pc, line_no)
                if undef and pass_no == 2:
                    raise AsmError(f"Undefined symbol in .org expression", line_no, raw)
                pc = val
                if origin is None:
                    origin = pc
                if label:
                    self._define_symbol(label, pc, line_no, pass_no)
                continue

            if op == '=':
                val, undef = eval_expr(operand, self.symbols, pc, line_no)
                self._define_symbol(label, val, line_no, pass_no, allow_redefine=True)
                continue

            if label and op not in ('.org', '='):
                self._define_symbol(label, pc, line_no, pass_no)

            if op is None:
                continue

            if op in ('.byte', '.db'):
                for a in split_args(operand):
                    if a.startswith('"'):
                        s = a[1:-1] if a.endswith('"') else a[1:]
                        b = ascii_to_petscii(s)
                        if pass_no == 2:
                            output.extend(b)
                        pc += len(b)
                    else:
                        val, undef = eval_expr(a, self.symbols, pc, line_no)
                        if undef and pass_no == 2:
                            raise AsmError(f"Undefined symbol in .byte '{a}'", line_no, raw)
                        if pass_no == 2:
                            output.append(val & 0xFF)
                        pc += 1
                continue

            if op in ('.word', '.dw'):
                for a in split_args(operand):
                    val, undef = eval_expr(a, self.symbols, pc, line_no)
                    if undef and pass_no == 2:
                        raise AsmError(f"Undefined symbol in .word '{a}'", line_no, raw)
                    if pass_no == 2:
                        output.append(val & 0xFF)
                        output.append((val >> 8) & 0xFF)
                    pc += 2
                continue

            if op in ('.text', '.asc'):
                for a in split_args(operand):
                    s = a[1:-1] if a.startswith('"') and a.endswith('"') else a
                    b = ascii_to_petscii(s)
                    if pass_no == 2:
                        output.extend(b)
                    pc += len(b)
                continue

            if op in ('.fill', '.ds', '.res'):
                args = split_args(operand)
                count, undef = eval_expr(args[0], self.symbols, pc, line_no)
                fill_val = 0
                if len(args) > 1:
                    fill_val, _ = eval_expr(args[1], self.symbols, pc, line_no)
                if pass_no == 2:
                    output.extend(bytes([fill_val & 0xFF]) * count)
                pc += count
                continue

            # Real instruction
            mnemonic = op
            mode, expr_text, undef, val = parse_operand(mnemonic, operand,
                                                          self.symbols, pc, line_no)
            size = MODE_SIZE[mode]

            if pass_no == 2:
                if mnemonic not in OPCODES or mode not in OPCODES[mnemonic]:
                    raise AsmError(f"Invalid addressing mode for {mnemonic}", line_no, raw)
                opcode = OPCODES[mnemonic][mode]
                if undef:
                    raise AsmError(f"Undefined symbol in operand '{operand}'", line_no, raw)

                if mode == 'rel':
                    target = val
                    offset = target - (entry_pc + 2)
                    if offset < -128 or offset > 127:
                        raise AsmError(
                            f"Branch target out of range ({offset:+d}) for {mnemonic} {operand}",
                            line_no, raw)
                    output.append(opcode)
                    output.append(offset & 0xFF)
                elif size == 1:
                    output.append(opcode)
                elif size == 2:
                    output.append(opcode)
                    output.append(val & 0xFF)
                elif size == 3:
                    output.append(opcode)
                    output.append(val & 0xFF)
                    output.append((val >> 8) & 0xFF)

                self.listing.append((entry_pc, raw, output[-size:] if size else b''))

            pc += size

        if origin is None:
            origin = 0x0801
        return origin, bytes(output)

    def _build_basic_stub(self, sys_target):
        """Build the byte sequence for a tokenized BASIC line '10 SYS
        <sys_target>' living at $0801, given a specific target address."""
        addr = 0x0801
        body = bytes([0x9E]) + str(sys_target).encode('ascii') + b'\x00'
        line_len = 2 + 2 + len(body)
        next_addr = addr + line_len
        stub = bytearray()
        stub.append(next_addr & 0xFF)
        stub.append((next_addr >> 8) & 0xFF)
        stub.append(10 & 0xFF)          # line number 10, low byte
        stub.append((10 >> 8) & 0xFF)   # line number 10, high byte
        stub.extend(body)
        stub.append(0x00)               # end of BASIC program (null pointer)
        stub.append(0x00)
        return bytes(stub)

    def _basic_stub_fixed_point(self):
        """The stub's length depends on the decimal digit-count of the SYS
        target address, and the SYS target (= where machine code actually
        starts) depends on the stub's length. Resolve by iterating to a
        fixed point (this always converges in at most 2 steps for any
        realistic C64 address)."""
        target = 0x0801 + 13   # initial guess: typical stub length
        for _ in range(4):
            stub = self._build_basic_stub(target)
            new_target = 0x0801 + len(stub)
            if new_target == target:
                return stub, new_target
            target = new_target
        return stub, new_target

    def _define_symbol(self, name, value, line_no, pass_no, allow_redefine=False):
        if pass_no == 1:
            if name in self.symbols and not allow_redefine and self.symbols[name] != value:
                raise AsmError(f"Symbol '{name}' already defined", line_no)
            self.symbols[name] = value
        else:
            self.symbols[name] = value


def assemble_source(source_text):
    asm = Assembler()
    asm.load(source_text)
    origin, code = asm.assemble()
    return origin, code, asm.listing, asm.symbols


def main():
    parser = argparse.ArgumentParser(description="Two-pass 6502/6510 assembler for the Commodore 64.")
    parser.add_argument('input', help="Input assembly source file")
    parser.add_argument('-o', '--output', required=True, help="Output .prg file")
    parser.add_argument('--listing', help="Optional assembly listing output file")
    args = parser.parse_args()

    with open(args.input, 'r') as f:
        source = f.read()

    try:
        origin, code, listing, symbols = assemble_source(source)
    except AsmError as e:
        print(f"Assembly error: {e}", file=sys.stderr)
        sys.exit(1)

    with open(args.output, 'wb') as f:
        f.write(bytes([origin & 0xFF, (origin >> 8) & 0xFF]))
        f.write(code)

    print(f"Assembled {len(code)} bytes, origin=${origin:04X} -> {args.output}")

    if args.listing:
        with open(args.listing, 'w') as f:
            f.write(f"; c64asm listing  (origin ${origin:04X}, {len(code)} bytes)\n\n")
            for (addr, raw, byts) in listing:
                hexb = ' '.join(f'{b:02X}' for b in byts)
                f.write(f"{addr:04X}  {hexb:<9} {raw.rstrip()}\n")
            f.write("\nSymbol table:\n")
            for name in sorted(symbols):
                f.write(f"  {name:<20} = ${symbols[name]:04X}\n")
        print(f"Listing written to {args.listing}")


if __name__ == '__main__':
    main()
