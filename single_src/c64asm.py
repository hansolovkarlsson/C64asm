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
    .align 64            pad with zero bytes up to the next multiple of N
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
import os
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
              '.fill', '.ds', '.res', '.basic', '.equ', '.align',
              '.if', '.elif', '.else', '.endif', '.ifdef', '.ifndef'}

MAX_MACRO_EXPANSION_DEPTH = 16   # guards against runaway/infinite recursive macros
MAX_INCLUDE_DEPTH = 16           # guards against runaway/circular .include chains
MAX_COND_DEPTH = 16               # guards against runaway .if/.ifdef nesting


# Filename-aware error messages, for .include support -- see the
# "Includes" section further down for the full design. This is
# deliberately global, mutable, single-threaded state rather than a
# filename parameter threaded through eval_expr() and every other
# function that can raise an AsmError: doing that properly would mean
# touching a large fraction of this file's functions to plumb a value
# through that, for the overwhelming majority of programs (anything not
# using .include), is never even read. Reading a small piece of global
# state at the exact moment an error is actually raised gets the same
# result with a far smaller footprint -- and it's safe here specifically
# because assembly is strictly sequential and single-threaded: there is
# only ever one "currently relevant" file for error-reporting purposes
# at any given moment, whether during preprocessing or during either
# assembly pass.
_current_error_file = None
_multi_file_mode = False   # only becomes true once .include is actually
                            # used; until then, error messages are
                            # byte-for-byte identical to how they always
                            # were, with no filename shown at all


def _set_error_file(filename):
    global _current_error_file
    _current_error_file = filename


def _note_include_used():
    global _multi_file_mode
    _multi_file_mode = True


class AsmError(Exception):
    def __init__(self, message, line_no=None, text=None):
        self.message = message
        self.line_no = line_no
        self.text = text
        self.filename = _current_error_file if _multi_file_mode else None
        super().__init__(str(self))

    def __str__(self):
        if self.line_no is not None:
            if self.filename:
                loc = f"{self.filename}, line {self.line_no}"
            else:
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
        if kind == 'ident':
            name = tok
            if name in self.symbols:
                return self.symbols[name]
            self.undefined = True
            return 0  # placeholder during pass 1 / forward reference
        if kind == 'op' and tok == '*':
            # A '*' reached here (needing a single atom) can only mean the
            # current PC -- an infix multiply would already have been
            # consumed by parse_term's loop before parse_atom was called.
            return self.pc
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
# Macros and local (@) labels
# ---------------------------------------------------------------------------
#
# Macro expansion is a preprocessing step over raw source *text*, run
# before split_line() ever sees a line -- it knows nothing about labels,
# opcodes, or addressing modes, only about ".macro"/".endmacro" blocks,
# parameter substitution, and recognizing when a line invokes a macro
# name instead of a real mnemonic.
#
# Syntax:
#     .macro NAME param1, param2
#             ; body, referencing \param1, \param2
#     .endmacro
#
# invoked like a pseudo-instruction:
#     NAME arg1, arg2
#
# Local labels use this same preprocessing layer. A label name starting
# with '@' (e.g. "@loop") is textually rewritten to a scope-specific
# global name (e.g. "__local5_loop") before split_line() ever sees it --
# so as far as the rest of the assembler (symbol table, expression
# evaluator, everything) is concerned, it's just an ordinary label; all
# the "local" behavior lives entirely in this rewriting step. A new
# scope begins each time an ordinary ("identifier:") global label is
# defined, *and* each time a macro invocation begins expanding (with the
# previous scope restored once that invocation's body is fully
# processed) -- which is what makes a macro's own @-labels distinct on
# every separate invocation, automatically, with no suffix parameter or
# other caller-side bookkeeping required. A reference to an @-label from
# outside the scope it was defined in mangles to a name that was never
# actually defined, and so becomes an ordinary "undefined symbol" error
# at assembly time -- scope violations are caught by the existing
# machinery for free, without any dedicated scope-checking code.
#
# Deliberate limitations (documented, not oversights):
#   - Macros must be defined before they're used -- there's no separate
#     pre-scan of the whole file for macro definitions first, so this
#     stays a simple single-pass expansion.
#   - A macro invocation can't share a line with a label ("foo: SOME_MACRO
#     x" doesn't work) -- put the label on its own line above instead.
#   - A new local-label scope is only recognized from the explicit
#     "identifier:" form -- a bare label with no colon doesn't start a
#     new scope. Every label in this project's own example programs uses
#     the colon form, so this is not a practical restriction, but it is
#     a real one worth knowing about.
#   - '@' inside a double-quoted string (e.g. .text "user@example.com")
#     is left alone, not mangled.

