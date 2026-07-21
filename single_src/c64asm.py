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

# ---------------------------------------------------------------------------
# Illegal / undocumented opcodes -- see c64asm-reference.md's "Illegal
# opcodes" section for the full user-facing explanation. These are real
# instructions the NMOS 6502/6510 executes (nothing in the silicon
# actually "traps" an unused opcode byte the way, say, an undefined
# machine-code instruction on a modern CPU would), but MOS never
# documented or supported them, and their exact behavior sometimes
# differs subtly between individual chips -- see the notes below on
# which of these are considered unstable.
#
# Mnemonics and opcode assignments follow the widely-used oxyron.de
# table (http://www.oxyron.de/html/opcodes02.html), the standard
# C64-scene reference for this. A few of these opcodes have more than
# one valid encoding for the exact same mnemonic+mode (e.g. ANC is both
# $0B and $2B) -- this assembler always emits the lower/more common one
# of the two, which is what every other assembler of this kind does; a
# disassembler would need to preserve the distinction, but this is an
# assembler, not a disassembler, so only one encoding per mnemonic+mode
# is ever needed.
#
# $EB is a byte-for-byte functional duplicate of SBC #imm ($E9) -- to
# avoid a collision with the real, documented SBC mnemonic in the table
# above, it's given the distinct mnemonic USBC here, following the same
# convention several other illegal-opcode assemblers use.
ILLEGAL_OPCODES = {
    'SLO':  {'zp':0x07,'zpx':0x17,'indx':0x03,'indy':0x13,'abs':0x0F,'absx':0x1F,'absy':0x1B},
    'RLA':  {'zp':0x27,'zpx':0x37,'indx':0x23,'indy':0x33,'abs':0x2F,'absx':0x3F,'absy':0x3B},
    'SRE':  {'zp':0x47,'zpx':0x57,'indx':0x43,'indy':0x53,'abs':0x4F,'absx':0x5F,'absy':0x5B},
    'RRA':  {'zp':0x67,'zpx':0x77,'indx':0x63,'indy':0x73,'abs':0x6F,'absx':0x7F,'absy':0x7B},
    'SAX':  {'zp':0x87,'zpy':0x97,'indx':0x83,'abs':0x8F},
    'LAX':  {'zp':0xA7,'zpy':0xB7,'indx':0xA3,'indy':0xB3,'abs':0xAF,'absy':0xBF,'imm':0xAB},
    'DCP':  {'zp':0xC7,'zpx':0xD7,'indx':0xC3,'indy':0xD3,'abs':0xCF,'absx':0xDF,'absy':0xDB},
    'ISC':  {'zp':0xE7,'zpx':0xF7,'indx':0xE3,'indy':0xF3,'abs':0xEF,'absx':0xFF,'absy':0xFB},
    'ANC':  {'imm':0x0B},
    'ALR':  {'imm':0x4B},
    'ARR':  {'imm':0x6B},
    'XAA':  {'imm':0x8B},   # highly unstable -- see reference doc
    'AXS':  {'imm':0xCB},
    'USBC': {'imm':0xEB},   # functional duplicate of SBC #imm
    'AHX':  {'indy':0x93,'absy':0x9F},   # highly unstable
    'SHY':  {'absx':0x9C},   # unstable
    'SHX':  {'absy':0x9E},   # unstable
    'TAS':  {'absy':0x9B},   # unstable
    'LAS':  {'absy':0xBB},
    'KIL':  {'imp':0x02},    # halts the CPU until reset; 11 other opcode
                              # bytes ($12,$22,$32,$42,$52,$62,$72,$92,
                              # $B2,$D2,$F2) do the same thing, but only
                              # one encoding is needed for assembling
}

# NOP normally only has implied-mode addressing ($EA, in OPCODES above).
# The NMOS 6502/6510 also executes several additional opcode bytes as
# NOP-with-an-ignored-operand, across four more addressing modes --
# these extend the *same* mnemonic's mode table rather than needing a
# distinct name, since they behave exactly like NOP: the operand is
# fetched (costing the extra byte(s) and cycles) and then discarded.
ILLEGAL_NOP_MODES = {'imm':0x80,'zp':0x04,'zpx':0x14,'abs':0x0C,'absx':0x1C}

for _mnem, _modes in ILLEGAL_OPCODES.items():
    OPCODES[_mnem] = dict(_modes)
OPCODES['NOP'] = dict(OPCODES['NOP'], **ILLEGAL_NOP_MODES)

# Every (mnemonic, mode) pair that requires '.cpu 6510x' to assemble --
# used by _pass() to gate illegal-opcode use behind that directive. Kept
# as a set of pairs, rather than a set of mnemonics, specifically
# because of NOP: 'imp' NOP ($EA) is perfectly legal and always
# available, while its four extra illegal modes above are not.
ILLEGAL_SLOTS = {(m, mode) for m, modes in ILLEGAL_OPCODES.items() for mode in modes}
ILLEGAL_SLOTS |= {('NOP', mode) for mode in ILLEGAL_NOP_MODES}

# Number of bytes an instruction occupies for each addressing mode.
MODE_SIZE = {'imp':1,'acc':1,'imm':2,'zp':2,'zpx':2,'zpy':2,'rel':2,
             'abs':3,'absx':3,'absy':3,'ind':3,'indx':2,'indy':2}

BRANCHES = {'BCC','BCS','BEQ','BMI','BNE','BPL','BVC','BVS'}

DIRECTIVES = {'.org', '*=', '.byte', '.db', '.word', '.dw', '.text', '.asc',
              '.fill', '.ds', '.res', '.basic', '.equ', '.align', '.cpu',
              '.charset', '.error', '.warning', '.incbin', '.assert',
              '.if', '.elif', '.else', '.endif', '.ifdef', '.ifndef'}

MAX_MACRO_EXPANSION_DEPTH = 16   # guards against runaway/infinite recursive macros
MAX_INCLUDE_DEPTH = 16           # guards against runaway/circular .include chains
MAX_COND_DEPTH = 16               # guards against runaway .if/.ifdef nesting
MAX_REPEAT_COUNT = 65536          # guards against a mistyped .repeat/.dup count
                                     # (e.g. a stray extra digit) generating an
                                     # enormous, memory-exhausting expansion


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
# Multi-error reporting. Raising AsmError (above) is for genuinely fatal
# problems -- a missing file, a circular .include, a macro or
# conditional-assembly block whose structure is broken -- where the
# shape of the rest of the source file becomes ambiguous and there's no
# reasonable way to keep going. Everything else -- an undefined symbol,
# a malformed expression, an addressing mode a mnemonic doesn't support,
# a branch out of range, a redefined symbol -- is a self-contained
# problem with one specific line or operand. For those, record_error()
# below records the message (in AsmError's exact display format)
# instead of raising, and returns normally so the calling code can
# carry on with some sensible fallback value (0 for a broken
# expression, a plausible addressing mode, the previous value for a
# symbol redefinition, and so on) -- each call site chooses its own
# fallback right where it calls this, the same way it already had to
# choose what to do in the success case.
#
# This is what lets one assembly run surface several independent
# mistakes instead of stopping at the first one. It's an intentional
# trade-off: a later error's line number and message are still exactly
# correct, but if an earlier error meant a value or an addressing-mode
# decision came out different from what the source actually implies, a
# handful of further messages may be downstream noise from that first
# real mistake rather than independent problems of their own -- fix the
# first one and reassemble if the rest look strange. This is the same
# trade-off multi-error reporting makes in essentially every compiler
# that does it.
# ---------------------------------------------------------------------------

