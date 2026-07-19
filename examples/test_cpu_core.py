"""
Self-tests for the CPU6502 core, run before this emulator is trusted
to validate anything else. Small, targeted programs with hand-computed
expected results -- not testing against the emulator's own idea of
what should happen, but against independently worked-out arithmetic
and control flow.
"""

import sys
sys.path.insert(0, '/home/claude/emu_dev')
from mini6502 import CPU6502, FLAG_C, FLAG_Z, FLAG_N, FLAG_V

passed = 0
failed = 0


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {name}  {detail}")


def run_program(bytes_, start=0xC000, max_instr=1000):
    cpu = CPU6502()
    cpu.memory[start:start+len(bytes_)] = bytes(bytes_)
    cpu.pc = start
    # sentinel: RTS with no real caller -- just run a fixed instruction
    # count and inspect state directly, rather than relying on a halt
    # condition, since these are tiny straight-line/branchy snippets.
    for _ in range(max_instr):
        if cpu.halted:
            break
        cpu.step()
    return cpu


print("=== LDA/STA/addressing ===")
# LDA #$42 ; STA $00 ; LDA $00
cpu = run_program([0xA9, 0x42, 0x85, 0x00, 0xA5, 0x00], max_instr=3)
check("LDA immediate", cpu.a == 0x42, f"a={cpu.a:02X}")
check("Z flag clear for nonzero load", cpu.get_flag(FLAG_Z) == 0)
check("N flag clear for positive load", cpu.get_flag(FLAG_N) == 0)

cpu = run_program([0xA9, 0x00], max_instr=1)
check("Z flag set for zero load", cpu.get_flag(FLAG_Z) == 1)

cpu = run_program([0xA9, 0x80], max_instr=1)
check("N flag set for negative (bit7) load", cpu.get_flag(FLAG_N) == 1)

print("=== zero-page,X wraps within zero page ===")
# LDX #$05 ; LDA #$99 ; STA $FE,X  (effective addr should wrap to $03, not $103)
cpu = run_program([0xA2, 0x05, 0xA9, 0x99, 0x95, 0xFE], max_instr=3)
check("zp,X wraps", cpu.memory[0x03] == 0x99, f"mem[3]={cpu.memory[0x03]:02X}")
check("zp,X doesn't write past zero page", cpu.memory[0x103] == 0x00)

print("=== ADC binary arithmetic + flags ===")
# CLC ; LDA #$50 ; ADC #$50  -> 0xA0, signed overflow (both positive, result negative)
cpu = run_program([0x18, 0xA9, 0x50, 0x69, 0x50], max_instr=3)
check("ADC result", cpu.a == 0xA0, f"a={cpu.a:02X}")
check("ADC signed overflow detected", cpu.get_flag(FLAG_V) == 1)
check("ADC no carry (0x50+0x50 < 0x100)", cpu.get_flag(FLAG_C) == 0)

# CLC ; LDA #$FF ; ADC #$01 -> 0x00, carry out, no signed overflow
cpu = run_program([0x18, 0xA9, 0xFF, 0x69, 0x01], max_instr=3)
check("ADC wraps to zero", cpu.a == 0x00)
check("ADC carry out", cpu.get_flag(FLAG_C) == 1)
check("ADC no signed overflow (pos+neg can't overflow)", cpu.get_flag(FLAG_V) == 0)
check("ADC zero flag", cpu.get_flag(FLAG_Z) == 1)

print("=== SBC ===")
# SEC ; LDA #$50 ; SBC #$30  -> 0x20, no borrow (C stays set)
cpu = run_program([0x38, 0xA9, 0x50, 0xE9, 0x30], max_instr=3)
check("SBC result", cpu.a == 0x20, f"a={cpu.a:02X}")
check("SBC no borrow -> C set", cpu.get_flag(FLAG_C) == 1)

# SEC ; LDA #$10 ; SBC #$30  -> borrow needed, C clears
cpu = run_program([0x38, 0xA9, 0x10, 0xE9, 0x30], max_instr=3)
check("SBC with borrow", cpu.a == 0xE0, f"a={cpu.a:02X}")
check("SBC borrow -> C clear", cpu.get_flag(FLAG_C) == 0)

print("=== branches ===")
# LDA #$01 ; CMP #$01 ; BEQ +2 ; LDA #$FF ; LDA #$77 (skip target)
prog = [0xA9, 0x01, 0xC9, 0x01, 0xF0, 0x02, 0xA9, 0xFF, 0xA9, 0x77]
cpu = run_program(prog, max_instr=4)
check("BEQ taken skips the LDA #$FF", cpu.a == 0x77, f"a={cpu.a:02X}")

print("=== JSR/RTS ===")
# JSR sub; BRK ; sub: LDA #$AB; RTS
prog = [0x20, 0x05, 0xC0, 0x00, 0x00, 0xA9, 0xAB, 0x60]
cpu = run_program(prog, max_instr=10)
check("JSR/RTS: subroutine ran", cpu.a == 0xAB, f"a={cpu.a:02X}")
check("JSR/RTS: returned and hit BRK", cpu.halted and 'BRK' in cpu.halt_reason)

print("=== stack (PHA/PLA) ===")
# LDA #$11 ; PHA ; LDA #$22 ; PLA
cpu = run_program([0xA9, 0x11, 0x48, 0xA9, 0x22, 0x68], max_instr=4)
check("PHA/PLA round-trips", cpu.a == 0x11, f"a={cpu.a:02X}")

print("=== indexed indirect (indx) and indirect indexed (indy) ===")
cpu = CPU6502()
# Set up a pointer at zp $10/$11 -> $C050, put $9A there
cpu.memory[0x10] = 0x50
cpu.memory[0x11] = 0xC0
cpu.memory[0xC050] = 0x9A
# LDX #$00 ; LDA ($10,X)
cpu.memory[0xC000:0xC000+4] = bytes([0xA2, 0x00, 0xA1, 0x10])
cpu.pc = 0xC000
cpu.step(); cpu.step()
check("(zp,X) indexed indirect read", cpu.a == 0x9A, f"a={cpu.a:02X}")

cpu2 = CPU6502()
cpu2.memory[0x20] = 0x00
cpu2.memory[0x21] = 0xD0
cpu2.memory[0xD005] = 0x77
# LDY #$05 ; LDA ($20),Y
cpu2.memory[0xC000:0xC000+4] = bytes([0xA0, 0x05, 0xB1, 0x20])
cpu2.pc = 0xC000
cpu2.step(); cpu2.step()
check("(zp),Y indirect indexed read", cpu2.a == 0x77, f"a={cpu2.a:02X}")

print("=== JMP indirect page-boundary bug (faithfully reproduced) ===")
cpu = CPU6502()
# Pointer at $C0FF/  low byte at $C0FF, high byte WRONGLY wraps to $C000
# (a correct CPU would read the high byte from $C100)
cpu.memory[0xC0FF] = 0x34
cpu.memory[0xC000] = 0x12   # what the buggy 6502 actually reads for the high byte
cpu.memory[0xC100] = 0x99   # what a "fixed" CPU would incorrectly use instead
cpu.memory[0xD000:0xD000+3] = bytes([0x6C, 0xFF, 0xC0])  # JMP ($C0FF)
cpu.pc = 0xD000
cpu.step()
check("JMP indirect page-wrap bug reproduced", cpu.pc == 0x1234,
      f"pc={cpu.pc:04X} (expected $1234, the buggy wrap result)")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