LOCAL_LABEL_PREFIX = '__local'


def line_defines_global_label(trimmed):
    """True if `trimmed` starts with an identifier (not starting with
    '@') immediately followed by ':' -- the one thing that advances the
    local-label scope for ordinary, non-macro code. See the module
    comment above for why only this specific form is recognized."""
    if not trimmed or trimmed[0] == '@' or not (trimmed[0].isalpha() or trimmed[0] == '_'):
        return False
    i = 0
    n = len(trimmed)
    while i < n and (trimmed[i].isalnum() or trimmed[i] == '_'):
        i += 1
    return i < n and trimmed[i] == ':'


def mangle_local_labels(text, scope_id):
    """Replaces every @name in `text` with a scope-specific global name,
    skipping anything inside a double-quoted string."""
    out = []
    i = 0
    n = len(text)
    in_str = False
    while i < n:
        c = text[i]
        if c == '"':
            in_str = not in_str
            out.append(c)
            i += 1
        elif c == '@' and not in_str and i + 1 < n and (text[i+1].isalpha() or text[i+1] == '_'):
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == '_'):
                j += 1
            name = text[i+1:j]
            out.append(f"{LOCAL_LABEL_PREFIX}{scope_id}_{name}")
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


class MacroDef:
    def __init__(self, name, params):
        self.name = name
        self.params = params      # list of parameter names, no backslash
        self.body = []            # list of raw (comment-stripped) text lines


# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------
#
# .include "path" splices another file's lines into the source stream at
# that point, as if they'd been pasted in directly -- resolved relative
# to the directory of the file *containing* the .include line (not the
# current working directory), which is what lets a library file .include
# another library file sitting next to it, regardless of where the
# assembler itself was invoked from.
#
# Handles three things a naive "just open and read the file" version
# wouldn't:
#   - Circular includes (A includes B includes A, directly or through a
#     longer chain) are detected and reported with the full chain, not
#     left to hang or to fail with a generic "too deep" message.
#   - A hard depth limit as a backstop, in case some gap in the above
#     were ever missed.
#   - Automatic include-once semantics: a file that's already been fully
#     processed earlier in this run is silently skipped on a later
#     .include, the same way #pragma once works in C. This assembler
#     has no conditional assembly, so it has no way to write a manual
#     include guard -- and a shared library file (constants, common
#     macros) being .include'd from more than one other file is the
#     normal, expected case for "library files", not a mistake to flag.
#
# Both cycle detection and include-once comparison are done against each
# file's canonical, symlink-and-`..`-resolved path (via os.path.realpath),
# not the literal text after ".include" -- so the same physical file
# reached via two syntactically different relative paths (e.g. from two
# files in different directories) is still correctly recognized as the
# same file. Display names shown in error messages use the resolved-but-
# not-canonicalized form instead (e.g. "lib/util.inc"), matching how the
# source actually refers to it, since the fully-canonicalized form is
# usually a long, less readable absolute path.

