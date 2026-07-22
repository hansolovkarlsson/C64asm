"""
End-to-end regression test for editor.asm, using mini6502.py (see
mini6502.zip). Unlike this project's other demo tests, editor.asm
reads input via GETIN ($FFE4) -- the KERNAL's own keyboard-buffer poll,
correct for a custom full-screen editor in a way lib/keyboard.inc's
matrix-scanning READ_KEY isn't (that's for checking specific individual
keys in a game, not general text entry). mini6502 didn't emulate GETIN
before this test needed it; see mini6502.py's own _do_getin for the
new trap this test exercises, fed via m.getin_queue (queue one PETSCII
byte per simulated keypress, popped one per GETIN call, returning 0
once empty -- matching real GETIN's own "nothing typed yet" behavior).

The checks below confirm actual screen memory contents and cursor
state after specific key sequences -- not just that assembly succeeds
or that the program runs without crashing. One real mistake was caught
while writing these: an early version of the boundary-clamping test
used $9D (cursor LEFT) where $1D (cursor RIGHT) was intended, which
looked at first like a bug in the editor itself before turning out to
be a mistake in the test's own key sequence, not the code under test.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_editor.py
"""

import os
import re
import subprocess
import sys

try:
    from mini6502 import C64Machine
except ImportError:
    sys.exit("mini6502.py not found -- put it on PYTHONPATH (see mini6502.zip)")

passed = 0
failed = 0


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {name}  {detail}")


def find_c64asm():
    for candidate in ['c64asm.py', '/mnt/user-data/outputs/c64asm.py']:
        if os.path.exists(candidate):
            return candidate
    sys.exit("c64asm.py not found")


ASSEMBLER = find_c64asm()

print("=== assembling editor.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'editor.asm', '-o', '/tmp/editor_regress.prg',
     '--listing', '/tmp/editor_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("editor.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/editor_regress.prg', 'rb') as f:
    data = f.read()

SCREEN = 0x0400
CURSOR_X = 0x033c
CURSOR_Y = 0x033d

# PETSCII codes for the non-typable keys editor.asm handles specially.
RETURN = 0x0D
DEL = 0x14
CRSR_DOWN = 0x11
CRSR_UP = 0x91
CRSR_RIGHT = 0x1D
CRSR_LEFT = 0x9D
F1 = 0x85
ANY_KEY = ord(' ')   # dismisses the "press any key to start" prompt


def fresh():
    m = C64Machine(simulate_zp_poisoning=False)
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


def run_until_return(m, start_pc, max_instructions=3_000_000):
    SENTINEL = 0xFFFF
    m.cpu.push_word((SENTINEL - 1) & 0xFFFF)
    m.cpu.pc = start_pc
    m.cpu.halted = False
    m.cpu.instructions_run = 0
    while not m.cpu.halted and m.cpu.instructions_run < max_instructions:
        if m.cpu.pc == SENTINEL:
            return None
        m.step()
    return m.cpu.halt_reason if m.cpu.halted else f"exceeded {max_instructions} instructions"


def screen_char(m, row, col):
    return m.cpu.memory[SCREEN + row * 40 + col] & 0x7F   # strip any cursor reverse-video bit