MAX_COLLECTED_ERRORS = 20
_collected_errors = []      # formatted strings, capped at MAX_COLLECTED_ERRORS
_total_error_count = 0       # number seen, uncapped

_used_symbols = set()       # every symbol name successfully looked up by
                               # parse_atom's TK_IDENT case, across both
                               # passes -- see --warn-unused and
                               # report_unused_symbols() near main()


def any_errors_recorded():
    return _total_error_count > 0


def reset_collected_errors():
    """Called once per assemble_source() run, so a test harness or any
    other caller invoking this more than once in the same Python
    process starts each run with a clean slate."""
    global _collected_errors, _total_error_count, _used_symbols
    _collected_errors = []
    _total_error_count = 0
    _used_symbols = set()


def print_all_collected_errors_and_exit():
    for line in _collected_errors:
        print(line, file=sys.stderr)
    remaining = _total_error_count - len(_collected_errors)
    if remaining > 0:
        plural = "" if remaining == 1 else "s"
        print(f"... and {remaining} more error{plural} (stopping after {MAX_COLLECTED_ERRORS})",
              file=sys.stderr)
    plural = "" if _total_error_count == 1 else "s"
    print(f"{_total_error_count} error{plural}.", file=sys.stderr)
    sys.exit(1)


def record_error(message, line_no=None, raw=None):
    """The recoverable counterpart to `raise AsmError(...)` -- see the
    module note above. Records the message and returns normally instead
    of raising; the caller supplies its own fallback return value right
    after this call, exactly as it already had to decide what to return
    in the non-error case.

    If the number of recorded errors reaches MAX_COLLECTED_ERRORS, this
    prints everything collected so far and exits immediately -- so,
    like `raise AsmError(...)`, this function may not return, and
    callers must supply a fallback value as if it always does."""
    global _total_error_count
    _total_error_count += 1
    if len(_collected_errors) < MAX_COLLECTED_ERRORS:
        err = AsmError(message, line_no, raw)  # reuse AsmError's own
                                                   # __str__ formatting,
                                                   # without raising it
        _collected_errors.append(f"Assembly error: {err}")
        return  # still under the cap; nothing more to do
    # This is at least the (MAX_COLLECTED_ERRORS+1)th error -- stop
    # right here rather than continuing to burn through what could be
    # an enormous, mostly-noise number of further messages on a
    # badly-broken or wrong-language source file.
    print_all_collected_errors_and_exit()


def record_warning(message, line_no=None, raw=None):
    """Prints a warning message in the same '(line N: source text)'
    format record_error() uses -- but, unlike record_error(), this
    doesn't count toward the error total, doesn't stop pass 2 from
    running or output from being written, and doesn't affect the exit
    status. Two callers: the '.warning' directive's own handling in
    _pass(), and report_unused_symbols() (--warn-unused, near main())
    for an unused-symbol warning. Callers spanning more than one file
    (multiple unused symbols defined in different included files, say)
    must call _set_error_file() with the right filename before each
    call, the same as record_error()'s own callers already have to."""
    warn = AsmError(message, line_no, raw)  # reuse AsmError's own
                                               # __str__ formatting,
                                               # without raising it
    print(f"Warning: {warn}", file=sys.stderr)



# ---------------------------------------------------------------------------
# ASCII -> PETSCII mapping for .text / .asc
# ---------------------------------------------------------------------------

def ascii_to_petscii(s, lower_mode=False):
    """Map an ASCII string to C64 PETSCII bytes suitable for use with
    standard KERNAL CHROUT ($FFD2) output.

    lower_mode=False (the default, and this assembler's overall
    default via '.charset upper' -- see below): every letter, whatever
    case it was written in, becomes a PETSCII byte in the $41-$5A
    range. That range displays as uppercase on the C64's default
    (power-on) character set, which is the only character set any
    program using this mode is expected to be running under -- see the
    caveat below.

    lower_mode=True ('.charset lower'): letters keep their original
    case using PETSCII's actual encoding for it -- lowercase becomes
    $41-$5A (PETSCII's "unshifted" range, which the hardware displays
    as lowercase specifically on the *lowercase/uppercase* character
    set, not the default one) and uppercase becomes $C1-$DA
    ("shifted", which displays as uppercase on *either* character
    set). This is what actually produces mixed-case text on screen --
    but only once the C64 has been switched to the lowercase/uppercase
    character set at runtime (e.g. via text.inc's
    SET_LOWERCASE_CHARSET macro); the assembler has no way to do that
    switch itself, since it's a runtime hardware state, not something
    that exists at assembly time.

    Caveat: text assembled under '.charset upper' is only guaranteed
    to display as uppercase while the default character set is still
    active. If a program ever switches to the lowercase/uppercase set
    for some '.charset lower' text, any '.charset upper' text printed
    afterward would display as lowercase too, since $41-$5A means
    something different on that character set. Once a program switches
    character sets at runtime, use '.charset lower' for everything it
    prints from that point on -- typed-in-uppercase source text still
    displays correctly as uppercase either way, since '.charset
    lower' encodes uppercase letters using the character-set-independent
    $C1-$DA range specifically so this works.
    """
    out = bytearray()
    for ch in s:
        if lower_mode:
            if 'a' <= ch <= 'z':
                out.append(ord(ch) - ord('a') + 0x41)
            elif 'A' <= ch <= 'Z':
                out.append(ord(ch) - ord('A') + 0xC1)
            else:
                out.append(ord(ch) & 0xFF)
        else:
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
    # The '.' allowed mid-name (not just as the leading character, which
    # was already permitted) is for '.struct' field symbols (§9 --
    # section number as of this writing) like "Room.north = 2"; nothing
    # before that feature ever produced or relied on a dotted name here,
    # so this is purely additive.
    m = re.match(r'^([A-Za-z_.][A-Za-z0-9_.]*)\s*=\s*(.+)$', stripped)
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

    record_error(f"Unknown mnemonic or directive '{op}'", line_no, raw_line)
    # Treat the whole line as blank (no label, no op) rather than
    # guessing -- label's role here was already ambiguous in some paths
    # above (label? mistyped mnemonic?), so this matches the single-
    # file/split-source C versions' own choice for the same situation:
    # simple and safe rather than trying to preserve a partial guess.
    return None, None, ''