class IncludeProcessor:
    def __init__(self, on_line):
        self.on_line = on_line              # callback(raw_line, filename, line_no)
        self.open_stack_canon = []          # canonical paths, for cycle detection
        self.open_stack_display = []        # matching display names, for error messages
        self.already_included = set()       # canonical paths fully processed already

    def process_file(self, requested_path, including_file, including_line_no, including_raw):
        if including_file and not os.path.isabs(requested_path):
            resolved_display = os.path.join(os.path.dirname(including_file), requested_path)
        else:
            resolved_display = requested_path

        try:
            canon = os.path.realpath(resolved_display)
            if not os.path.isfile(canon):
                raise OSError()
        except OSError:
            if including_file:
                raise AsmError(f"Cannot open included file '{resolved_display}'",
                                including_line_no, including_raw)
            else:
                sys.exit(f"Cannot open input file '{resolved_display}'")

        if canon in self.already_included:
            return  # include-once: silently skip a file already fully processed

        if canon in self.open_stack_canon:
            chain = ' -> '.join(self.open_stack_display + [resolved_display])
            raise AsmError(f"circular .include detected: {chain}",
                            including_line_no, including_raw)

        if len(self.open_stack_canon) >= MAX_INCLUDE_DEPTH:
            raise AsmError(
                f".include nested too deeply (max {MAX_INCLUDE_DEPTH}) "
                f"-- possible circular include?",
                including_line_no, including_raw)

        self.open_stack_canon.append(canon)
        self.open_stack_display.append(resolved_display)
        _set_error_file(resolved_display)

        with open(resolved_display, 'r') as f:
            for line_no, raw in enumerate(f, start=1):
                self.on_line(raw, resolved_display, line_no)

        self.open_stack_canon.pop()
        self.open_stack_display.pop()
        self.already_included.add(canon)
        _set_error_file(self.open_stack_display[-1] if self.open_stack_display else None)