print("=== typing letters writes the correct screen codes ===")
m, target = fresh()
m.getin_queue = [ANY_KEY, ord('H'), ord('I'), F1]
reason = run_until_return(m, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("'H' -> screen code $08", screen_char(m, 0, 0) == 0x08)
check("'I' -> screen code $09", screen_char(m, 0, 1) == 0x09)
check("cursor advanced to column 2", m.cpu.memory[CURSOR_X] == 2)

print("=== typing past column 39 wraps to the next row ===")
m2, target = fresh()
m2.getin_queue = [ANY_KEY] + [ord('A')] * 41 + [F1]
reason = run_until_return(m2, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 0 filled with 'A' (screen code $01)",
      all(screen_char(m2, 0, c) == 0x01 for c in range(40)))
check("row 1 column 0 also 'A'", screen_char(m2, 1, 0) == 0x01)
check("cursor wrapped to (1, 1)",
      m2.cpu.memory[CURSOR_X] == 1 and m2.cpu.memory[CURSOR_Y] == 1)

print("=== DEL erases the character behind the cursor, wrapping across lines ===")
m3, target = fresh()
m3.getin_queue = [ANY_KEY] + [ord('A')] * 41 + [DEL, DEL, F1]
reason = run_until_return(m3, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("cursor wrapped back to end of row 0 (39, 0)",
      m3.cpu.memory[CURSOR_X] == 39 and m3.cpu.memory[CURSOR_Y] == 0)
check("row 0 column 39 erased back to a space", screen_char(m3, 0, 39) == 0x20)

print("=== DEL at the very first cell (0,0) does nothing ===")
m4, target = fresh()
m4.getin_queue = [ANY_KEY, DEL, DEL, ord('Z'), F1]
reason = run_until_return(m4, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("cursor still at (0,0) before the typed character took effect",
      screen_char(m4, 0, 0) == 0x1A)  # 'Z' -> screen code $1A, proving DEL didn't move anything

print("=== cursor movement: clamped at the top-left corner ===")
m5, target = fresh()
m5.getin_queue = [ANY_KEY, CRSR_UP, CRSR_UP, CRSR_LEFT, CRSR_LEFT, CRSR_RIGHT, CRSR_RIGHT, F1]
reason = run_until_return(m5, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("cursor clamped at row 0 then moved right twice -> (2, 0)",
      m5.cpu.memory[CURSOR_X] == 2 and m5.cpu.memory[CURSOR_Y] == 0)

print("=== cursor left/right actually move within a line ===")
m6, target = fresh()
m6.getin_queue = [ANY_KEY, ord('A'), ord('B'), ord('C'), CRSR_LEFT, CRSR_LEFT, ord('X'), F1]
reason = run_until_return(m6, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("'X' overwrote the middle character ('B')", screen_char(m6, 0, 1) == 0x18)
check("first character ('A') untouched", screen_char(m6, 0, 0) == 0x01)
check("third character ('C') untouched", screen_char(m6, 0, 2) == 0x03)

print("=== RETURN moves to the start of the next line ===")
m7, target = fresh()
m7.getin_queue = [ANY_KEY, ord('X'), ord('Y'), RETURN, ord('Z'), F1]
reason = run_until_return(m7, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 1 column 0 has the post-RETURN character", screen_char(m7, 1, 0) == 0x1A)
check("cursor at (1, 1) after typing one character on the new line",
      m7.cpu.memory[CURSOR_X] == 1 and m7.cpu.memory[CURSOR_Y] == 1)

print("=== cursor down/up move without altering anything ===")
m8, target = fresh()
m8.getin_queue = [ANY_KEY, CRSR_DOWN, CRSR_DOWN, CRSR_UP, ord('Q'), F1]
reason = run_until_return(m8, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("ends on row 1 after down/down/up", m8.cpu.memory[CURSOR_Y] == 1)
check("'Q' landed on row 1, not row 0", screen_char(m8, 1, 0) == 0x11)
check("row 0 wasn't written to", screen_char(m8, 0, 0) != 0x11)

print("=== no stray reverse-video (cursor) bits left after a clean exit ===")
m9, target = fresh()
m9.getin_queue = [ANY_KEY, ord('A'), ord('B'), CRSR_DOWN, CRSR_RIGHT, CRSR_LEFT, CRSR_UP, F1]
reason = run_until_return(m9, target)
check("program returns cleanly", reason is None, f"reason={reason}")
stray = [i for i in range(1000) if m9.cpu.memory[SCREEN + i] & 0x80]
check("every screen cell has bit 7 clear", stray == [], f"stray bits at offsets {stray}")

print("=== F1 exits and prints a goodbye message ===")
m10, target = fresh()
m10.getin_queue = [ANY_KEY, F1]
reason = run_until_return(m10, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("GOODBYE printed on exit", 'GOODBYE' in ''.join(m10.output_text))

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