# ---------------------------------------------------------------------------
# Expression evaluation
# ---------------------------------------------------------------------------

class ExprParser:
    """A small recursive-descent parser/evaluator for assembler expressions.
    Supports + - * / ( ) unary - , unary < (low byte) and > (high byte),
    == and != (comparison, evaluating to 1 or 0 -- see parse_equality;
    deliberately not <, >, <=, >= as binary comparisons, since < and >
    are already unary low/high-byte operators here and overloading them
    for both meanings would be genuinely ambiguous to parse), hex ($),
    binary (%), decimal, character literals, '*' (current PC), and
    symbol references."""

    TOKEN_RE = re.compile(r"""
        \s*(?:
            (?P<hex>\$[0-9A-Fa-f]+)
          | (?P<bin>%[01]+)
          | (?P<char>'(\\.|[^'])')
          | (?P<dec>[0-9]+)
          | (?P<ident>[A-Za-z_.][A-Za-z0-9_.]*)
          | (?P<cmp>==|!=)
          | (?P<op>[()+\-*/<>])
        )
    """, re.VERBOSE)

    def __init__(self, text, symbols, pc, line_no):
        self.text = text
        self.symbols = symbols
        self.pc = pc
        self.line_no = line_no
        self.pos = 0
        self.undefined = False
        self.tokenize_failed = False   # set only by _tokenize()'s own error
                                          # path, distinct from `undefined`
                                          # (which also gets legitimately set
                                          # for an undefined SYMBOL found
                                          # during parsing, not just a
                                          # tokenizing problem) -- see
                                          # parse() for why this distinction
                                          # matters
        self.tokens = self._tokenize(text)

    def _tokenize(self, text):
        toks = []
        i = 0
        while i < len(text):
            m = self.TOKEN_RE.match(text, i)
            if not m or m.end() == i:
                if text[i].isspace():
                    i += 1
                    continue
                record_error(f"Bad character '{text[i]}' in expression '{text}'",
                             self.line_no)
                self.undefined = True
                self.tokenize_failed = True
                return toks  # give up on this expression rather than risk
                               # tokenizing more of a string we already know
                               # is malformed
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
        if self.tokenize_failed:
            # _tokenize already recorded its own error and gave up
            # partway through -- don't also try to parse the possibly
            # incomplete token list, which would just produce a second,
            # redundant report for the exact same underlying problem.
            # This is distinct from an empty token list from a
            # genuinely empty expression (handled separately below) or
            # from self.undefined alone (which also gets legitimately
            # set for an expression that tokenizes fine but references
            # an undefined SYMBOL -- that case has a full, valid token
            # list well worth still trying to parse).
            return 0
        if not self.tokens:
            record_error("Empty expression", self.line_no, self.text)
            self.undefined = True
            return 0
        val = self.parse_equality()
        if self.pos != len(self.tokens):
            record_error(f"Unexpected trailing text in expression '{self.text}'",
                         self.line_no)
            self.undefined = True
            return val  # still return what was successfully parsed so far,
                          # rather than discarding it for a trailing-text typo
        return val

    def parse_equality(self):
        """== and != -- deliberately the loosest-binding operators (lower
        precedence than +/-, matching the usual convention that
        "a + b == c + d" means "(a+b) == (c+d)"), and deliberately not
        chainable the way some languages allow ("a == b == c" is valid
        here but means "(a==b) == c", not "a==b and b==c"). Mainly
        meant for '.assert' (c64asm-reference.md), but available in any
        expression, evaluating to 1 (true) or 0 (false) either way."""
        val = self.parse_expr()
        kind, tok = self.peek()
        if kind == 'cmp':
            self.next()
            rhs = self.parse_expr()
            result = (val == rhs) if tok == '==' else (val != rhs)
            val = 1 if result else 0
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
                _used_symbols.add(name)
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
                record_error(f"Missing ')' in expression '{self.text}'", self.line_no)
                self.undefined = True
                # fall through: use the sub-expression's value anyway
                # rather than discarding work already correctly parsed
                # just because the closing paren is missing
            return val
        record_error(f"Cannot parse expression '{self.text}'", self.line_no)
        self.undefined = True
        return 0


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
        record_error(f"{mnemonic} requires an operand", line_no)
        return 'imp', None, True, None  # smallest footprint for "we don't
                                           # know what was meant"

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
            record_error(f"{mnemonic} does not support (zp,X) addressing", line_no)
            return 'indx', m.group(1), True, val
        return mode, m.group(1), undef, val

    # Indirect indexed: (expr),Y
    m = re.match(r'^\(\s*(.+?)\s*\)\s*,\s*[Yy]\s*$', op)
    if m:
        val, undef = eval_expr(m.group(1), symbols, pc, line_no)
        mode = 'indy' if 'indy' in modes else None
        if mode is None:
            record_error(f"{mnemonic} does not support (zp),Y addressing", line_no)
            return 'indy', m.group(1), True, val
        return mode, m.group(1), undef, val

    # Indirect (JMP only): (expr)
    m = re.match(r'^\(\s*(.+?)\s*\)$', op)
    if m:
        val, undef = eval_expr(m.group(1), symbols, pc, line_no)
        mode = 'ind' if 'ind' in modes else None
        if mode is None:
            record_error(f"{mnemonic} does not support indirect addressing", line_no)
            return 'ind', m.group(1), True, val
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
        record_error(f"{mnemonic} does not support that addressing mode", line_no)
        return ('zp' if is_zp else 'abs'), expr_text, True, val

    # Plain expr -> zero page or absolute
    val, undef = eval_expr(op, symbols, pc, line_no)
    is_zp = (not undef) and val <= 0xFF and not _looks_forced_absolute(op)
    if is_zp and 'zp' in modes:
        return 'zp', op, undef, val
    if 'abs' in modes:
        return 'abs', op, undef, val
    if 'zp' in modes:
        return 'zp', op, undef, val
    record_error(f"{mnemonic} does not support that addressing mode", line_no)
    return 'imp', op, True, val  # reachable now: every real addressing
                                    # mode has been ruled out


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