class MacroProcessor:
    """Feeds raw source lines through macro expansion. Lines that are part
    of a macro definition, that are themselves a macro invocation
    (expanded, recursively), or that are an .include directive (spliced
    in via include_processor, also recursively), never reach `emit`;
    every other line does, via `emit(raw_line, filename, line_no)`.

    filename is threaded alongside line_no everywhere line_no already
    flows through this class, including through macro expansion --
    which means an error inside an expanded macro body is attributed to
    the file *and* line of the invocation, not wherever the macro
    itself happened to be defined, consistent with how line numbers
    already worked before .include existed."""

    def __init__(self, emit):
        self.macros = {}          # NAME.upper() -> MacroDef
        self.capturing = None     # MacroDef currently being defined, or None
        self.expansion_depth = 0
        self.emit = emit
        self.current_scope = 0    # scope 0: everything before the first
                                   # global label or macro expansion
        self.next_scope = 1       # ever-increasing; a fresh value is
                                   # handed out for every new scope, so
                                   # no two scopes ever share an id
        self.scope_stack = []     # saved current_scope values, for
                                   # restoring the enclosing scope after
                                   # a (possibly nested) macro expansion
        self.include_processor = None   # set by Assembler.load(), after
                                          # construction (the two objects
                                          # need a reference to each other)

    def process_line(self, raw_line, filename, line_no):
        stripped = strip_comment(raw_line).rstrip('\n')
        trimmed = stripped.strip()

        if self.capturing is not None:
            first_tok = trimmed.split(None, 1)[0] if trimmed else ''
            if first_tok.upper() in ('.ENDMACRO', '.ENDM'):
                self.macros[self.capturing.name.upper()] = self.capturing
                self.capturing = None
                return
            if first_tok.upper() == '.MACRO':
                raise AsmError(
                    f"nested macro definitions are not supported (already defining '{self.capturing.name}')",
                    line_no, raw_line)
            self.capturing.body.append(stripped)
            return

        if not trimmed:
            self._emit_final(raw_line, filename, line_no)
            return

        first_tok = trimmed.split(None, 1)[0]

        if first_tok.upper() == '.MACRO':
            rest = trimmed[len(first_tok):].strip()
            parts = rest.split(None, 1)
            if not parts:
                raise AsmError("'.macro' requires a name", line_no, raw_line)
            name = parts[0]
            if name.upper() in OPCODES or name.lower() in DIRECTIVES:
                raise AsmError(
                    f"macro name '{name}' conflicts with a built-in mnemonic or directive",
                    line_no, raw_line)
            if name.upper() in self.macros:
                raise AsmError(f"macro '{name}' already defined", line_no, raw_line)
            params = []
            if len(parts) > 1:
                params = [p.strip() for p in split_args(parts[1])]
            self.capturing = MacroDef(name, params)
            return

        if first_tok.upper() in ('.ENDMACRO', '.ENDM'):
            raise AsmError("'.endmacro' with no matching '.macro'", line_no, raw_line)

        if first_tok.upper() == '.INCLUDE':
            rest = trimmed[len(first_tok):].strip()
            if len(rest) < 2 or rest[0] != '"' or rest[-1] != '"':
                raise AsmError('.include requires a quoted path, e.g. .include "lib.inc"',
                                line_no, raw_line)
            path = rest[1:-1]
            _note_include_used()
            self.include_processor.process_file(path, filename, line_no, raw_line)
            return

        m = self.macros.get(first_tok.upper())
        if m is not None:
            arg_text = trimmed[len(first_tok):].strip()
            args = split_args(arg_text) if arg_text else []
            if len(args) != len(m.params):
                raise AsmError(
                    f"macro '{m.name}' expects {len(m.params)} argument(s), got {len(args)}",
                    line_no, raw_line)
            self.expansion_depth += 1
            if self.expansion_depth > MAX_MACRO_EXPANSION_DEPTH:
                raise AsmError(
                    f"macro expansion nested too deep (recursive macro '{m.name}'?)",
                    line_no, raw_line)

            # A fresh, globally-unique scope for this invocation -- every
            # invocation, even of the same macro, even nested calls, gets
            # one it will never share with any other invocation.
            self.scope_stack.append(self.current_scope)
            self.current_scope = self.next_scope
            self.next_scope += 1

            for body_line in m.body:
                substituted = self._substitute(body_line, m.params, args, line_no)
                self.process_line(substituted, filename, line_no)

            self.current_scope = self.scope_stack.pop()
            self.expansion_depth -= 1
            return

        self._emit_final(raw_line, filename, line_no)

    def _emit_final(self, raw_line, filename, line_no):
        """The single choke point every genuinely final (non-macro,
        non-definition, non-include) line passes through, whether it
        came from ordinary source text or from expanding a macro body.
        Advances the local-label scope on an ordinary global-label line,
        then mangles any @name references using whatever scope is
        current."""
        stripped = strip_comment(raw_line).rstrip('\n')
        trimmed = stripped.strip()
        if line_defines_global_label(trimmed):
            self.current_scope = self.next_scope
            self.next_scope += 1
        mangled = mangle_local_labels(raw_line, self.current_scope)
        self.emit(mangled, filename, line_no)

    def _substitute(self, text, params, args, line_no):
        """Replaces every \\paramname in `text` with its argument. A
        backslash not followed by an identifier character is left as a
        literal backslash (harmless, since this syntax has no other use
        for one); a \\name that doesn't match any declared parameter is
        a fatal error, to catch typos rather than silently leaving the
        literal text in place."""
        out = []
        i = 0
        n = len(text)
        while i < n:
            c = text[i]
            if c == '\\' and i + 1 < n and (text[i+1].isalpha() or text[i+1] == '_'):
                j = i + 1
                while j < n and (text[j].isalnum() or text[j] == '_'):
                    j += 1
                pname = text[i+1:j]
                if pname not in params:
                    raise AsmError(f"unknown macro parameter '\\{pname}'", line_no, text)
                out.append(args[params.index(pname)])
                i = j
            else:
                out.append(c)
                i += 1
        return ''.join(out)


