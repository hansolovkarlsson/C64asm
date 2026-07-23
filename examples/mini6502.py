"""
mini6502.py - a small 6502/6510 CPU emulator with C64-specific memory
and hardware behavior, purpose-built for this project.

This is NOT a general-purpose, cycle-accurate C64 emulator (no VIC-II
pixel rendering, no real KERNAL ROM, no accurate timing). It exists for
one job: actually *running* the .prg files this project's assembler
produces, so their behavior can be checked against real assertions
(register/memory/flag state, captured text output) instead of only
against a hand-read listing. Where this project has already discovered
a real hardware quirk the hard way -- through VICE or actual C64
testing -- that quirk is modeled here too, specifically so the same
class of bug gets caught by emulation next time, not just by careful
reading:

  - CIA1 PRA/PRB (joystick port 2 and the keyboard matrix) are
    ACTIVE-LOW: a pressed key or held direction reads as a 0 bit, not a
    1. Every one of this project's own demos' input-reading code
    depends on getting this backwards-from-intuition polarity right.
  - KERNAL zero-page poisoning: certain zero-page locations
    ($F3-$F6) get silently overwritten by the real KERNAL's own
    interrupt-driven housekeeping. A program that picks a "scratch"
    zero-page address without knowing this can have it corrupted
    out from under it on real hardware -- which is exactly what
    happened during this project's own zero-page range selection
    (see c64-memory-reference.md's zero-page notes). Simulated here by
    periodically overwriting those bytes during emulation, so code that
    collides with them shows visibly wrong behavior instead of quietly
    working in emulation and failing later on real hardware.
  - JOY2 and the keyboard column-select register are the same
    physical address ($DC00) -- modeled naturally here, for free, since
    both are just reads/writes of the same underlying byte; the
    machine doesn't try to "guess" which one a program meant.

Deliberately NOT modeled: decimal (BCD) mode for ADC/SBC (unused by
this project's code -- SED is never emitted anywhere), real VIC-II
video timing or pixel output, and the actual KERNAL ROM (CHROUT/CHRIN
are trapped and handled at a behavioral level -- see C64Machine -- not
executed as real 6502 code).
"""

# ---------------------------------------------------------------------------
# CPU core
# ---------------------------------------------------------------------------

FLAG_N = 0x80
FLAG_V = 0x40
FLAG_UNUSED = 0x20   # always reads as 1 on real hardware; not otherwise meaningful
FLAG_B = 0x10
FLAG_D = 0x08
FLAG_I = 0x04
FLAG_Z = 0x02
FLAG_C = 0x01


class CPUError(Exception):
    """Raised for anything the emulator can't/won't do: an unimplemented
    or illegal opcode, a stack overflow/underflow, etc. Deliberately
    fatal rather than silently guessing -- a program that hits one of
    these needs the bug fixed, not the emulator papering over it."""
    pass