def resolve_asset_path(requested_path, including_file, lib_dir):
    """Resolves a quoted path from '.include'/'.incbin' to an actual
    file on disk -- shared by IncludeProcessor.process_file() below and
    '.incbin's own handling in _pass(), so the two follow identical
    rules: relative to the file containing the directive first (the
    default, always tried first), then --lib-dir as a fallback (see the
    comment where IncludeProcessor.process_file() used to inline this
    same logic, before .incbin needed it too).

    Returns (resolved_display, lib_dir_display) -- resolved_display is
    where the file was actually found (or the primary path attempted,
    if neither location has it); lib_dir_display is the --lib-dir
    fallback path that was tried, or None if --lib-dir wasn't
    consulted (either because it wasn't given, or the default
    resolution already succeeded). Callers use lib_dir_display only to
    decide whether an error message should mention it."""
    if including_file and not os.path.isabs(requested_path):
        resolved_display = os.path.join(os.path.dirname(including_file), requested_path)
    else:
        resolved_display = requested_path

    # --lib-dir is purely a fallback: the default resolution above
    # (relative to the file containing the directive) is always tried
    # first and, if it finds the file, --lib-dir is never even
    # consulted -- so a project with its own local lib/ next to it
    # keeps working unchanged whether or not --lib-dir is given. Only
    # when that default lookup comes up empty, and --lib-dir was
    # given, and the requested path isn't absolute (an absolute path
    # is never subject to search-path fallback, same as the default
    # resolution above), do we also try requested_path relative to
    # --lib-dir.
    #
    # --lib-dir names the lib/ directory itself (the one holding
    # text.inc, input.inc, ...), not its parent -- so a leading "lib/"
    # in the requested path (this project's own convention,
    # `.include "lib/text.inc"`) is stripped before joining with
    # --lib-dir, or `--lib-dir /shared/c64lib` would end up looking for
    # /shared/c64lib/lib/text.inc, one "lib" too many. A requested path
    # that doesn't start with "lib/" is joined as-is.
    lib_dir_display = None
    if (lib_dir and including_file and not os.path.isabs(requested_path)
            and not os.path.isfile(resolved_display)):
        lib_relative = requested_path
        if lib_relative.startswith('lib/'):
            lib_relative = lib_relative[len('lib/'):]
        lib_dir_display = os.path.join(lib_dir, lib_relative)
        if os.path.isfile(lib_dir_display):
            resolved_display = lib_dir_display

    return resolved_display, lib_dir_display