# ---------------------------------------------------------------------------
# Assembler core (two-pass)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Conditional assembly
# ---------------------------------------------------------------------------
#
# .if expr / .elif expr / .else / .endif
# .ifdef NAME / .else / .endif
# .ifndef NAME / .else / .endif
#
# Unlike macros and .include, this is an *assembly-time* feature, handled
# directly in Assembler._pass() rather than as a preprocessing step --
# deliberately, so a condition can see real constants and labels (like a
# PAL/NTSC flag defined with "="), not just things known before any real
# parsing happens. The trade-off: .if can gate whether instructions and
# data get assembled, but it can't gate which .macro gets *defined* or
# which file gets .include'd, since those are already fully resolved
# before .if is ever evaluated.
#
# Two correctness requirements come directly from this being a two-pass
# assembler, and are easy to get wrong:
#
#  1. ".if"/".elif" conditions must not reference a forward-declared
#     symbol -- this is an unconditional error, checked the same way on
#     both passes, *not* deferred to pass 2 the way other expressions'
#     undefined-symbol checks are (see .org/.align above). The reason:
#     for an ordinary expression, pass 1 guessing wrong about an
#     undefined symbol's value only affects a byte *value*, silently
#     corrected once pass 2 knows better. For "if", a wrong guess
#     changes which lines exist at all -- which would desynchronize
#     every address computed after it between the two passes. Requiring
#     the condition to be fully known equally on both passes is what
#     keeps that from ever happening.
#
#  2. ".ifdef"/".ifndef" must NOT simply check "is this symbol in the
#     symbol table right now". self.symbols is never reset between pass
#     1 and pass 2 (pass 2 needs pass 1's complete table to resolve
#     forward references) -- which means by the time pass 2 *starts*,
#     the table already contains every symbol defined anywhere in the
#     file, including ones that don't textually appear until later. A
#     plain existence check would see "not defined" during pass 1
#     (walking forward, symbol not reached yet) but "defined" during
#     pass 2, for the exact same .ifdef line -- the two passes would
#     disagree about whether that line's block even exists. The fix:
#     self.symbol_first_li tracks the line *index* each symbol was
#     first defined at, and .ifdef asks "was it defined strictly before
#     this line's index" rather than "does it exist right now". Since
#     both passes walk self.lines in the same order, that question has
#     the same answer on both passes.

class CondFrame:
    """One entry per currently-open .if/.ifdef/.ifndef block."""
    def __init__(self, is_ifdef_style, opened_line_no, opened_raw):
        self.is_ifdef_style = is_ifdef_style   # True for .ifdef/.ifndef,
                                                 # which don't allow .elif
        self.currently_active = False   # is the branch we're IN right now
                                          # (if/elif/else) the active one?
        self.condition_met = False      # has ANY branch in this block
                                          # already been taken? (stops a
                                          # later .elif/.else from also
                                          # activating)
        self.opened_line_no = opened_line_no   # where this block's own
        self.opened_raw = opened_raw            # .if/.ifdef/.ifndef line
                                                  # was, for a helpful
                                                  # "unclosed at end of
                                                  # file" error message


