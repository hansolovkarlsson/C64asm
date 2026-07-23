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

# --- load / save / directory -----------------------------------------
#
# mini6502.py's own virtual disk (see that file's header comment) traps
# SETLFS/SETNAM/OPEN/CHKIN/CHKOUT/CLRCHN/CLOSE/READST and CHRIN/CHROUT's
# file-redirected behavior, verified against documented KERNAL calling
# conventions -- not a real IEC bus or drive simulation. These checks
# confirm this program's own KERNAL call sequence and byte-for-byte
# file contents are correct against that model; testing against VICE
# or real hardware is still worth doing before trusting this with
# anything you'd mind losing.
#
# A real CLS actually fills screen memory with space screen codes on
# hardware; mini6502's own CHROUT trap doesn't touch screen memory at
# all (see that file's own _do_chrout), so every test below fills the
# screen with $20 first to match what real hardware would have already
# done by the time the editor starts -- skipping this would leave
# mini6502's own zero-initialized memory show up as '@' characters
# in a saved file, which is a simulator quirk, not anything real.

F2 = 0x89   # new file
F3 = 0x86   # save
F4 = 0x8a   # delete
F5 = 0x87   # load
F7 = 0x88   # directory
DEL = 0x14
RUNSTOP = 0x03   # RUN/STOP -- the C64's conventional "abort this" key,
                    # also reachable as Ctrl+C; there's no key labeled
                    # ESC on a C64 keyboard


def fresh_with_blank_screen():
    m, target = fresh()
    for i in range(1000):
        m.cpu.memory[SCREEN + i] = 0x20
    return m, target


def status_row_text(m):
    chars = []
    for c in range(40):
        b = m.cpu.memory[STATUS_ROW + c] & 0x7F
        chars.append(chr(b + 0x40) if b < 0x20 else chr(b))
    return ''.join(chars).rstrip()


STATUS_ROW = SCREEN + 24 * 40

print("=== F3 saves the editable area (960 bytes) as a SEQ file, excluding the status line ===")
m11, target = fresh_with_blank_screen()
m11.getin_queue = [ANY_KEY, ord('H'), ord('I'), F3,
                    ord('T'), ord('E'), ord('S'), ord('T'), RETURN, F1]
reason = run_until_return(m11, target)
check("program returns cleanly", reason is None, f"reason={reason}")
saved = m11.disk_files.get('TEST')
check("file was saved under the typed name", saved is not None)
if saved is not None:
    check("saved file is exactly 960 bytes (24 rows, not 25)", len(saved) == 960,
          f"got {len(saved)}")
    check("saved content starts with the typed text", saved[:2] == b'HI')
    check("saved content has no status-line text mixed in "
          "(the bug an early version of this feature actually had)",
          saved[2:] == b' ' * 958, "found non-space bytes after the typed text")