class CPU6502:
    """A 6502/6510 CPU core: registers, flags, addressing modes, and the
    full official (documented) instruction set -- every mnemonic and
    addressing mode this project's own assembler (c64asm) can produce,
    since this emulator's whole job is running that assembler's output.
    No undocumented/illegal opcodes -- c64asm doesn't generate them
    either (see c64asm-reference.md's known limitations).

    Memory access goes through self.read_byte()/self.write_byte()
    rather than touching self.memory directly everywhere, specifically
    so a subclass (see C64Machine below) can intercept reads/writes to
    specific addresses -- memory-mapped I/O, KERNAL call traps, and the
    zero-page poisoning simulation all hook in through those two
    methods alone.
    """

    def __init__(self):
        self.a = 0
        self.x = 0
        self.y = 0
        self.sp = 0xFF
        self.pc = 0x0000
        self.p = FLAG_UNUSED | FLAG_I   # status register; IRQs start masked,
                                          # matching real power-on/reset state
        self.memory = bytearray(65536)
        self.cycles = 0          # running total, for diagnostics only --
                                   # not used to gate timing anywhere
        self.instructions_run = 0
        self.halted = False
        self.halt_reason = None

    # --- flag helpers -----------------------------------------------

    def get_flag(self, mask):
        return 1 if (self.p & mask) else 0

    def set_flag(self, mask, value):
        if value:
            self.p |= mask
        else:
            self.p &= ~mask & 0xFF

    def set_nz(self, value):
        value &= 0xFF
        self.set_flag(FLAG_Z, value == 0)
        self.set_flag(FLAG_N, value & 0x80)

    # --- memory --------------------------------------------------------

    def read_byte(self, addr):
        return self.memory[addr & 0xFFFF]

    def write_byte(self, addr, value):
        self.memory[addr & 0xFFFF] = value & 0xFF

    def read_word(self, addr):
        lo = self.read_byte(addr)
        hi = self.read_byte(addr + 1)
        return lo | (hi << 8)

    def read_word_zp_wrap(self, addr):
        """Like read_word(), but wraps within zero page for the second
        byte -- the real 6502's zero-page indexed-indirect and
        indirect-indexed addressing modes never carry out of zero page
        even when the index would otherwise push them past $FF."""
        lo = self.read_byte(addr & 0xFF)
        hi = self.read_byte((addr + 1) & 0xFF)
        return lo | (hi << 8)

    # --- stack -----------------------------------------------------------
    # The 6502 stack always lives at $0100-$01FF; sp is just the low byte
    # of the address, and it counts DOWN as things are pushed.

    def push_byte(self, value):
        self.write_byte(0x0100 + self.sp, value)
        self.sp = (self.sp - 1) & 0xFF

    def pop_byte(self):
        self.sp = (self.sp + 1) & 0xFF
        return self.read_byte(0x0100 + self.sp)

    def push_word(self, value):
        self.push_byte((value >> 8) & 0xFF)
        self.push_byte(value & 0xFF)

    def pop_word(self):
        lo = self.pop_byte()
        hi = self.pop_byte()
        return lo | (hi << 8)

    # --- fetch helpers, used only during instruction decode ---------------

    def _fetch_byte(self):
        b = self.read_byte(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return b

    def _fetch_word(self):
        w = self.read_word(self.pc)
        self.pc = (self.pc + 2) & 0xFFFF
        return w

    def _signed(self, b):
        return b - 256 if b >= 128 else b

    # --- addressing modes --------------------------------------------
    # Each resolver consumes whatever operand bytes the mode needs from
    # the instruction stream (via _fetch_byte/_fetch_word) and returns
    # an effective address for the instruction to read or write through
    # read_byte()/write_byte(). IMP and ACC modes have no address at all
    # (the instruction operates on registers directly), so those two
    # resolvers return None; every instruction handler that can appear
    # with IMP/ACC checks for that explicitly rather than dereferencing
    # a None address.

    def _mode_imp(self):
        return None

    def _mode_acc(self):
        return None

    def _mode_imm(self):
        addr = self.pc
        self.pc = (self.pc + 1) & 0xFFFF
        return addr

    def _mode_zp(self):
        return self._fetch_byte()

    def _mode_zpx(self):
        return (self._fetch_byte() + self.x) & 0xFF

    def _mode_zpy(self):
        return (self._fetch_byte() + self.y) & 0xFF

    def _mode_rel(self):
        # Returns the actual branch TARGET address (not a memory operand
        # to read) -- the offset is signed and relative to the address
        # of the instruction *following* this one, matching how
        # assembler.c computes it when encoding a branch.
        offset = self._signed(self._fetch_byte())
        return (self.pc + offset) & 0xFFFF

    def _mode_abs(self):
        return self._fetch_word()

    def _mode_absx(self):
        return (self._fetch_word() + self.x) & 0xFFFF

    def _mode_absy(self):
        return (self._fetch_word() + self.y) & 0xFFFF

    def _mode_ind(self):
        # JMP ($xxxx) only. Faithfully reproduces the real 6502's
        # well-known indirect-JMP page-boundary bug: if the low byte of
        # the pointer is $FF, the high byte of the target is fetched
        # from the START of the SAME page (wrapping), not from the
        # start of the next page as you'd naively expect. c64asm's own
        # opcode reference documents this as a real hardware gotcha to
        # avoid (never place a JMP-indirect pointer at an $xxFF
        # boundary); emulating the bug faithfully, rather than "fixing"
        # it, is what lets that exact mistake be caught here instead of
        # only on real hardware.
        ptr = self._fetch_word()
        lo = self.read_byte(ptr)
        if (ptr & 0xFF) == 0xFF:
            hi = self.read_byte(ptr & 0xFF00)
        else:
            hi = self.read_byte(ptr + 1)
        return lo | (hi << 8)

    def _mode_indx(self):
        zp = (self._fetch_byte() + self.x) & 0xFF
        return self.read_word_zp_wrap(zp)

    def _mode_indy(self):
        zp = self._fetch_byte()
        base = self.read_word_zp_wrap(zp)
        return (base + self.y) & 0xFFFF

    MODES = {
        'imp': _mode_imp, 'acc': _mode_acc, 'imm': _mode_imm,
        'zp': _mode_zp, 'zpx': _mode_zpx, 'zpy': _mode_zpy,
        'rel': _mode_rel, 'abs': _mode_abs, 'absx': _mode_absx,
        'absy': _mode_absy, 'ind': _mode_ind, 'indx': _mode_indx,
        'indy': _mode_indy,
    }

    # --- single-step execution -----------------------------------------

    def step(self):
        """Executes exactly one instruction. Raises CPUError for an
        opcode this emulator doesn't implement (there shouldn't be any,
        for code c64asm actually produces -- see OPCODES below)."""
        if self.halted:
            return
        opcode = self._fetch_byte()
        entry = OPCODES.get(opcode)
        if entry is None:
            raise CPUError(f"Unimplemented/illegal opcode ${opcode:02X} at "
                            f"${(self.pc - 1) & 0xFFFF:04X}")
        mnemonic, mode = entry
        addr = self.MODES[mode](self)
        handler = getattr(self, f"_op_{mnemonic.lower()}")
        handler(addr, mode)
        self.instructions_run += 1

    def run(self, max_instructions=2_000_000):
        """Runs until self.halted becomes true (see C64Machine for how
        that gets set -- typically on RTS back to a sentinel return
        address, or a JMP-to-self spin loop) or max_instructions is
        reached, whichever comes first. The instruction cap exists so a
        genuinely buggy or infinite-looping program under test fails
        the test with a clear "ran too long" error instead of hanging
        the whole test run forever."""
        while not self.halted and self.instructions_run < max_instructions:
            self.step()
        if not self.halted:
            self.halt_reason = f"exceeded {max_instructions} instructions without halting"
            self.halted = True

    # --- instruction handlers -------------------------------------------
    # Each takes (addr, mode) -- addr is whatever the addressing-mode
    # resolver returned (None for imp/acc), mode is the mode name string,
    # needed only by the handful of instructions (ASL/LSR/ROL/ROR) that
    # behave differently in accumulator mode vs. every other mode.

    # -- load/store --
    def _op_lda(self, addr, mode):
        self.a = self.read_byte(addr)
        self.set_nz(self.a)

    def _op_ldx(self, addr, mode):
        self.x = self.read_byte(addr)
        self.set_nz(self.x)

    def _op_ldy(self, addr, mode):
        self.y = self.read_byte(addr)
        self.set_nz(self.y)

    def _op_sta(self, addr, mode):
        self.write_byte(addr, self.a)

    def _op_stx(self, addr, mode):
        self.write_byte(addr, self.x)

    def _op_sty(self, addr, mode):
        self.write_byte(addr, self.y)

    # -- transfer --
    def _op_tax(self, addr, mode):
        self.x = self.a; self.set_nz(self.x)

    def _op_tay(self, addr, mode):
        self.y = self.a; self.set_nz(self.y)

    def _op_txa(self, addr, mode):
        self.a = self.x; self.set_nz(self.a)

    def _op_tya(self, addr, mode):
        self.a = self.y; self.set_nz(self.a)

    def _op_tsx(self, addr, mode):
        self.x = self.sp; self.set_nz(self.x)

    def _op_txs(self, addr, mode):
        self.sp = self.x   # TXS does NOT affect flags

    # -- arithmetic --
    def _op_adc(self, addr, mode):
        # Binary mode only -- see the module docstring for why decimal
        # mode isn't implemented (unused anywhere in this project).
        m = self.read_byte(addr)
        c = self.get_flag(FLAG_C)
        result = self.a + m + c
        # Signed overflow: happens exactly when the two operands share a
        # sign and the result's sign differs from theirs -- the classic
        # "both positive but result negative" or "both negative but
        # result positive" cases.
        overflow = (~(self.a ^ m) & (self.a ^ result) & 0x80) != 0
        self.set_flag(FLAG_C, result > 0xFF)
        self.set_flag(FLAG_V, overflow)
        self.a = result & 0xFF
        self.set_nz(self.a)

    def _op_sbc(self, addr, mode):
        # SBC A,M,C is implemented as ADC A,~M,C -- the standard 6502
        # identity (subtraction is addition of the two's-complement,
        # and borrow is the inverse of carry).
        m = self.read_byte(addr) ^ 0xFF
        c = self.get_flag(FLAG_C)
        result = self.a + m + c
        overflow = (~(self.a ^ m) & (self.a ^ result) & 0x80) != 0
        self.set_flag(FLAG_C, result > 0xFF)
        self.set_flag(FLAG_V, overflow)
        self.a = result & 0xFF
        self.set_nz(self.a)

    def _op_inc(self, addr, mode):
        v = (self.read_byte(addr) + 1) & 0xFF
        self.write_byte(addr, v)
        self.set_nz(v)

    def _op_dec(self, addr, mode):
        v = (self.read_byte(addr) - 1) & 0xFF
        self.write_byte(addr, v)
        self.set_nz(v)

    def _op_inx(self, addr, mode):
        self.x = (self.x + 1) & 0xFF; self.set_nz(self.x)

    def _op_iny(self, addr, mode):
        self.y = (self.y + 1) & 0xFF; self.set_nz(self.y)

    def _op_dex(self, addr, mode):
        self.x = (self.x - 1) & 0xFF; self.set_nz(self.x)

    def _op_dey(self, addr, mode):
        self.y = (self.y - 1) & 0xFF; self.set_nz(self.y)

    # -- logic --
    def _op_and(self, addr, mode):
        self.a &= self.read_byte(addr); self.set_nz(self.a)

    def _op_ora(self, addr, mode):
        self.a |= self.read_byte(addr); self.set_nz(self.a)

    def _op_eor(self, addr, mode):
        self.a ^= self.read_byte(addr); self.set_nz(self.a)

    def _op_bit(self, addr, mode):
        m = self.read_byte(addr)
        self.set_flag(FLAG_Z, (self.a & m) == 0)
        # N and V come from bits 7 and 6 of the MEMORY operand directly
        # -- not from the AND result -- a genuinely easy detail to get
        # wrong, since every other flag-setting instruction here derives
        # N from its own result.
        self.set_flag(FLAG_N, m & 0x80)
        self.set_flag(FLAG_V, m & 0x40)

    # -- shifts/rotates --
    def _op_asl(self, addr, mode):
        v = self.a if mode == 'acc' else self.read_byte(addr)
        c = (v & 0x80) != 0
        v = (v << 1) & 0xFF
        self.set_flag(FLAG_C, c)
        self.set_nz(v)
        if mode == 'acc': self.a = v
        else: self.write_byte(addr, v)

    def _op_lsr(self, addr, mode):
        v = self.a if mode == 'acc' else self.read_byte(addr)
        c = (v & 0x01) != 0
        v = (v >> 1) & 0xFF
        self.set_flag(FLAG_C, c)
        self.set_nz(v)
        if mode == 'acc': self.a = v
        else: self.write_byte(addr, v)

    def _op_rol(self, addr, mode):
        v = self.a if mode == 'acc' else self.read_byte(addr)
        old_c = self.get_flag(FLAG_C)
        new_c = (v & 0x80) != 0
        v = ((v << 1) | old_c) & 0xFF
        self.set_flag(FLAG_C, new_c)
        self.set_nz(v)
        if mode == 'acc': self.a = v
        else: self.write_byte(addr, v)

    def _op_ror(self, addr, mode):
        v = self.a if mode == 'acc' else self.read_byte(addr)
        old_c = self.get_flag(FLAG_C)
        new_c = (v & 0x01) != 0
        v = ((v >> 1) | (old_c << 7)) & 0xFF
        self.set_flag(FLAG_C, new_c)
        self.set_nz(v)
        if mode == 'acc': self.a = v
        else: self.write_byte(addr, v)

    # -- compare --
    def _compare(self, reg, addr):
        m = self.read_byte(addr)
        result = (reg - m) & 0xFF
        self.set_flag(FLAG_C, reg >= m)
        self.set_nz(result)

    def _op_cmp(self, addr, mode): self._compare(self.a, addr)
    def _op_cpx(self, addr, mode): self._compare(self.x, addr)
    def _op_cpy(self, addr, mode): self._compare(self.y, addr)

    # -- branches --
    def _branch(self, addr, condition):
        if condition:
            self.pc = addr

    def _op_bcc(self, addr, mode): self._branch(addr, not self.get_flag(FLAG_C))
    def _op_bcs(self, addr, mode): self._branch(addr, self.get_flag(FLAG_C))
    def _op_beq(self, addr, mode): self._branch(addr, self.get_flag(FLAG_Z))
    def _op_bne(self, addr, mode): self._branch(addr, not self.get_flag(FLAG_Z))
    def _op_bmi(self, addr, mode): self._branch(addr, self.get_flag(FLAG_N))
    def _op_bpl(self, addr, mode): self._branch(addr, not self.get_flag(FLAG_N))
    def _op_bvc(self, addr, mode): self._branch(addr, not self.get_flag(FLAG_V))
    def _op_bvs(self, addr, mode): self._branch(addr, self.get_flag(FLAG_V))

    # -- jumps/calls --
    def _op_jmp(self, addr, mode):
        self.pc = addr

    def _op_jsr(self, addr, mode):
        # The 6502 pushes the address of the LAST byte of the JSR
        # instruction (not the next instruction's address) -- RTS then
        # adds 1 after popping it. By the time this handler runs, self.pc
        # has already been advanced past the full 3-byte JSR instruction
        # by the 'abs' addressing mode resolver, so pc-1 is that last byte.
        self.push_word((self.pc - 1) & 0xFFFF)
        self.pc = addr

    def _op_rts(self, addr, mode):
        self.pc = (self.pop_word() + 1) & 0xFFFF

    def _op_rti(self, addr, mode):
        self.p = self.pop_byte() | FLAG_UNUSED
        self.pc = self.pop_word()

    # -- stack --
    def _op_pha(self, addr, mode): self.push_byte(self.a)
    def _op_php(self, addr, mode): self.push_byte(self.p | FLAG_B | FLAG_UNUSED)
    def _op_pla(self, addr, mode):
        self.a = self.pop_byte(); self.set_nz(self.a)
    def _op_plp(self, addr, mode):
        self.p = self.pop_byte() | FLAG_UNUSED

    # -- flags --
    def _op_clc(self, addr, mode): self.set_flag(FLAG_C, False)
    def _op_sec(self, addr, mode): self.set_flag(FLAG_C, True)
    def _op_cli(self, addr, mode): self.set_flag(FLAG_I, False)
    def _op_sei(self, addr, mode): self.set_flag(FLAG_I, True)
    def _op_clv(self, addr, mode): self.set_flag(FLAG_V, False)
    def _op_cld(self, addr, mode): self.set_flag(FLAG_D, False)
    def _op_sed(self, addr, mode): self.set_flag(FLAG_D, True)

    # -- misc --
    def _op_nop(self, addr, mode):
        pass

    def _op_brk(self, addr, mode):
        # A real BRK pushes PC+2 (a padding byte after the opcode) and
        # jumps through the IRQ/BRK vector at $FFFE. There's no real
        # KERNAL ROM here for that vector to point at, and nothing in
        # this project's own code ever intentionally executes BRK, so
        # this is treated as a hard halt instead -- almost always
        # reached by accident (e.g. running off the end of code into
        # zeroed memory, since $00 is BRK's opcode), which is exactly
        # the situation a test should fail loudly on, not silently
        # continue past.
        self.halted = True
        self.halt_reason = f"BRK encountered at ${(self.pc - 1) & 0xFFFF:04X}"


# ---------------------------------------------------------------------------
# Opcode table: byte -> (mnemonic, addressing mode)
# ---------------------------------------------------------------------------
# A direct transcription of c64asm's own opcode table (opcodes.c's
# init_opcodes()) -- every (mnemonic, mode, byte) triple there has a
# matching entry here, so this emulator can decode anything that
# assembler produces. Deliberately not derived cleverly from any
# pattern in the bit layout, for the same reason c64asm's own table
# isn't: a plain transcription is easier to verify against a reference
# than something that tries to be clever about it.

OPCODES = {
    0x69: ('ADC', 'imm'), 0x65: ('ADC', 'zp'), 0x75: ('ADC', 'zpx'),
    0x6D: ('ADC', 'abs'), 0x7D: ('ADC', 'absx'), 0x79: ('ADC', 'absy'),
    0x61: ('ADC', 'indx'), 0x71: ('ADC', 'indy'),

    0x29: ('AND', 'imm'), 0x25: ('AND', 'zp'), 0x35: ('AND', 'zpx'),
    0x2D: ('AND', 'abs'), 0x3D: ('AND', 'absx'), 0x39: ('AND', 'absy'),
    0x21: ('AND', 'indx'), 0x31: ('AND', 'indy'),

    0x0A: ('ASL', 'acc'), 0x06: ('ASL', 'zp'), 0x16: ('ASL', 'zpx'),
    0x0E: ('ASL', 'abs'), 0x1E: ('ASL', 'absx'),

    0x90: ('BCC', 'rel'), 0xB0: ('BCS', 'rel'), 0xF0: ('BEQ', 'rel'),

    0x24: ('BIT', 'zp'), 0x2C: ('BIT', 'abs'),

    0x30: ('BMI', 'rel'), 0xD0: ('BNE', 'rel'), 0x10: ('BPL', 'rel'),

    0x00: ('BRK', 'imp'),

    0x50: ('BVC', 'rel'), 0x70: ('BVS', 'rel'),

    0x18: ('CLC', 'imp'), 0xD8: ('CLD', 'imp'),
    0x58: ('CLI', 'imp'), 0xB8: ('CLV', 'imp'),

    0xC9: ('CMP', 'imm'), 0xC5: ('CMP', 'zp'), 0xD5: ('CMP', 'zpx'),
    0xCD: ('CMP', 'abs'), 0xDD: ('CMP', 'absx'), 0xD9: ('CMP', 'absy'),
    0xC1: ('CMP', 'indx'), 0xD1: ('CMP', 'indy'),

    0xE0: ('CPX', 'imm'), 0xE4: ('CPX', 'zp'), 0xEC: ('CPX', 'abs'),
    0xC0: ('CPY', 'imm'), 0xC4: ('CPY', 'zp'), 0xCC: ('CPY', 'abs'),

    0xC6: ('DEC', 'zp'), 0xD6: ('DEC', 'zpx'), 0xCE: ('DEC', 'abs'),
    0xDE: ('DEC', 'absx'),
    0xCA: ('DEX', 'imp'), 0x88: ('DEY', 'imp'),

    0x49: ('EOR', 'imm'), 0x45: ('EOR', 'zp'), 0x55: ('EOR', 'zpx'),
    0x4D: ('EOR', 'abs'), 0x5D: ('EOR', 'absx'), 0x59: ('EOR', 'absy'),
    0x41: ('EOR', 'indx'), 0x51: ('EOR', 'indy'),

    0xE6: ('INC', 'zp'), 0xF6: ('INC', 'zpx'), 0xEE: ('INC', 'abs'),
    0xFE: ('INC', 'absx'),
    0xE8: ('INX', 'imp'), 0xC8: ('INY', 'imp'),

    0x4C: ('JMP', 'abs'), 0x6C: ('JMP', 'ind'),
    0x20: ('JSR', 'abs'),

    0xA9: ('LDA', 'imm'), 0xA5: ('LDA', 'zp'), 0xB5: ('LDA', 'zpx'),
    0xAD: ('LDA', 'abs'), 0xBD: ('LDA', 'absx'), 0xB9: ('LDA', 'absy'),
    0xA1: ('LDA', 'indx'), 0xB1: ('LDA', 'indy'),

    0xA2: ('LDX', 'imm'), 0xA6: ('LDX', 'zp'), 0xB6: ('LDX', 'zpy'),
    0xAE: ('LDX', 'abs'), 0xBE: ('LDX', 'absy'),

    0xA0: ('LDY', 'imm'), 0xA4: ('LDY', 'zp'), 0xB4: ('LDY', 'zpx'),
    0xAC: ('LDY', 'abs'), 0xBC: ('LDY', 'absx'),

    0x4A: ('LSR', 'acc'), 0x46: ('LSR', 'zp'), 0x56: ('LSR', 'zpx'),
    0x4E: ('LSR', 'abs'), 0x5E: ('LSR', 'absx'),

    0xEA: ('NOP', 'imp'),

    0x09: ('ORA', 'imm'), 0x05: ('ORA', 'zp'), 0x15: ('ORA', 'zpx'),
    0x0D: ('ORA', 'abs'), 0x1D: ('ORA', 'absx'), 0x19: ('ORA', 'absy'),
    0x01: ('ORA', 'indx'), 0x11: ('ORA', 'indy'),

    0x48: ('PHA', 'imp'), 0x08: ('PHP', 'imp'),
    0x68: ('PLA', 'imp'), 0x28: ('PLP', 'imp'),

    0x2A: ('ROL', 'acc'), 0x26: ('ROL', 'zp'), 0x36: ('ROL', 'zpx'),
    0x2E: ('ROL', 'abs'), 0x3E: ('ROL', 'absx'),

    0x6A: ('ROR', 'acc'), 0x66: ('ROR', 'zp'), 0x76: ('ROR', 'zpx'),
    0x6E: ('ROR', 'abs'), 0x7E: ('ROR', 'absx'),

    0x40: ('RTI', 'imp'), 0x60: ('RTS', 'imp'),

    0xE9: ('SBC', 'imm'), 0xE5: ('SBC', 'zp'), 0xF5: ('SBC', 'zpx'),
    0xED: ('SBC', 'abs'), 0xFD: ('SBC', 'absx'), 0xF9: ('SBC', 'absy'),
    0xE1: ('SBC', 'indx'), 0xF1: ('SBC', 'indy'),

    0x38: ('SEC', 'imp'), 0xF8: ('SED', 'imp'), 0x78: ('SEI', 'imp'),

    0x85: ('STA', 'zp'), 0x95: ('STA', 'zpx'), 0x8D: ('STA', 'abs'),
    0x9D: ('STA', 'absx'), 0x99: ('STA', 'absy'), 0x81: ('STA', 'indx'),
    0x91: ('STA', 'indy'),

    0x86: ('STX', 'zp'), 0x96: ('STX', 'zpy'), 0x8E: ('STX', 'abs'),
    0x84: ('STY', 'zp'), 0x94: ('STY', 'zpx'), 0x8C: ('STY', 'abs'),

    0xAA: ('TAX', 'imp'), 0xA8: ('TAY', 'imp'), 0xBA: ('TSX', 'imp'),
    0x8A: ('TXA', 'imp'), 0x9A: ('TXS', 'imp'), 0x98: ('TYA', 'imp'),
}


# ---------------------------------------------------------------------------
# C64-specific machine: memory-mapped I/O, KERNAL call traps, and the
# hardware quirks this project has actually discovered.
# ---------------------------------------------------------------------------

# PETSCII -> printable-text mapping for CHROUT's captured output, for
# the byte ranges whose displayed glyph doesn't depend on which C64
# character set (default vs. lowercase/uppercase) happens to be active.
# The $41-$5A and $C1-$DA ranges -- whose displayed case DOES depend on
# that -- are handled directly in C64Machine._do_chrout() instead,
# alongside the charset_lower flag that tracks which set is active; see
# that method and c64asm-reference.md's "Text and PETSCII" section.
_PETSCII_TO_TEXT = {}
for _c in range(0x20, 0x7F):
    _PETSCII_TO_TEXT[_c] = chr(_c)
del _c


class _VirtualFile:
    """One open logical file, as tracked by the virtual disk simulation
    in C64Machine (see that class's own __init__ for the bigger
    picture). mode is 'read', 'write', or 'directory' -- 'directory' is
    just a 'read' of generated listing bytes rather than a real file's
    contents, built once at OPEN time (see _generate_directory_listing).
    name is the filename with any ",S,W"/",S,R" (or similar) suffix
    already stripped off, i.e. the same key C64Machine.disk_files
    itself uses."""

    def __init__(self, mode, name, data=b''):
        self.mode = mode
        self.name = name
        if mode in ('read', 'directory'):
            self.data = data
            self.pos = 0
        else:
            self.write_buffer = bytearray()


class C64Machine:
    """Wraps a CPU6502 with just enough C64-specific behavior to run
    this project's own demo and library code and check it did the
    right thing: CIA1 (joystick/keyboard) memory-mapped I/O, KERNAL
    CHROUT/CHRIN call traps, and an optional simulation of KERNAL
    zero-page poisoning. Not a general C64 emulator -- there's no VIC-II
    pixel output or real ROM; register WRITES to VIC-II/SID addresses
    are recorded (see self.io_writes) so a test can assert on them, but
    nothing reads them back with special meaning except where noted
    below.
    """

    CIA1_PRA = 0xDC00
    CIA1_PRB = 0xDC01
    CIA1_DDRA = 0xDC02
    CIA1_DDRB = 0xDC03

    CHROUT = 0xFFD2
    CHRIN = 0xFFCF
    GETIN = 0xFFE4
    READST = 0xFFB7
    SETLFS = 0xFFBA
    SETNAM = 0xFFBD
    OPEN = 0xFFC0
    CLOSE = 0xFFC3
    CHKIN = 0xFFC6
    CHKOUT = 0xFFC9
    CLRCHN = 0xFFCC

    # Real KERNAL housekeeping touches these zero-page locations on
    # every raster/timer IRQ (roughly 60 times a second), regardless of
    # what a running program is doing -- this project discovered the
    # hard way, via VICE and real hardware, that a program's own
    # zero-page scratch variables must avoid this range or get silently
    # corrupted. See c64-memory-reference.md's zero-page notes for the
    # full, community-documented picture; this emulator only needs to
    # know the range exists in order to simulate it.
    KERNAL_POISON_RANGE = range(0xF3, 0xF7)   # $F3-$F6 inclusive

    def __init__(self, simulate_zp_poisoning=True, poison_every_n_instructions=1000):
        self.cpu = CPU6502()
        self.output_text = []        # captured, decoded CHROUT output
        self.output_raw = []          # captured raw PETSCII bytes
        self.charset_lower = False    # tracks the C64's *runtime*
                                        # character set, toggled by
                                        # CHROUT receiving $0E/$8E --
                                        # see _do_chrout() for why this
                                        # affects how $41-$5A decodes
        self.io_writes = []           # [(addr, value)] for every write to
                                        # $D000-$DFFF (VIC/SID/CIA), in order
        self.io_write_log_max = 10000  # cap, so a runaway loop writing I/O
                                         # every instruction can't exhaust memory

        # Simulated input state -- a test sets these directly before
        # calling run()/step(), rather than the machine trying to read
        # from any real keyboard/joystick.
        self.joystick2 = 0x1F        # active-HIGH here for test-writing
                                       # convenience (bit0 up..bit4 fire);
                                       # translated to the real active-LOW
                                       # hardware polarity in _read_cia1_pra
        self.keys_held = set()       # set of (column_select_byte, row_mask)
                                       # pairs currently "held" -- see
                                       # press_key()/release_key()

        self._cia1_output_latch = 0xFF  # last byte WRITTEN to $DC00,
                                           # regardless of DDRA -- on real
                                           # CIA hardware this is always
                                           # latched, but only visible on a
                                           # read for bits DDRA configures
                                           # as outputs (see _read_cia1_pra)
        self._cia1_ddra = 0x00           # data-direction register A: bit=1
                                           # means that pin is an OUTPUT.
                                           # Real hardware/KERNAL default is
                                           # all-input (0x00) until a
                                           # program changes it (this
                                           # project's own CIA_KEYBOARD_SETUP
                                           # sets it to all-output, which is
                                           # exactly what exposes the bug
                                           # this fix models -- see
                                           # _read_cia1_pra)

        self.simulate_zp_poisoning = simulate_zp_poisoning
        self.poison_every_n_instructions = poison_every_n_instructions
        self._next_poison_at = poison_every_n_instructions
        self._rng_state = 0xACE1     # small deterministic PRNG for the
                                       # "garbage" poisoned bytes, so a test
                                       # run is reproducible

        # --- virtual disk / KERNAL file I/O simulation -----------------
        # A simplified model of SETLFS/SETNAM/OPEN/CHKIN/CHKOUT/CLRCHN/
        # CLOSE/READST, plus CHRIN/CHROUT's own file-redirected behavior
        # (see _do_chrin/_do_chrout) -- enough to verify a program's own
        # KERNAL call sequence, register usage, and byte-for-byte file
        # contents are correct, matched against publicly documented
        # KERNAL conventions. This is NOT a real IEC bus/1541 simulation
        # -- there's no serial bus protocol, no device-not-present
        # timing, no real disk block layout. A program that passes every
        # check here is verified against the *documented contract* those
        # KERNAL calls make, not against a real drive; testing against
        # VICE or real hardware is still the right final check for
        # anything that actually does disk I/O.
        self.disk_files = {}          # filename (str, no ",S,W"/",S,R"
                                        # suffix) -> bytes -- the virtual
                                        # disk's contents; a test can
                                        # pre-populate this before a
                                        # simulated LOAD, or inspect it
                                        # after a simulated SAVE
        self.disk_name = "VIRTUAL DISK"  # used only when generating a
                                            # simulated directory listing
        self.device_present = True     # set False to simulate no drive
                                          # at all -- see _do_open
        self.drive_status = "00, OK,00,00\r"   # what the command channel
                                                  # (secondary address 15)
                                                  # reports -- a real 1541
                                                  # says something like
                                                  # "73,CBM DOS V2.6
                                                  # 1541,00,00" right after
                                                  # power-on/reset; a test
                                                  # can set this to a
                                                  # different message to
                                                  # simulate a real drive
                                                  # error being reported
        self._pending_lfs = None       # (logical_file_num, device, sa)
                                          # from the most recent SETLFS
        self._pending_name = None      # filename string from the most
                                          # recent SETNAM
        self._open_files = {}          # logical_file_num -> _VirtualFile
        self._channel_in = None        # logical_file_num currently the
                                          # input channel, or None for the
                                          # keyboard (see CHKIN/CLRCHN)
        self._channel_out = None       # logical_file_num currently the
                                          # output channel, or None for
                                          # the screen (see CHKOUT/CLRCHN)
        self._io_status = 0x00         # READST's own return value --
                                          # real READST clears this back
                                          # to 0 once read, which is why
                                          # _do_readst does the same

        self.cpu.read_byte = self._read_byte
        self.cpu.write_byte = self._write_byte

    # --- input simulation -------------------------------------------

    def press_key(self, column_select, row_mask):
        self.keys_held.add((column_select & 0xFF, row_mask & 0xFF))

    def release_key(self, column_select, row_mask):
        self.keys_held.discard((column_select & 0xFF, row_mask & 0xFF))

    def release_all_keys(self):
        self.keys_held.clear()

    # --- memory-mapped I/O -------------------------------------------

    def _read_byte(self, addr):
        addr &= 0xFFFF
        if addr == self.CIA1_PRA:
            return self._read_cia1_pra()
        if addr == self.CIA1_PRB:
            return self._read_cia1_prb()
        return self.cpu.memory[addr]

    def _read_cia1_pra(self):
        # Real 6526 CIA behavior, per bit: if DDRA configures that bit
        # as an OUTPUT (1), reading it back returns whatever was last
        # WRITTEN to it (the output latch) -- NOT the external pin
        # state. Only bits DDRA configures as INPUT (0) reflect the
        # actual external state, which for $DC00 is joystick port 2.
        #
        # This matters more than it might look: this library's own
        # CIA_KEYBOARD_SETUP configures ALL of DDRA as output (needed
        # for keyboard column-selection, which works by *writing* a
        # column-select byte to this same port). Once that's done,
        # reading $DC00 expecting fresh joystick state -- the way a
        # naive read_joy2 does -- doesn't read the joystick at all
        # anymore: every bit reads back the last column-select value
        # written, misinterpreted as joystick input. See input.inc's
        # header comment for the real-world consequence of this and
        # the fix.
        external_pins = (~self.joystick2) & 0xFF   # active-LOW, real hardware polarity
        return (external_pins & ~self._cia1_ddra) | (self._cia1_output_latch & self._cia1_ddra)

    def _read_cia1_prb(self):
        # Row data for whichever column was most recently selected by a
        # write to $DC00. Active-LOW, same as the joystick: a held key
        # pulls its bit to 0. Starts all-1s (nothing held), then clears
        # the bit for any (column, mask) pair in self.keys_held that
        # matches the currently selected column.
        result = 0xFF
        for column, mask in self.keys_held:
            if column == self._cia1_output_latch:
                result &= ~mask & 0xFF
        return result

    def _write_byte(self, addr, value):
        addr &= 0xFFFF
        value &= 0xFF
        if addr == self.CIA1_PRA:
            self._cia1_output_latch = value
            self.cpu.memory[addr] = value
            return
        if addr == self.CIA1_DDRA:
            self._cia1_ddra = value
            self.cpu.memory[addr] = value
            return
        if 0xD000 <= addr <= 0xDFFF and len(self.io_writes) < self.io_write_log_max:
            self.io_writes.append((addr, value))
        self.cpu.memory[addr] = value

    # --- KERNAL call traps ---------------------------------------------
    # There's no real KERNAL ROM here. Instead, the moment PC would
    # start executing AT one of these addresses (always reached via
    # JSR, per this project's own calling convention -- see
    # text.inc/lib's print_msg wrapper), the trap fires, performs the
    # behaviorally-equivalent action directly, and pops the return
    # address itself, exactly as if the real KERNAL routine had run and
    # hit its own RTS.

    def _maybe_trap_kernal_call(self):
        pc = self.cpu.pc
        if pc == self.CHROUT:
            self._do_chrout()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.CHRIN:
            self._do_chrin()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.GETIN:
            self._do_getin()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.READST:
            self._do_readst()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.SETLFS:
            self._do_setlfs()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.SETNAM:
            self._do_setnam()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.OPEN:
            self._do_open()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.CLOSE:
            self._do_close()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.CHKIN:
            self._do_chkin()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.CHKOUT:
            self._do_chkout()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        if pc == self.CLRCHN:
            self._do_clrchn()
            self.cpu.pc = (self.cpu.pop_word() + 1) & 0xFFFF
            return True
        return False

    def _do_chrout(self):
        b = self.cpu.a
        if self._channel_out is not None:
            # Output redirected to an open file (CHKOUT) -- append the
            # raw byte to that file's own write buffer instead of the
            # screen. Real CHROUT doesn't distinguish "printing" from
            # "writing a file byte" at all; it's the same call either
            # way, just a different destination based on the current
            # output channel -- this mirrors that directly rather than
            # having two separate code paths pretend to be one call.
            vf = self._open_files.get(self._channel_out)
            if vf is not None and vf.mode == 'write':
                vf.write_buffer.append(b)
            return
        self.output_raw.append(b)
        if b == 0x93:
            self.output_text.append('\x0c')   # clear screen, represented
                                                 # as a form-feed in captured
                                                 # text output
        elif b == 0x0D:
            self.output_text.append('\n')
        elif b == 0x0E:
            # PETSCII "switch to lowercase/uppercase character set" --
            # a screen-editor command, not a printable character; see
            # text.inc's SET_LOWERCASE_CHARSET
            self.charset_lower = True
        elif b == 0x8E:
            # PETSCII "switch to uppercase/graphics character set"
            # (the default) -- see text.inc's SET_UPPERCASE_CHARSET
            self.charset_lower = False
        elif 0x41 <= b <= 0x5A:
            # PETSCII's "unshifted" letter range -- displays as
            # lowercase on the lowercase/uppercase character set,
            # uppercase on the default one, hence the charset_lower
            # check. This is what '.charset upper'/'.charset lower'
            # (see c64asm-reference.md's "Text and PETSCII" section)
            # and this project's own ascii_to_petscii() produce for
            # letters in a .text/.asc/.byte string.
            base = ord('a') if self.charset_lower else ord('A')
            self.output_text.append(chr(b - 0x41 + base))
        elif 0xC1 <= b <= 0xDA:
            # PETSCII's "shifted" letter range -- displays as
            # uppercase on EITHER character set, unlike $41-$5A above;
            # this is exactly why '.charset lower' encodes uppercase
            # source letters this way instead.
            self.output_text.append(chr(b - 0xC1 + ord('A')))
        else:
            self.output_text.append(_PETSCII_TO_TEXT.get(b, f'\\x{b:02X}'))
        # Real CHROUT returns with carry clear (success) and doesn't
        # otherwise disturb A/X/Y in the cases this project's code
        # relies on -- nothing further to do here.

    def _do_chrin(self):
        if self._channel_in is not None:
            # Input redirected to an open file (CHKIN) -- pop the next
            # byte from that file's own buffer instead of the keyboard.
            # See _do_readst for the matching EOF-flagging half of this.
            vf = self._open_files.get(self._channel_in)
            if vf is not None and vf.mode in ('read', 'directory'):
                if vf.pos < len(vf.data):
                    self.cpu.a = vf.data[vf.pos]
                    vf.pos += 1
                    if vf.pos >= len(vf.data):
                        self._io_status |= 0x40   # EOF, the same bit
                                                      # real READST uses
                else:
                    self.cpu.a = 0x00
                    self._io_status |= 0x40
                self.cpu.set_nz(self.cpu.a)
                return
        # No real screen editor to read a line from -- a test that
        # needs CHRIN input queues bytes in self.chrin_queue first, and
        # this pops one per call, returning $0D (return) once the
        # queue's empty, matching "pressing return with nothing typed".
        queue = getattr(self, 'chrin_queue', None)
        if queue:
            self.cpu.a = queue.pop(0)
        else:
            self.cpu.a = 0x0D
        self.cpu.set_nz(self.cpu.a)

    def _do_getin(self):
        # Real GETIN pops one byte off the KERNAL's own keyboard
        # buffer (fed by the default keyboard IRQ handler this project
        # doesn't otherwise emulate) and returns 0 immediately if
        # nothing is waiting -- unlike CHRIN above, there's no
        # "assume a blank line" fallback, since GETIN is meant to be
        # polled in a loop, not block. A test that needs GETIN input
        # queues PETSCII bytes in self.getin_queue first (one entry
        # per simulated keypress, in order); this pops one per call,
        # returning 0 once the queue's empty, exactly like a real
        # program polling GETIN with nothing typed yet would see.
        queue = getattr(self, 'getin_queue', None)
        if queue:
            self.cpu.a = queue.pop(0)
        else:
            self.cpu.a = 0x00
        self.cpu.set_nz(self.cpu.a)   # real GETIN's own internal load
                                         # sets these the normal way --
                                         # needed here since code like
                                         # "jsr GETIN / beq ..." depends
                                         # on Z actually reflecting the
                                         # value just returned in A

    def _do_readst(self):
        # Real READST clears the status back to 0 once read -- a well-
        # known KERNAL quirk (save the value if you need to check it
        # more than once). See _do_chrin for the EOF bit this reports.
        self.cpu.a = self._io_status
        self.cpu.set_nz(self.cpu.a)
        self._io_status = 0x00

    def _do_setlfs(self):
        # A=logical file number, X=device number, Y=secondary address.
        # Just remembers the triple for the OPEN that follows -- doesn't
        # touch A/X/Y further, matching real SETLFS.
        self._pending_lfs = (self.cpu.a, self.cpu.x, self.cpu.y)

    def _do_setnam(self):
        # A=name length, X=pointer low byte, Y=pointer high byte.
        # The name bytes in memory are PETSCII, but for the $20-$5F
        # range this project's own keyboard input (_do_getin) and
        # editor.asm's own typing both stay within, PETSCII and ASCII
        # are identical -- so a plain ASCII decode is exact here, not
        # an approximation.
        length = self.cpu.a
        ptr = self.cpu.x | (self.cpu.y << 8)
        raw = bytes(self.cpu.memory[(ptr + i) & 0xFFFF] for i in range(length))
        self._pending_name = raw.decode('ascii', errors='replace')

    def _parse_filename(self):
        """Splits self._pending_name into (base_name, mode), stripping
        a ",S,W"/",S,R"/",W"/",R" -style suffix the same way real CBM
        DOS does -- 'write' if a ",W" appears anywhere in the name,
        'read' otherwise (the default direction when nothing says
        otherwise, matching a plain ",S" or no suffix at all)."""
        name = self._pending_name or ''
        parts = name.split(',')
        base = parts[0]
        mode = 'write' if any(p.strip() == 'W' for p in parts[1:]) else 'read'
        return base, mode

    def _generate_directory_listing(self):
        """Builds the exact byte structure a real 1541 sends for
        LOAD"$",8 / OPEN+CHRIN -- see
        https://www.pagetable.com/?p=273 for the format this follows:
        a fake 2-byte load address, then one BASIC-program-style
        "line" per entry (a link pointer, a line number doubling as
        the block count, null-terminated text), ending in a 2-byte
        $0000 link pointer. Built from self.disk_files, which a test
        populates directly rather than this simulating real disk
        block/BAM layout."""
        out = bytearray([0x01, 0x04])   # fake load address ($0401)

        def add_line(line_number, text_bytes):
            # link pointer: any non-zero placeholder works, since
            # nothing on the reading side uses it for real addressing,
            # only checks "zero means end of listing"
            out.extend([0x01, 0x01])
            out.append(line_number & 0xFF)
            out.append((line_number >> 8) & 0xFF)
            out.extend(text_bytes)
            out.append(0x00)

        name_text = bytearray([0x12])   # reverse video on
        name_text.extend(f'"{self.disk_name}" 00 2a'.upper().encode('ascii'))
        add_line(0, name_text)

        for fname, data in self.disk_files.items():
            blocks = max(1, (len(data) + 253) // 254)
            text = f'   "{fname}"'.encode('ascii')
            text += b' ' * max(0, 20 - len(text))
            text += b'seq'.upper()
            add_line(blocks, text)

        add_line(664, b'BLOCKS FREE.')
        out.extend([0x00, 0x00])   # end of listing
        return bytes(out)

    def _do_open(self):
        # self.device_present (default True) lets a test simulate the
        # drive not being there at all -- no disk image attached in
        # VICE, device powered off, etc. -- which real OPEN reports by
        # setting carry and an error code in A, rather than the
        # program discovering it only once CHRIN starts returning
        # nonsense. A program that never checks carry after OPEN has
        # no way to tell the difference between this and success.
        if not self.device_present:
            self.cpu.set_flag(FLAG_C, True)
            self.cpu.a = 0x05   # DEVICE NOT PRESENT, real KERNAL's own code
            self._io_status |= 0x80
            return
        self.cpu.set_flag(FLAG_C, False)
        if self._pending_lfs is None:
            self._io_status |= 0x80   # no SETLFS -- treat as a device error
            return
        lfn, device, sa = self._pending_lfs
        name = self._pending_name or ''
        if sa == 15:
            # The command channel -- every real disk operation reports
            # its own result here, whether or not a program asks for
            # it, and reading it needs no filename at all (a real
            # OPEN 15,8,15 has nothing after the second 15). A test
            # can set self.drive_status to something other than the
            # default "everything's fine" message to simulate an
            # actual drive error being reported this way instead.
            #
            # A SCRATCH command ("S0:filename", the standard, safe way
            # to delete a file -- see editor.asm's own header comment
            # on why this project deliberately doesn't use the
            # "@0:filename,S,W" save-and-replace shortcut instead,
            # given that mechanism's well-documented data-corruption
            # bug on the original 1541) is handled specially here too:
            # this project's editor only ever sends an exact filename,
            # never a wildcard pattern, so this doesn't attempt
            # wildcard matching -- just an exact, case-sensitive name
            # lookup in self.disk_files, removed if present. The
            # response format (errcode, message, count, 00) matches a
            # real drive's own, with the count being the actual thing
            # a caller needs to check: 0 means nothing matched.
            if name.upper().startswith('S0:') or name.upper().startswith('S:'):
                target = name.split(':', 1)[1]
                if target in self.disk_files:
                    del self.disk_files[target]
                    count = 1
                else:
                    count = 0
                response = f"01,FILES SCRATCHED,{count:02d},00\r"
                self._open_files[lfn] = _VirtualFile(
                    'read', '$command', response.encode('ascii'))
                return
            self._open_files[lfn] = _VirtualFile(
                'read', '$command', self.drive_status.encode('ascii'))
            return
        if name == '$' or name.startswith('$,') or name.startswith('$='):
            if sa != 0:
                # Confirmed against real hardware, not assumed: the
                # special "$" directory request only produces the
                # well-formed BASIC-program-style listing when opened
                # with secondary address 0, matching BASIC's own
                # LOAD"$",8 -- unlike an ordinary file, where any
                # value 2-14 works equally well as a data channel. An
                # earlier version of this simulation didn't enforce
                # this at all, which is exactly why a real bug (this
                # project's own directory-reading code using secondary
                # address 4) passed every test here before eventually
                # being caught on real hardware instead -- see
                # CHANGELOG.md. Modeled here as "file not found" rather
                # than reproducing the exact garbage bytes observed on
                # that one real drive, since the precise bytes aren't
                # something to treat as a portable, general guarantee;
                # what matters for testing purposes is that the wrong
                # secondary address does NOT produce a well-formed
                # listing, which this now enforces either way.
                self._io_status |= 0x40
                self._open_files[lfn] = _VirtualFile('read', '$', b'')
                return
            data = self._generate_directory_listing()
            self._open_files[lfn] = _VirtualFile('directory', '$', data)
            return
        base, mode = self._parse_filename()
        if mode == 'write':
            self._open_files[lfn] = _VirtualFile('write', base)
        else:
            data = self.disk_files.get(base)
            if data is None:
                # Real disk OPEN often succeeds even for a file that
                # doesn't exist, with the error only becoming visible
                # on the first read. This simulation simplifies that
                # by flagging EOF right here rather than waiting for a
                # first CHRIN attempt to discover it -- a program can
                # check READST immediately after OPEN+CHKIN to detect
                # "not found" without needing to first distinguish an
                # empty-but-real file from a missing one, which this
                # simulation doesn't otherwise try to tell apart.
                data = b''
                self._io_status |= 0x40
            self._open_files[lfn] = _VirtualFile('read', base, data)

    def _do_close(self):
        lfn = self.cpu.a
        vf = self._open_files.pop(lfn, None)
        if vf is not None and vf.mode == 'write':
            self.disk_files[vf.name] = bytes(vf.write_buffer)
        if self._channel_in == lfn:
            self._channel_in = None
        if self._channel_out == lfn:
            self._channel_out = None

    def _do_chkin(self):
        self._channel_in = self.cpu.x

    def _do_chkout(self):
        self._channel_out = self.cpu.x

    def _do_clrchn(self):
        self._channel_in = None
        self._channel_out = None

    # --- zero-page poisoning simulation --------------------------------

    def _poison_zero_page(self):
        for addr in self.KERNAL_POISON_RANGE:
            self._rng_state = (self._rng_state * 1103515245 + 12345) & 0xFFFFFFFF
            self.cpu.memory[addr] = (self._rng_state >> 16) & 0xFF

    # --- running ---------------------------------------------------------

    def step(self):
        if self._maybe_trap_kernal_call():
            self.cpu.instructions_run += 1
            return
        if self.simulate_zp_poisoning:
            self._next_poison_at -= 1
            if self._next_poison_at <= 0:
                self._poison_zero_page()
                self._next_poison_at = self.poison_every_n_instructions
        self.cpu.step()

    def run_until_return(self, start_pc, max_instructions=2_000_000):
        """Runs starting at start_pc, treating a real 6502 RTS back to a
        sentinel address on the stack as "the program is done" -- the
        natural way to run something and get control back, matching how
        this project's own demos structure their top-level code (a
        single top-level `rts` that hands control back to BASIC).
        Returns the halt reason string (None if it returned normally)."""
        SENTINEL = 0xFFFF
        self.cpu.push_word((SENTINEL - 1) & 0xFFFF)
        self.cpu.pc = start_pc
        self.cpu.halted = False
        self.cpu.instructions_run = 0
        while not self.cpu.halted and self.cpu.instructions_run < max_instructions:
            if self.cpu.pc == SENTINEL:
                return None   # clean return -- this is success
            self.step()
        if self.cpu.halted:
            return self.cpu.halt_reason
        return f"exceeded {max_instructions} instructions without returning"

    # --- loading -----------------------------------------------------

    def load_prg(self, data):
        """Loads a .prg file's bytes (2-byte little-endian load address
        followed by the program) into memory at that address. Returns
        the load address."""
        load_addr = data[0] | (data[1] << 8)
        self.cpu.memory[load_addr:load_addr + len(data) - 2] = data[2:]
        return load_addr

    def find_sys_target(self, data):
        """Parses a ".basic" BASIC loader stub's tokenized "SYS N" line
        to find the machine-code entry point, the same way LOAD+RUN on
        a real C64 would arrive at it -- rather than requiring a test to
        already know (or hardcode) the entry address."""
        load_addr = data[0] | (data[1] << 8)
        if load_addr != 0x0801:
            raise CPUError(f"expected a BASIC-loadable .prg at $0801, got ${load_addr:04X}")
        body = data[2:]
        # body: [next-line ptr, 2 bytes][line#, 2 bytes][$9E][digits...][$00]...
        idx = 4
        if idx >= len(body) or body[idx] != 0x9E:
            raise CPUError("no SYS token found where expected in the BASIC stub")
        idx += 1
        digits = b''
        while idx < len(body) and body[idx] != 0x00:
            digits += bytes([body[idx]])
            idx += 1
        return int(digits)