class IncludeProcessor:
    def __init__(self, on_line, lib_dir=None):
        self.on_line = on_line              # callback(raw_line, filename, line_no)
        self.open_stack_canon = []          # canonical paths, for cycle detection
        self.open_stack_display = []        # matching display names, for error messages
        self.already_included = set()       # canonical paths fully processed already
        self.lib_dir = lib_dir              # --lib-dir fallback search root, or None

    def process_file(self, requested_path, including_file, including_line_no, including_raw):
        resolved_display, lib_dir_display = resolve_asset_path(
            requested_path, including_file, self.lib_dir)

        try:
            canon = os.path.realpath(resolved_display)
            if not os.path.isfile(canon):
                raise OSError()
        except OSError:
            if including_file:
                if lib_dir_display and lib_dir_display != resolved_display:
                    raise AsmError(
                        f"Cannot open included file '{resolved_display}' "
                        f"(also tried '{lib_dir_display}' via --lib-dir)",
                        including_line_no, including_raw)
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
        self.repeat_capturing = None   # [count, index_name, body_lines] for
                                          # a '.repeat'/'.dup' block currently
                                          # being captured, or None -- see
                                          # process_line() and _expand_repeat()
        self.struct_capturing = None   # [struct_name, body_lines] for a
                                          # '.struct' block currently being
                                          # captured, or None -- see
                                          # process_line() and _expand_struct()
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
            if first_tok.upper() in ('.REPEAT', '.DUP'):
                raise AsmError(
                    "'.repeat'/'.dup' cannot appear inside a '.macro' body "
                    "-- define the macro first, then use '.repeat' to "
                    "invoke it, if that's what you need", line_no, raw_line)
            if first_tok.upper() == '.STRUCT':
                raise AsmError(
                    "'.struct' cannot appear inside a '.macro' body -- "
                    "define it before the '.macro' instead", line_no, raw_line)
            self.capturing.body.append(stripped)
            return

        if self.repeat_capturing is not None:
            first_tok = trimmed.split(None, 1)[0] if trimmed else ''
            if first_tok.upper() in ('.ENDREPEAT', '.ENDDUP'):
                count, index_name, body = self.repeat_capturing
                self.repeat_capturing = None
                self._expand_repeat(count, index_name, body, filename, line_no)
                return
            if first_tok.upper() in ('.REPEAT', '.DUP'):
                raise AsmError(
                    "nested '.repeat'/'.dup' blocks are not supported",
                    line_no, raw_line)
            if first_tok.upper() == '.MACRO':
                raise AsmError(
                    "'.macro' cannot be defined inside a '.repeat'/'.dup' "
                    "block -- define it before the '.repeat' instead",
                    line_no, raw_line)
            if first_tok.upper() == '.STRUCT':
                raise AsmError(
                    "'.struct' cannot appear inside a '.repeat'/'.dup' "
                    "block -- define it before the '.repeat' instead",
                    line_no, raw_line)
            self.repeat_capturing[2].append(stripped)
            return

        if self.struct_capturing is not None:
            first_tok = trimmed.split(None, 1)[0] if trimmed else ''
            if first_tok.upper() == '.ENDSTRUCT':
                name, body = self.struct_capturing
                self.struct_capturing = None
                self._expand_struct(name, body, filename, line_no)
                return
            if first_tok.upper() == '.STRUCT':
                raise AsmError(
                    f"nested '.struct' definitions are not supported "
                    f"(already defining '{self.struct_capturing[0]}')",
                    line_no, raw_line)
            if first_tok.upper() in ('.MACRO', '.REPEAT', '.DUP'):
                raise AsmError(
                    f"'{first_tok}' cannot appear inside a '.struct' body "
                    f"-- only field declarations (.byte, .word, .res) are "
                    f"allowed there", line_no, raw_line)
            self.struct_capturing[1].append(stripped)
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

        if first_tok.upper() in ('.REPEAT', '.DUP'):
            rest = trimmed[len(first_tok):].strip()
            parts = [p.strip() for p in split_args(rest)] if rest else []
            if not parts:
                raise AsmError(
                    "'.repeat'/'.dup' requires a count, e.g. '.repeat 16' "
                    "or '.repeat 16, i'", line_no, raw_line)
            if len(parts) > 2:
                raise AsmError(
                    "'.repeat'/'.dup' takes at most a count and an index "
                    "name (e.g. '.repeat 16, i'), got too many arguments",
                    line_no, raw_line)
            count = self._parse_repeat_count(parts[0], line_no, raw_line)
            index_name = parts[1] if len(parts) > 1 else None
            self.repeat_capturing = [count, index_name, []]
            return

        if first_tok.upper() in ('.ENDREPEAT', '.ENDDUP'):
            raise AsmError("'.endrepeat'/'.enddup' with no matching '.repeat'/'.dup'",
                            line_no, raw_line)

        if first_tok.upper() == '.STRUCT':
            rest = trimmed[len(first_tok):].strip()
            parts = rest.split(None, 1) if rest else []
            if not parts:
                raise AsmError("'.struct' requires a name, e.g. '.struct Room'",
                                line_no, raw_line)
            name = parts[0]
            if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', name):
                raise AsmError(f"'{name}' is not a valid struct name", line_no, raw_line)
            if len(parts) > 1:
                raise AsmError(
                    "'.struct' takes only a name -- field declarations go "
                    "on their own lines, between '.struct' and '.endstruct'",
                    line_no, raw_line)
            self.struct_capturing = [name, []]
            return

        if first_tok.upper() == '.ENDSTRUCT':
            raise AsmError("'.endstruct' with no matching '.struct'", line_no, raw_line)

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

    def _expand_repeat(self, count, index_name, body, filename, line_no):
        """Expands a captured '.repeat'/'.dup' block's body `count`
        times, in order -- essentially an anonymous macro with zero or
        one parameter (index_name), immediately invoked that many times
        with the loop index (0, 1, 2, ...) as the argument, reusing the
        exact same \\param substitution (_substitute) and per-invocation
        local-label scoping every ordinary macro invocation already
        gets (see the module comment above MacroDef). line_no here is
        the '.endrepeat'/'.enddup' line's own number, used for any
        error raised by the expansion itself (a too-deep nesting from a
        macro invoked inside the body, say) -- individual body lines
        keep whatever line_no they were originally captured with,
        exactly like a macro body's lines do."""
        self.expansion_depth += 1
        if self.expansion_depth > MAX_MACRO_EXPANSION_DEPTH:
            raise AsmError(
                "'.repeat'/'.dup' nested too deep (via a macro invocation "
                "inside its own body?)", line_no)
        for i in range(count):
            self.scope_stack.append(self.current_scope)
            self.current_scope = self.next_scope
            self.next_scope += 1

            for body_line in body:
                if index_name:
                    substituted = self._substitute(body_line, [index_name], [str(i)], line_no)
                else:
                    substituted = body_line
                self.process_line(substituted, filename, line_no)

            self.current_scope = self.scope_stack.pop()
        self.expansion_depth -= 1

    @staticmethod
    def _parse_repeat_count(text, line_no, raw_line):
        """Parses '.repeat'/'.dup's count argument -- a single plain
        integer literal (decimal, $hex, or %binary), deliberately NOT a
        full expression: this runs during macro/include preprocessing,
        entirely before pass 1 even starts building a symbol table, so
        there's no way to look up a label or forward-declared constant
        here even if the syntax allowed writing one."""
        t = text.strip()
        try:
            if t.startswith('$'):
                n = int(t[1:], 16)
            elif t.startswith('%'):
                n = int(t[1:], 2)
            else:
                n = int(t, 10)
        except ValueError:
            raise AsmError(
                f"'.repeat'/'.dup' count must be a plain integer literal "
                f"(decimal, $hex, or %binary) -- symbols and expressions "
                f"aren't available yet at this point in assembly, got '{text}'",
                line_no, raw_line)
        if n < 0:
            raise AsmError(f"'.repeat'/'.dup' count must not be negative (got {n})",
                            line_no, raw_line)
        if n > MAX_REPEAT_COUNT:
            raise AsmError(
                f"'.repeat'/'.dup' count {n} exceeds the maximum ({MAX_REPEAT_COUNT})",
                line_no, raw_line)
        return n

    def _expand_struct(self, name, body, filename, line_no):
        """Expands a captured '.struct' block's body into a set of
        Name.field = offset symbol-assignment lines, one per declared
        field, plus a final Name.size giving the whole struct's total
        byte width -- fed back through process_line() as ordinary
        '=' assignment lines (see _emit_struct_field()), the same way
        _expand_repeat() feeds its own generated lines back through.
        Nothing here emits any actual bytes or advances the assembled
        program's own address -- a '.struct' block is purely a
        compile-time source of named offsets, the same as a plain
        '=' constant is.

        line_no here is the '.endstruct' line's own number, used for
        errors that aren't tied to one specific field declaration (an
        unrecognized directive inside the block, say); a malformed
        individual field declaration's error instead points at that
        field's own line, using the body line's original text."""
        offset = 0
        for body_line in body:
            stripped = body_line.strip()
            if not stripped:
                continue
            parts = stripped.split(None, 1)
            directive = parts[0].lower()
            rest = parts[1].strip() if len(parts) > 1 else ''

            if directive in ('.byte', '.db', '.word', '.dw'):
                field_size = 1 if directive in ('.byte', '.db') else 2
                field_names = [p.strip() for p in split_args(rest)] if rest else []
                if not field_names:
                    raise AsmError(
                        f"'.struct {name}': '{parts[0]}' requires at least "
                        f"one field name", line_no, stripped)
                for fname in field_names:
                    self._emit_struct_field(name, fname, offset, filename, line_no, stripped)
                    offset += field_size
            elif directive in ('.res', '.ds', '.fill'):
                args = [p.strip() for p in split_args(rest)] if rest else []
                if len(args) != 2:
                    raise AsmError(
                        f"'.struct {name}': '{parts[0]}' requires exactly a "
                        f"field name and a byte count, e.g. '.res buf, 16'",
                        line_no, stripped)
                fname, count_str = args
                count = self._parse_repeat_count(count_str, line_no, stripped)
                # _parse_repeat_count's own restriction to a plain integer
                # literal (no symbols/expressions) applies here too, and
                # for the same reason: struct field offsets, like a
                # .repeat count, need to be known during this same
                # preprocessing pass, before pass 1 builds a symbol table.
                self._emit_struct_field(name, fname, offset, filename, line_no, stripped)
                offset += count
            else:
                raise AsmError(
                    f"'.struct {name}': '{parts[0]}' is not a valid field "
                    f"declaration -- expected .byte, .word, or .res",
                    line_no, stripped)

        self._emit_struct_field(name, 'size', offset, filename, line_no, None)

    def _emit_struct_field(self, struct_name, field_name, offset, filename, line_no, raw_line):
        if not re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', field_name):
            raise AsmError(f"'.struct {struct_name}': '{field_name}' is not "
                            f"a valid field name", line_no, raw_line)
        synthetic = f"{struct_name}.{field_name} = {offset}"
        self.process_line(synthetic, filename, line_no)

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

    def load(self, main_path, lib_dir=None):
        self.lib_dir = lib_dir   # needed again in _pass(), for '.incbin's
                                    # own path resolution -- see
                                    # resolve_asset_path()
        def emit(raw, filename, line_no):
            label, op, operand = split_line(raw, line_no)
            self.lines.append((line_no, raw, label, op, operand, filename))

        macros = MacroProcessor(emit)
        includes = IncludeProcessor(macros.process_line, lib_dir=lib_dir)
        macros.include_processor = includes   # see MacroProcessor.__init__
        includes.process_file(main_path, None, 0, None)

    def assemble(self):
        self._pass(pass_no=1)
        if any_errors_recorded():
            # Pass 1 already found at least one real problem -- don't
            # even attempt pass 2. Pass 2 depends on pass 1 having
            # produced a complete, trustworthy symbol table and a
            # consistent set of addresses; running it anyway on top of
            # a known-broken pass 1 would likely just flood the output
            # with secondary "undefined symbol" noise stemming from the
            # original mistakes, not independent problems worth
            # separately reporting.
            print_all_collected_errors_and_exit()
        result = self._pass(pass_no=2)
        if any_errors_recorded():
            # Pass 2 found problems pass 1 couldn't see (an addressing
            # mode an opcode doesn't support, a branch out of range,
            # and so on). No .prg or listing gets written by the caller
            # -- there is nothing correct to write once any error was
            # recorded.
            print_all_collected_errors_and_exit()
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
        illegal_enabled = False   # toggled by '.cpu 6510x'/'.cpu 6510' --
                                     # see that directive's handling below
        charset_lower = False     # toggled by '.charset lower'/'.charset
                                     # upper' -- see that directive's
                                     # handling below

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
                if operand:
                    # An explicit start label: emit `jmp <label>` right
                    # after the stub, so SYS always lands at the real
                    # entry point even if code-emitting .include lines
                    # (a library's own routines, say) sit between here
                    # and the label -- forgetting this by hand was a
                    # recurring, hard-to-spot bug (SYS silently landing
                    # inside the first included routine instead).
                    jmp_addr = pc
                    val, undef = eval_expr(operand, self.symbols, pc, line_no)
                    pc += 3  # always exactly 3 bytes for this jmp, moved up
                               # so it still happens even if the undefined-
                               # symbol check just below is a recoverable
                               # error -- otherwise every line after this
                               # one would be 3 bytes off for the rest of
                               # the file
                    if undef and pass_no == 2:
                        record_error(
                            f"Undefined symbol in .basic start operand '{operand}'",
                            line_no, raw)
                    if pass_no == 2:
                        output.append(0x4C)
                        output.append(val & 0xFF)
                        output.append((val >> 8) & 0xFF)
                        self.listing.append((jmp_addr, raw, output[-3:]))
                continue

            if op == '.cpu':
                # Switches illegal/undocumented-opcode support on or off
                # from this point in the file forward (not retroactively
                # -- a mnemonic used above this line is checked against
                # whatever '.cpu' setting was active there, not this
                # one). See ILLEGAL_SLOTS above and c64asm-reference.md's
                # "Illegal opcodes" section for the full explanation.
                mode_name = operand.strip().upper()
                if mode_name == '6510X':
                    illegal_enabled = True
                elif mode_name in ('6510', '6502'):
                    illegal_enabled = False
                else:
                    record_error(
                        f"Unknown .cpu mode '{operand.strip()}' -- expected "
                        f"'6510' (standard, the default) or '6510x' "
                        f"(enables illegal/undocumented opcodes)", line_no, raw)
                    # fallback: leave illegal_enabled exactly as it was --
                    # an unrecognized mode isn't a request to change
                    # anything, just a mistake to report
                continue

            if op == '.error' or op == '.warning':
                # A source-author-placed diagnostic -- typically paired
                # with .ifdef/.ifndef (§11) to check a precondition (a
                # required zero-page symbol defined, say) and fail with
                # a clear, specific message right at the point of the
                # mistake, instead of a confusing "Undefined symbol"
                # buried inside a macro expansion three files away:
                #
                #       .ifndef gfx_ptr
                #       .error "graphics.inc requires gfx_ptr (2-byte zero page)"
                #       .endif
                #
                # '.error' is recoverable, not fatal -- like any other
                # entry in ILLEGAL_SLOTS-style checks, several
                # independent '.error's (e.g. two different missing
                # zero-page symbols in two different included files)
                # can all be collected and reported together in one
                # run. '.warning' never stops assembly or affects the
                # exit status at all; it's only gated to pass 2 here so
                # its message prints exactly once, not twice (once per
                # pass) the way a fatal/recoverable error's own
                # pass-independence doesn't need to worry about.
                msg_text = operand.strip()
                if len(msg_text) >= 2 and msg_text[0] == '"' and msg_text[-1] == '"':
                    msg_text = msg_text[1:-1]
                else:
                    record_error(
                        f"{op} requires a quoted message string, e.g. "
                        f'{op} "message"', line_no, raw)
                    # fallback: nothing else to do with a malformed
                    # directive -- there's no message to act on
                    continue
                if op == '.error':
                    record_error(msg_text, line_no, raw)
                elif pass_no == 2:
                    record_warning(msg_text, line_no, raw)
                continue

            if op == '.assert':
                # Fails assembly (recoverably -- see '.error' just
                # above) if `condition` evaluates to 0, e.g. to catch a
                # struct (§10) changing shape out from under code that
                # assumed a specific size:
                #
                #       .assert Exits.size == 4, "compute_room_exits_offset assumes 4 fields"
                #
                # The message is optional; without one, the condition's
                # own source text stands in for it. Only checked on
                # pass 2 -- during pass 1, a symbol the condition
                # depends on may still be an unresolved forward
                # reference, standing in as 0 (§4), which would make an
                # otherwise-true condition look spuriously false.
                args = split_args(operand)
                if not args:
                    record_error(
                        ".assert requires a condition, e.g. "
                        ".assert Exits.size == 4", line_no, raw)
                    continue
                if len(args) > 2:
                    record_error(
                        ".assert takes at most a condition and a quoted "
                        "message", line_no, raw)
                    continue
                cond_text = args[0]
                msg_text = None
                if len(args) > 1:
                    msg_arg = args[1].strip()
                    if len(msg_arg) >= 2 and msg_arg[0] == '"' and msg_arg[-1] == '"':
                        msg_text = msg_arg[1:-1]
                    else:
                        record_error(
                            ".assert's message must be a quoted string, "
                            'e.g. .assert Exits.size == 4, "message"',
                            line_no, raw)
                        continue
                val, undef = eval_expr(cond_text, self.symbols, pc, line_no)
                if undef and pass_no == 2:
                    record_error(f"Undefined symbol in .assert condition '{cond_text}'",
                                 line_no, raw)
                    continue
                if pass_no == 2 and val == 0:
                    record_error(msg_text if msg_text else f"Assertion failed: {cond_text}",
                                 line_no, raw)
                continue

            if op == '.charset':
                # Switches how .text/.asc/.byte string literals encode
                # letters, from this point in the file forward (not
                # retroactively -- same positional behavior as '.cpu'
                # above). See ascii_to_petscii() above and
                # c64asm-reference.md's "Text and PETSCII" section for
                # the full explanation, including the important caveat
                # about mixing '.charset upper' and '.charset lower'
                # text in a program that switches its character set at
                # runtime.
                mode_name = operand.strip().upper()
                if mode_name == 'UPPER':
                    charset_lower = False
                elif mode_name == 'LOWER':
                    charset_lower = True
                else:
                    record_error(
                        f"Unknown .charset mode '{operand.strip()}' -- expected "
                        f"'upper' (the default) or 'lower'", line_no, raw)
                    # fallback: leave charset_lower exactly as it was --
                    # same reasoning as '.cpu''s fallback above
                continue

            if op == '.org':
                val, undef = eval_expr(operand, self.symbols, pc, line_no)
                if undef and pass_no == 2:
                    record_error("Undefined symbol in .org expression", line_no, raw)
                    # fallback: leave pc wherever it already was -- an
                    # unknown target isn't something to guess at, and
                    # this is at least deterministic for whatever comes
                    # next
                elif origin is None:
                    origin = val
                    pc = val
                elif pass_no == 2:
                    current_abs = origin + len(output)
                    gap = val - current_abs
                    if gap < 0:
                        record_error(
                            f".org cannot move the program counter backward "
                            f"(from ${current_abs:04X} to ${val:04X}) -- the "
                            f"assembler can't overwrite bytes already assembled",
                            line_no, raw)
                        # fallback: same as the undefined-symbol case
                        # above -- don't move pc to an invalid target
                    else:
                        if gap > 0:
                            output.extend(b'\x00' * gap)
                        pc = val
                else:
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
                    record_error("Undefined symbol in .align expression", line_no, raw)
                if undef:
                    # Forward-referenced alignment value, pass 1 only
                    # (pass 2 would already have raised above). n is
                    # just eval_expr's undefined-symbol placeholder (0)
                    # here, not a real value -- validating its sign or
                    # dividing by it would be meaningless (and, for 0
                    # specifically, a division by zero), so pc simply
                    # doesn't advance this pass. That never produces
                    # incorrect output in a clean assembly: pass 2
                    # always catches the undefined symbol and (so long
                    # as pass 1 came back completely clean, a
                    # precondition for even attempting pass 2 -- see
                    # assemble()) aborts before anything computed from a
                    # wrong pass-1 address could ship.
                    target = pc
                elif n <= 0:
                    record_error(
                        f".align requires a positive alignment value (got {n})",
                        line_no, raw)
                    target = pc  # same "don't move" fallback as above --
                                   # and necessary here, not just
                                   # consistent: n<=0 below would divide
                                   # by zero (n==0, an uncaught Python
                                   # ZeroDivisionError) or produce a
                                   # nonsensical negative gap (n<0)
                else:
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
                        b = ascii_to_petscii(s, charset_lower)
                        if pass_no == 2:
                            output.extend(b)
                        pc += len(b)
                    else:
                        val, undef = eval_expr(a, self.symbols, pc, line_no)
                        if undef and pass_no == 2:
                            record_error(f"Undefined symbol in .byte '{a}'", line_no, raw)
                        if pass_no == 2:
                            output.append(val & 0xFF)
                        pc += 1
                continue

            if op in ('.word', '.dw'):
                for a in split_args(operand):
                    val, undef = eval_expr(a, self.symbols, pc, line_no)
                    if undef and pass_no == 2:
                        record_error(f"Undefined symbol in .word '{a}'", line_no, raw)
                    if pass_no == 2:
                        output.append(val & 0xFF)
                        output.append((val >> 8) & 0xFF)
                    pc += 2
                continue

            if op in ('.text', '.asc'):
                for a in split_args(operand):
                    if a.startswith('"'):
                        s = a[1:-1] if a.endswith('"') else a[1:]
                        b = ascii_to_petscii(s, charset_lower)
                        if pass_no == 2:
                            output.extend(b)
                        pc += len(b)
                    else:
                        val, undef = eval_expr(a, self.symbols, pc, line_no)
                        if undef and pass_no == 2:
                            record_error(f"Undefined symbol in .text '{a}'", line_no, raw)
                        if pass_no == 2:
                            output.append(val & 0xFF)
                        pc += 1
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

            if op == '.incbin':
                # Unlike .byte/.text's undefined-symbol errors, every
                # error path below is fatal, not recoverable -- an
                # .incbin problem means the assembler doesn't know how
                # many bytes this line emits, which (unlike an
                # ordinary .byte's *value* being wrong) would throw off
                # every address computed after it, the same class of
                # problem a missing .include'd file is (§11).
                args = split_args(operand)
                if not args or len(args[0].strip()) < 2 or \
                        args[0].strip()[0] != '"' or args[0].strip()[-1] != '"':
                    raise AsmError(
                        '.incbin requires a quoted path, e.g. .incbin "sprite.bin"',
                        line_no, raw)
                path = args[0].strip()[1:-1]
                if len(args) > 3:
                    raise AsmError(
                        ".incbin takes at most a path, an offset, and a length",
                        line_no, raw)

                offset = 0
                length = None
                if len(args) > 1:
                    offset, off_undef = eval_expr(args[1], self.symbols, pc, line_no)
                    if off_undef:
                        raise AsmError("Undefined symbol in .incbin offset expression",
                                        line_no, raw)
                if len(args) > 2:
                    length, len_undef = eval_expr(args[2], self.symbols, pc, line_no)
                    if len_undef:
                        raise AsmError("Undefined symbol in .incbin length expression",
                                        line_no, raw)

                resolved_display, lib_dir_display = resolve_asset_path(
                    path, filename, self.lib_dir)
                try:
                    with open(resolved_display, 'rb') as f:
                        data = f.read()
                except OSError:
                    if lib_dir_display and lib_dir_display != resolved_display:
                        raise AsmError(
                            f"Cannot open included binary file '{resolved_display}' "
                            f"(also tried '{lib_dir_display}' via --lib-dir)",
                            line_no, raw)
                    raise AsmError(
                        f"Cannot open included binary file '{resolved_display}'",
                        line_no, raw)

                if offset < 0 or offset > len(data):
                    raise AsmError(
                        f".incbin offset {offset} is out of range for "
                        f"'{resolved_display}' ({len(data)} bytes)", line_no, raw)
                if length is None:
                    length = len(data) - offset
                if length < 0 or offset + length > len(data):
                    raise AsmError(
                        f".incbin length {length} (from offset {offset}) exceeds "
                        f"the size of '{resolved_display}' ({len(data)} bytes)", line_no, raw)

                chunk = data[offset:offset + length]
                if pass_no == 2:
                    output.extend(chunk)
                pc += length
                continue

            # Real instruction
            mnemonic = op
            mode, expr_text, undef, val = parse_operand(mnemonic, operand,
                                                          self.symbols, pc, line_no)
            size = MODE_SIZE[mode]

            if pass_no == 2:
                mode_ok = mnemonic in OPCODES and mode in OPCODES[mnemonic]
                illegal_slot = mode_ok and (mnemonic, mode) in ILLEGAL_SLOTS
                illegal_blocked = illegal_slot and not illegal_enabled
                opcode = OPCODES[mnemonic][mode] if (mode_ok and not illegal_blocked) else 0x00  # BRK's
                    # opcode -- an arbitrary but harmless placeholder;
                    # never written to the .prg, since a mode_ok failure
                    # (or an illegal opcode used without '.cpu 6510x')
                    # here always means at least one error was recorded,
                    # and this build never writes output once any error
                    # exists (see main())
                if not mode_ok:
                    record_error(f"Invalid addressing mode for {mnemonic}", line_no, raw)
                elif illegal_blocked:
                    record_error(
                        f"Illegal/undocumented opcode '{mnemonic}' used without "
                        f"'.cpu 6510x' -- see c64asm-reference.md's \"Illegal "
                        f"opcodes\" section", line_no, raw)
                if undef:
                    record_error(f"Undefined symbol in operand '{operand}'", line_no, raw)

                if mode == 'rel':
                    target = val
                    offset = target - (entry_pc + 2)
                    if offset < -128 or offset > 127:
                        record_error(
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
                record_error(f"Symbol '{name}' already defined", line_no, raw)
                # fallback: fall through to the assignment below anyway --
                # last definition wins, deterministic and safe, even
                # though it's now flagged as a mistake
            self.symbols[name] = value
        else:
            self.symbols[name] = value


def assemble_source(main_path, lib_dir=None):
    asm = Assembler()
    reset_collected_errors()   # must happen before load() -- split_line()
                                 # (called from load(), via emit()) can
                                 # itself record recoverable errors (e.g.
                                 # "Unknown mnemonic or directive"), and
                                 # those need to survive into assemble()'s
                                 # pass-1/pass-2 checks rather than being
                                 # wiped out by a reset that runs after
                                 # load() already recorded them
    asm.load(main_path, lib_dir=lib_dir)
    origin, code = asm.assemble()
    return origin, code, asm.listing, asm.symbols, asm


def report_unused_symbols(asm, main_path, include_library=False):
    """Prints a warning for every symbol defined but never referenced
    anywhere in the program -- see --warn-unused. Opt-in, not
    automatic: a typical program that only uses part of an .include'd
    library will have plenty of genuinely-fine unused library-internal
    symbols (a constant defined for a routine the program never calls,
    say), which would otherwise bury any warning actually worth
    looking at under a pile of expected noise.

    Scoped to the main file by default, for the same reason: an
    .include'd file's own unused symbols are overwhelmingly library
    noise rather than anything about *this* program, so they're
    suppressed (with a one-line count of how many, so nothing goes
    missing silently) unless include_library is set -- see
    --warn-unused-all.

    "Used" means looked up by name from within an expression -- see
    ExprParser.parse_atom's TK_IDENT case, which is the only place
    _used_symbols is added to. A symbol only referenced from inside a
    permanently-false '.if' branch (dead code that pass 2 never
    actually evaluates) counts as unused here, the same as a real
    compiler would treat code excluded by an '#ifdef'."""
    unused = sorted(set(asm.symbols) - _used_symbols)
    suppressed_count = 0
    for name in unused:
        li = asm.symbol_first_li.get(name)
        filename = asm.lines[li][5] if (li is not None and li < len(asm.lines)) else None
        is_local = filename is None or filename == main_path
        if not is_local and not include_library:
            suppressed_count += 1
            continue
        if li is not None and li < len(asm.lines):
            line_no, raw, _label, _op, _operand, filename = asm.lines[li]
            _set_error_file(filename)
            record_warning(f"Unused symbol '{name}' (never referenced)", line_no, raw)
        else:
            # Shouldn't normally happen (every symbol in asm.symbols
            # should have a matching symbol_first_li entry from
            # define_symbol()), but degrade to a plain message with no
            # location rather than crash the whole report over it.
            record_warning(f"Unused symbol '{name}' (never referenced)")

    if suppressed_count and not include_library:
        plural = "" if suppressed_count == 1 else "s"
        print(f"({suppressed_count} more unused symbol{plural} in "
              f".include'd files not shown -- use --warn-unused-all to see them)",
              file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Two-pass 6502/6510 assembler for the Commodore 64.")
    parser.add_argument('input', help="Input assembly source file")
    parser.add_argument('-o', '--output', required=True, help="Output .prg file")
    parser.add_argument('--listing', help="Optional assembly listing output file")
    parser.add_argument('--vice-labels', metavar='FILE',
                         help="Optional VICE monitor label file (add_label "
                              "commands for every symbol, same content as "
                              "--listing's own symbol table) -- load it in "
                              "the VICE monitor with 'll \"FILE\"' (or "
                              "load_labels) to debug by name: 'break "
                              ".main_loop' instead of 'break $0a60'. See "
                              "c64asm-reference.md's \"VICE label export\" "
                              "section.")
    parser.add_argument('--lib-dir', metavar='DIR',
                         help="Fallback directory to also search when resolving "
                              ".include paths that aren't found relative to the "
                              "including file (the default, unaffected if this "
                              "isn't given). Lets a common library directory be "
                              "shared across separate project directories instead "
                              "of each needing its own copy.")
    parser.add_argument('--warn-unused', action='store_true',
                         help="After assembling, warn about every symbol "
                              "(label or constant) defined but never "
                              "referenced anywhere in the program, scoped "
                              "to the main file (an .include'd file's own "
                              "unused symbols are suppressed by default -- "
                              "see --warn-unused-all). Off by default. "
                              "Never fails the build -- see "
                              "c64asm-reference.md's \"Unused-symbol "
                              "warnings\" section.")
    parser.add_argument('--warn-unused-all', action='store_true',
                         help="Like --warn-unused (and implies it), but "
                              "without the main-file scoping -- also warns "
                              "about unused symbols defined in .include'd "
                              "files. Expect a lot of library-internal "
                              "noise unless the program uses nearly "
                              "everything a library it includes defines.")
    args = parser.parse_args()

    # File reading (of the main file, and of anything it .include's) now
    # happens inside Assembler.load() -> IncludeProcessor, which already
    # has a clean "Cannot open input file" message for exactly this case
    # -- so there's no separate open() here to fail with a raw traceback
    # if args.input doesn't exist.
    try:
        origin, code, listing, symbols, asm = assemble_source(args.input, lib_dir=args.lib_dir)
    except AsmError as e:
        print(f"Assembly error: {e}", file=sys.stderr)
        sys.exit(1)

    with open(args.output, 'wb') as f:
        f.write(bytes([origin & 0xFF, (origin >> 8) & 0xFF]))
        f.write(code)

    print(f"Assembled {len(code)} bytes, origin=${origin:04X} -> {args.output}")

    if args.warn_unused or args.warn_unused_all:
        report_unused_symbols(asm, args.input, include_library=args.warn_unused_all)

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

    if args.vice_labels:
        with open(args.vice_labels, 'w') as f:
            f.write(f"; c64asm VICE label export  (origin ${origin:04X}, "
                     f"{len(code)} bytes)\n")
            f.write("; load in the VICE monitor with: ll \"" +
                     args.vice_labels + "\"\n")
            for name in sorted(symbols):
                f.write(f"add_label ${symbols[name]:04X} .{name}\n")
        print(f"VICE labels written to {args.vice_labels}")


if __name__ == '__main__':
    main()