class Assembler:
    def __init__(self):
        self.symbols = {}
        self.symbol_first_li = {}   # name -> index into self.lines where it
                                      # was FIRST defined (never updated on a
                                      # constant's redefinition) -- used only
                                      # by .ifdef/.ifndef; see the note in
                                      # _pass() for why plain "is this symbol
                                      # in self.symbols" isn't safe there
        self.lines = []       # (line_no, raw_text, label, op, operand, filename)
        self.listing = []

    def load(self, main_path):
        def emit(raw, filename, line_no):
            label, op, operand = split_line(raw, line_no)
            self.lines.append((line_no, raw, label, op, operand, filename))

        macros = MacroProcessor(emit)
        includes = IncludeProcessor(macros.process_line)
        macros.include_processor = includes   # see MacroProcessor.__init__
        includes.process_file(main_path, None, 0, None)

    def assemble(self):
        self._pass(pass_no=1)
        result = self._pass(pass_no=2)
        return result

    def _eval_if_condition(self, operand, pc, line_no, raw):
        """Evaluates a .if/.elif expression. Returns (truthy, truthy) --
        the pair is just for call-site symmetry with the .ifdef/.ifndef
        branch in _pass(), which also produces (currently_active,
        condition_met) and happens to have them be equal in the simple
        .if case too (there's no "already taken" concept from a single
        evaluation the way an .elif *chain* has across calls).

        Unlike every other expression in this assembler, an undefined
        symbol here is a hard error on *both* passes, not just pass 2 --
        see the design note above CondFrame for why that's required for
        two-pass correctness, not just a style choice."""
        val, undef = eval_expr(operand, self.symbols, pc, line_no)
        if undef:
            raise AsmError(
                "Undefined symbol in .if/.elif expression -- forward references "
                "are not allowed in conditional-assembly expressions",
                line_no, raw)
        cond = (val != 0)
        return cond, cond

    def _pass(self, pass_no):
        pc = 0x0801
        origin = None
        output = bytearray()
        cond_stack = []   # see the ".if"/".ifdef" handling below

        def parent_active():
            return not cond_stack or cond_stack[-1].currently_active

        def currently_skipping():
            return bool(cond_stack) and not cond_stack[-1].currently_active

        for li, (line_no, raw, label, op, operand, filename) in enumerate(self.lines):
            _set_error_file(filename)
            entry_pc = pc

            if op == '.if' or op == '.ifdef' or op == '.ifndef':
                if len(cond_stack) >= MAX_COND_DEPTH:
                    raise AsmError(f"conditional nesting too deep (max {MAX_COND_DEPTH})",
                                    line_no, raw)
                frame = CondFrame(is_ifdef_style=(op != '.if'), opened_line_no=line_no, opened_raw=raw)
                if not parent_active():
                    # An enclosing branch is already false, so this whole
                    # block is dead regardless of its own condition --
                    # don't even evaluate it (it may reference symbols
                    # that don't exist, which would otherwise be a
                    # spurious error for code that was never going to run
                    # anyway), and make sure none of ITS .elif/.else
                    # branches can activate either.
                    frame.currently_active = False
                    frame.condition_met = True
                elif op == '.if':
                    frame.currently_active, frame.condition_met = \
                        self._eval_if_condition(operand, pc, line_no, raw)
                else:
                    is_defined = (operand in self.symbol_first_li and
                                  self.symbol_first_li[operand] < li)
                    cond = is_defined if op == '.ifdef' else not is_defined
                    frame.currently_active = cond
                    frame.condition_met = cond
                cond_stack.append(frame)
                continue

            if op == '.elif':
                if not cond_stack:
                    raise AsmError("'.elif' with no matching '.if'", line_no, raw)
                frame = cond_stack[-1]
                if frame.is_ifdef_style:
                    raise AsmError("'.elif' is not allowed after '.ifdef'/'.ifndef' "
                                    "(only after '.if')", line_no, raw)
                outer_ok = len(cond_stack) == 1 or cond_stack[-2].currently_active
                if not outer_ok or frame.condition_met:
                    frame.currently_active = False
                else:
                    frame.currently_active, cond = \
                        self._eval_if_condition(operand, pc, line_no, raw)
                    if cond:
                        frame.condition_met = True
                continue

            if op == '.else':
                if not cond_stack:
                    raise AsmError("'.else' with no matching '.if'", line_no, raw)
                frame = cond_stack[-1]
                outer_ok = len(cond_stack) == 1 or cond_stack[-2].currently_active
                frame.currently_active = outer_ok and not frame.condition_met
                frame.condition_met = True
                continue

            if op == '.endif':
                if not cond_stack:
                    raise AsmError("'.endif' with no matching '.if'/'.ifdef'/'.ifndef'",
                                    line_no, raw)
                cond_stack.pop()
                continue

            if currently_skipping():
                continue

            if op == '.basic':
                stub, code_start = self._basic_stub_fixed_point()
                if pass_no == 2:
                    output.extend(stub)
                origin = 0x0801
                pc = code_start
                if label:
                    self._define_symbol(label, pc, line_no, pass_no, li, raw=raw)
                continue

            if op == '.org':
                val, undef = eval_expr(operand, self.symbols, pc, line_no)
                if undef and pass_no == 2:
                    raise AsmError(f"Undefined symbol in .org expression", line_no, raw)
                if origin is None:
                    origin = val
                elif pass_no == 2:
                    current_abs = origin + len(output)
                    gap = val - current_abs
                    if gap < 0:
                        raise AsmError(
                            f".org cannot move the program counter backward "
                            f"(from ${current_abs:04X} to ${val:04X}) -- the "
                            f"assembler can't overwrite bytes already assembled",
                            line_no, raw)
                    if gap > 0:
                        output.extend(b'\x00' * gap)
                pc = val
                if label:
                    self._define_symbol(label, pc, line_no, pass_no, li, raw=raw)
                continue

            if op == '.align':
                # Advances pc to the next multiple of `n`, padding the
                # skipped bytes with zero -- exactly .org's forward-gap
                # logic above, just with the target computed by rounding
                # up rather than given directly. pc can never move
                # *backward* here (target is always >= pc by
                # construction), so there's no equivalent of .org's
                # "moving backward" error to check for.
                n, undef = eval_expr(operand, self.symbols, pc, line_no)
                if undef and pass_no == 2:
                    raise AsmError(f"Undefined symbol in .align expression", line_no, raw)
                if undef:
                    # Forward-referenced alignment value, pass 1 only
                    # (pass 2 would already have raised above). n is
                    # just eval_expr's undefined-symbol placeholder (0)
                    # here, not a real value -- validating its sign or
                    # dividing by it would be meaningless (and, for 0
                    # specifically, a division by zero), so pc simply
                    # doesn't advance this pass. That never produces
                    # incorrect output: pass 2 always catches the
                    # undefined symbol and aborts before anything
                    # computed from a wrong pass-1 address could ship.
                    target = pc
                else:
                    if n <= 0:
                        raise AsmError(
                            f".align requires a positive alignment value (got {n})",
                            line_no, raw)
                    target = ((pc + n - 1) // n) * n
                if pass_no == 2:
                    gap = target - pc
                    if gap > 0:
                        output.extend(b'\x00' * gap)
                pc = target
                if label:
                    self._define_symbol(label, pc, line_no, pass_no, li, raw=raw)
                continue

            if op == '=':
                val, undef = eval_expr(operand, self.symbols, pc, line_no)
                self._define_symbol(label, val, line_no, pass_no, li, allow_redefine=True, raw=raw)
                continue

            if label and op not in ('.org', '='):
                self._define_symbol(label, pc, line_no, pass_no, li, raw=raw)

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

        if cond_stack:
            raise AsmError(
                "unclosed '.if'/'.ifdef'/'.ifndef' at end of file (missing '.endif')",
                cond_stack[-1].opened_line_no, cond_stack[-1].opened_raw)
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

    def _define_symbol(self, name, value, line_no, pass_no, li, allow_redefine=False, raw=None):
        if name not in self.symbols:
            self.symbol_first_li[name] = li
        if pass_no == 1:
            if name in self.symbols and not allow_redefine and self.symbols[name] != value:
                raise AsmError(f"Symbol '{name}' already defined", line_no, raw)
            self.symbols[name] = value
        else:
            self.symbols[name] = value


def assemble_source(main_path):
    asm = Assembler()
    asm.load(main_path)
    origin, code = asm.assemble()
    return origin, code, asm.listing, asm.symbols


def main():
    parser = argparse.ArgumentParser(description="Two-pass 6502/6510 assembler for the Commodore 64.")
    parser.add_argument('input', help="Input assembly source file")
    parser.add_argument('-o', '--output', required=True, help="Output .prg file")
    parser.add_argument('--listing', help="Optional assembly listing output file")
    args = parser.parse_args()

    # File reading (of the main file, and of anything it .include's) now
    # happens inside Assembler.load() -> IncludeProcessor, which already
    # has a clean "Cannot open input file" message for exactly this case
    # -- so there's no separate open() here to fail with a raw traceback
    # if args.input doesn't exist.
    try:
        origin, code, listing, symbols = assemble_source(args.input)
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