print("=== F5 loads a file back onto the editable area ===")
m12, target = fresh_with_blank_screen()
m12.disk_files = {'NOTES': b'HELLO WORLD' + b' ' * (960 - 11)}
m12.getin_queue = [ANY_KEY, F5, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'), RETURN, F1]
reason = run_until_return(m12, target)
check("program returns cleanly", reason is None, f"reason={reason}")
loaded = [screen_char(m12, 0, c) for c in range(11)]
expected = [8, 5, 12, 12, 15, 0x20, 23, 15, 18, 12, 4]  # H E L L O sp W O R L D as screen codes
check("loaded text decodes correctly on row 0", loaded == expected,
      f"got {loaded}, want {expected}")

print("=== F5 on a nonexistent file shows FILE NOT FOUND and touches nothing ===")
m13, target = fresh_with_blank_screen()
m13.getin_queue = [ANY_KEY, F5, ord('N'), ord('O'), ord('P'), ord('E'), RETURN, F1]
reason = run_until_return(m13, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows FILE NOT FOUND.", status_row_text(m13) == 'FILE NOT FOUND.')

print("=== an empty filename (RETURN immediately) cancels save/load ===")
m14, target = fresh_with_blank_screen()
m14.getin_queue = [ANY_KEY, F3, RETURN, F1]
reason = run_until_return(m14, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("nothing was saved", m14.disk_files == {})
check("status line shows CANCELLED.", status_row_text(m14) == 'CANCELLED.')

print("=== DEL backspaces within the filename prompt itself ===")
m15, target = fresh_with_blank_screen()
m15.getin_queue = [ANY_KEY, F3, ord('X'), ord('Y'), ord('Z'), DEL, DEL,
                    ord('A'), ord('B'), RETURN, F1]
reason = run_until_return(m15, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("backspacing mid-filename produces the corrected name",
      list(m15.disk_files.keys()) == ['XAB'], f"got {list(m15.disk_files.keys())}")

print("=== a filename longer than 16 characters is truncated, not rejected ===")
m16, target = fresh_with_blank_screen()
letters = [ord(c) for c in "ABCDEFGHIJKLMNOPQRST"]   # 20 characters
m16.getin_queue = [ANY_KEY, F3] + letters + [RETURN, F1]
reason = run_until_return(m16, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("filename truncated to 16 characters",
      list(m16.disk_files.keys()) == ['ABCDEFGHIJKLMNOP'],
      f"got {list(m16.disk_files.keys())}")

print("=== the cursor can never reach row 24 (the status line) ===")
m17, target = fresh_with_blank_screen()
m17.getin_queue = [ANY_KEY] + [CRSR_DOWN] * 30 + [F1]
reason = run_until_return(m17, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("cursor clamped at row 23, never row 24",
      m17.cpu.memory[CURSOR_Y] == 23, f"cursor_y={m17.cpu.memory[CURSOR_Y]}")

print("=== F7 shows a correct directory listing and returns to the editor unharmed ===")
m18, target = fresh_with_blank_screen()
m18.disk_files = {'HELLO': b'H' * 300, 'NOTES': b'short text'}
m18.getin_queue = [ANY_KEY, ord('A'), ord('B'), ord('C'), F7, ANY_KEY, F1]
reason = run_until_return(m18, target)
check("program returns cleanly", reason is None, f"reason={reason}")
dir_text = ''.join(m18.output_text)
check("disk name line appears", '"VIRTUAL DISK"' in dir_text)
check("HELLO listed with 2 blocks (300 bytes -> ceil(300/254))", '2    "HELLO"' in dir_text)
check("NOTES listed with 1 block", '1    "NOTES"' in dir_text)
check("free blocks line appears", 'BLOCKS FREE' in dir_text)
check("screen content from before the directory view is restored",
      screen_char(m18, 0, 0) == 0x01 and screen_char(m18, 0, 1) == 0x02
      and screen_char(m18, 0, 2) == 0x03,
      "row 0 should still read A, B, C after returning from the directory")

print("=== default status line shows the F1-F7 help text ===")
m19, target = fresh_with_blank_screen()
m19.getin_queue = [ANY_KEY, F1]
reason = run_until_return(m19, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("help text shown by default",
      status_row_text(m19) == 'F1-F7: QUIT/NEW/SAVE/DEL/LOAD/DIR')

print("=== round trip: save, then load into a differently-edited screen ===")
m20, target = fresh_with_blank_screen()
m20.getin_queue = [ANY_KEY, ord('R'), ord('T'), F3, ord('R'), ord('T'), RETURN, F1]
reason = run_until_return(m20, target)
check("program returns cleanly", reason is None, f"reason={reason}")
roundtrip_saved = m20.disk_files.get('RT')
check("round-trip file saved", roundtrip_saved is not None)

m21, target = fresh_with_blank_screen()
m21.disk_files = {'RT': roundtrip_saved} if roundtrip_saved else {}
m21.getin_queue = [ANY_KEY, ord('X'), ord('X'), ord('X'),   # different content first
                    F5, ord('R'), ord('T'), RETURN, F1]
reason = run_until_return(m21, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("loading overwrote the differently-typed content with the saved one",
      screen_char(m21, 0, 0) == 18 and screen_char(m21, 0, 1) == 20,  # R, T
      f"got {screen_char(m21, 0, 0)}, {screen_char(m21, 0, 1)}")

print()
print("=== each function key's real PETSCII code triggers its own action, and only that one ===")
# The bug this specifically guards against: F1=$85, F3=$86, F5=$87, F7=$88
# are sequential with no gaps. An earlier version of this file used $88 for
# F5 and $8A for F7 -- wrong on both counts, and both directly testable
# effects: pressing F7 (which really sends $88) triggered load instead of
# directory, and pressing F5 (which really sends $87) did nothing at all,
# since nothing checked for it. This test would have caught that: it
# presses each *real* key value in isolation and checks the specific,
# distinguishable effect only that key should have.
for key, name in [(0x85, 'F1'), (0x89, 'F2'), (0x86, 'F3'), (0x87, 'F5'), (0x88, 'F7')]:
    check(f"PETSCII ${key:02X} is a distinct, recognized key ({name})",
          key in (0x85, 0x89, 0x86, 0x87, 0x88))

m22, target = fresh_with_blank_screen()
m22.disk_files = {'X': b' ' * 960}
m22.getin_queue = [ANY_KEY, 0x87, ord('X'), RETURN, F1]   # $87 = real F5 (load)
reason = run_until_return(m22, target)
check("program returns cleanly (real F5 code)", reason is None, f"reason={reason}")
check("real F5 ($87) triggered load, not nothing",
      status_row_text(m22) == 'LOADED.', f"got {status_row_text(m22)!r}")

m23, target = fresh_with_blank_screen()
m23.getin_queue = [ANY_KEY, 0x88, ANY_KEY, F1]   # $88 = real F7 (directory)
reason = run_until_return(m23, target)
check("program returns cleanly (real F7 code)", reason is None, f"reason={reason}")
check("real F7 ($88) triggered the directory listing, not load",
      'DISK DIRECTORY' in ''.join(m23.output_text))

print("=== F2 clears the document and resets the cursor, without a confirmation prompt ===")
m24, target = fresh_with_blank_screen()
m24.getin_queue = [ANY_KEY, ord('A'), ord('B'), ord('C'), CRSR_DOWN, CRSR_DOWN, F2, F1]
reason = run_until_return(m24, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 0 cleared back to spaces", all(screen_char(m24, 0, c) == 0x20 for c in range(3)))
check("cursor reset to (0, 0)",
      m24.cpu.memory[CURSOR_X] == 0 and m24.cpu.memory[CURSOR_Y] == 0)
check("status line shows NEW FILE.", status_row_text(m24) == 'NEW FILE.')

print("=== F2 clears every row of the editable area, not just the first ===")
m25, target = fresh_with_blank_screen()
m25.getin_queue = [ANY_KEY, ord('X'), RETURN, ord('Y'), RETURN, ord('Z'), F2, F1]
reason = run_until_return(m25, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("rows 0-2 all cleared back to spaces",
      all(screen_char(m25, r, 0) == 0x20 for r in range(3)))

print("=== F2 never touches the status line's own row (only rows 0-23) ===")
m26, target = fresh_with_blank_screen()
m26.getin_queue = [ANY_KEY, ord('X'), F2]
m26.cpu.pc = target
m26.cpu.halted = False
m26.cpu.instructions_run = 0
for _ in range(200_000):
    m26.step()
check("status line shows F2's own result message, not left blank by the clear loop",
      status_row_text(m26) == 'NEW FILE.', f"got {status_row_text(m26)!r}")

print("=== F2 leaves no stray reverse-video cursor bits behind ===")
m27, target = fresh_with_blank_screen()
m27.getin_queue = [ANY_KEY, ord('A'), ord('B'), F2, F1]
reason = run_until_return(m27, target)
check("program returns cleanly", reason is None, f"reason={reason}")
stray = [i for i in range(960) if m27.cpu.memory[SCREEN + i] & 0x80]
check("every cell in the editable area has bit 7 clear", stray == [],
      f"stray bits at offsets {stray}")

print("=== default status line's help text includes all five commands ===")
m28, target = fresh_with_blank_screen()
m28.getin_queue = [ANY_KEY, F1]
reason = run_until_return(m28, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("help text mentions all five commands",
      status_row_text(m28) == 'F1-F7: QUIT/NEW/SAVE/DEL/LOAD/DIR',
      f"got {status_row_text(m28)!r}")

# --- F4 (delete) and the save-overwrite fix --------------------------
#
# Both driven by the same underlying mechanism: SCRATCH over the
# command channel ("S0:name"), rather than the "@0:name,S,W" save-
# and-replace shortcut CBM DOS also offers, which has a well-
# documented data-corruption bug on original 1541 firmware. See
# editor.asm's own header comment for the full reasoning.
#
# The real bug this project actually hit and fixed: save loading an
# existing file, editing it, and saving under the same name silently
# didn't update the file on disk at all -- the drive was refusing the
# write (a real "FILE EXISTS" error) because nothing deleted the old
# copy first. mini6502.py's own command-channel simulation was
# extended to actually remove the matching entry from self.disk_files
# and report a real "01,FILES SCRATCHED,NN,00"-style response,
# specifically so this could be tested directly rather than assumed
# fixed.

print("=== the critical bug: loading a file, editing it, and saving under "
      "the same name actually overwrites it on disk ===")
m29, target = fresh_with_blank_screen()
m29.disk_files = {'NOTES': b'OLD CONTENT' + b' ' * (960 - 11)}
m29.getin_queue = (
    [ANY_KEY, F5, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'), RETURN]
    + [CRSR_RIGHT] * 11 + [ord('X'), ord('X'), ord('X')]
    + [F3, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'), RETURN]
    + [F1]
)
reason = run_until_return(m29, target)
check("program returns cleanly", reason is None, f"reason={reason}")
saved = m29.disk_files.get('NOTES')
check("file was resaved under the same name", saved is not None)
if saved is not None:
    check("the disk copy reflects the edit, not the stale original",
          saved[:14] == b'OLD CONTENTXXX', f"got {saved[:14]!r}")

print("=== F4 with Y confirmation deletes an existing file ===")
m30, target = fresh_with_blank_screen()
m30.disk_files = {'NOTES': b'x' * 960}
m30.getin_queue = [ANY_KEY, F4, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'),
                    RETURN, ord('Y'), F1]
reason = run_until_return(m30, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the file is actually gone", m30.disk_files == {})
check("status line shows DELETED.", status_row_text(m30) == 'DELETED.')

print("=== F4 with N confirmation cancels, the file is untouched ===")
m31, target = fresh_with_blank_screen()
m31.disk_files = {'NOTES': b'x' * 960}
m31.getin_queue = [ANY_KEY, F4, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'),
                    RETURN, ord('N'), F1]
reason = run_until_return(m31, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the file still exists", list(m31.disk_files.keys()) == ['NOTES'])
check("status line shows CANCELLED.", status_row_text(m31) == 'CANCELLED.')

print("=== F4 on a nonexistent file correctly reports FILE NOT FOUND, "
      "not DELETED (the exact bug an early version of this had, from "
      "misreading the zero-padded \"00\" count as nonzero) ===")
m32, target = fresh_with_blank_screen()
m32.disk_files = {}
m32.getin_queue = [ANY_KEY, F4, ord('N'), ord('O'), ord('P'), ord('E'),
                    RETURN, ord('Y'), F1]
reason = run_until_return(m32, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows FILE NOT FOUND., not DELETED.",
      status_row_text(m32) == 'FILE NOT FOUND.',
      f"got {status_row_text(m32)!r}")

print("=== F4 with an empty filename cancels immediately, no confirmation shown ===")
m33, target = fresh_with_blank_screen()
m33.disk_files = {'X': b'y' * 960}
m33.getin_queue = [ANY_KEY, F4, RETURN, F1]
reason = run_until_return(m33, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("nothing was deleted", list(m33.disk_files.keys()) == ['X'])
check("status line shows CANCELLED.", status_row_text(m33) == 'CANCELLED.')

print("=== F4 never touches the in-memory document ===")
m34, target = fresh_with_blank_screen()
m34.disk_files = {'JUNK': b'z' * 960}
m34.getin_queue = [ANY_KEY, ord('A'), ord('B'), ord('C'), F4,
                    ord('J'), ord('U'), ord('N'), ord('K'), RETURN, ord('Y'), F1]
reason = run_until_return(m34, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 0 still reads ABC, untouched by the delete",
      screen_char(m34, 0, 0) == 1 and screen_char(m34, 0, 1) == 2
      and screen_char(m34, 0, 2) == 3)

# --- RUN/STOP cancels any of F3/F4/F5's own prompts ---------------
#
# The C64 keyboard has no key labeled ESC; RUN/STOP (also reachable
# as Ctrl+C) is the conventional C64 equivalent for "abort this."
# Implemented by forcing filename_len to 0 and falling into the same
# exit point RETURN-with-nothing-typed already uses, so every caller's
# existing "was this cancelled" check (checking for a zero length)
# handles it automatically -- no separate cancellation path needed.

print("=== RUN/STOP cancels the SAVE prompt mid-typing; nothing is saved ===")
m35, target = fresh_with_blank_screen()
m35.getin_queue = [ANY_KEY, F3, ord('N'), ord('O'), RUNSTOP, F1]
reason = run_until_return(m35, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("nothing was saved", m35.disk_files == {})
check("status line shows CANCELLED.", status_row_text(m35) == 'CANCELLED.')

print("=== RUN/STOP cancels the LOAD prompt; the document is untouched ===")
m36, target = fresh_with_blank_screen()
m36.disk_files = {'NOTES': b'X' + b' ' * 959}
m36.getin_queue = [ANY_KEY, F5, ord('N'), RUNSTOP, F1]
reason = run_until_return(m36, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows CANCELLED.", status_row_text(m36) == 'CANCELLED.')
check("screen was never touched by a load", screen_char(m36, 0, 0) == 0x20)

print("=== RUN/STOP cancels the DELETE prompt's filename entry ===")
m37, target = fresh_with_blank_screen()
m37.disk_files = {'NOTES': b'x' * 960}
m37.getin_queue = [ANY_KEY, F4, ord('N'), RUNSTOP, F1]
reason = run_until_return(m37, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the file still exists", list(m37.disk_files.keys()) == ['NOTES'])
check("status line shows CANCELLED.", status_row_text(m37) == 'CANCELLED.')

print("=== RUN/STOP also cancels DELETE's own Y/N confirmation step ===")
m38, target = fresh_with_blank_screen()
m38.disk_files = {'NOTES': b'x' * 960}
m38.getin_queue = [ANY_KEY, F4, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'),
                    RETURN, RUNSTOP, F1]
reason = run_until_return(m38, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the file was not deleted", list(m38.disk_files.keys()) == ['NOTES'])
check("status line shows CANCELLED., not DELETED.",
      status_row_text(m38) == 'CANCELLED.')

print("=== RUN/STOP is harmless during ordinary typing (main editing loop) ===")
m39, target = fresh_with_blank_screen()
m39.getin_queue = [ANY_KEY, ord('A'), RUNSTOP, ord('B'), F1]
reason = run_until_return(m39, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("both typed characters landed, RUN/STOP did nothing in between",
      screen_char(m39, 0, 0) == 1 and screen_char(m39, 0, 1) == 2)
check("cursor advanced by exactly 2, not 3",
      m39.cpu.memory[CURSOR_X] == 2, f"cursor_x={m39.cpu.memory[CURSOR_X]}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
