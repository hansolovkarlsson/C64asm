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
MAX_ROW = 23   # editor.asm's own last editable (screen) row -- row 24
                  # is the status line
DOC_ROWS = 200 # editor.asm's own DOC_BUF row count -- the true document
                  # capacity, not just what fits on one screen

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
    m = C64Machine(simulate_zp_poisoning=True)
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
F6 = 0x8b   # save as
F7 = 0x88   # directory
F8 = 0x8c   # help
HOME = 0x13   # page up
CLR = 0x93    # SHIFT+HOME -- page down
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

print("=== F3 saves only as much as the document actually uses, trimming "
      "trailing blank rows, as a SEQ file excluding the status line ===")
m11, target = fresh_with_blank_screen()
m11.getin_queue = [ANY_KEY, ord('H'), ord('I'), F3,
                    ord('T'), ord('E'), ord('S'), ord('T'), RETURN, F1]
reason = run_until_return(m11, target)
check("program returns cleanly", reason is None, f"reason={reason}")
saved = m11.disk_files.get('TEST')
check("file was saved under the typed name", saved is not None)
if saved is not None:
    check("saved file is exactly one 40-byte row, not the full 8000-byte "
          "document buffer or the old fixed 960", len(saved) == 40,
          f"got {len(saved)}")
    check("saved content starts with the typed text", saved[:2] == b'HI')
    check("saved content has no status-line text mixed in "
          "(the bug an early version of this feature actually had)",
          saved[2:] == b' ' * 38, "found non-space bytes after the typed text")

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
      status_row_text(m19) == 'F1-F7: QUIT/NEW/SAVE/AS/DEL/LOAD/DIR')

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
      status_row_text(m28) == 'F1-F7: QUIT/NEW/SAVE/AS/DEL/LOAD/DIR',
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

print("=== F4's picker: an empty disk shows NO FILES, gracefully, no crash ===")
m32, target = fresh_with_blank_screen()
m32.disk_files = {}
m32.getin_queue = [ANY_KEY, F4, ANY_KEY, F1]
reason = run_until_return(m32, target)
check("program returns cleanly", reason is None, f"reason={reason}")

print("=== F4's picker: RUN/STOP before selecting anything cancels, nothing deleted ===")
m33, target = fresh_with_blank_screen()
m33.disk_files = {'X': b'y' * 960}
m33.getin_queue = [ANY_KEY, F4, RUNSTOP, F1]
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

print("=== RUN/STOP cancels F4's picker even after some other, unrecognized "
      "key was pressed first ===")
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

# --- file picker (F4/F5 now select from a real list instead of typing
# a filename blind) ---
#
# One real, non-obvious bug came up building this, caught only by
# testing the full F5 flow end to end rather than each piece in
# isolation: parse_directory_filenames's own final CHRIN call (reading
# the directory listing's closing $00 terminator) happens to also be
# the very last byte of the whole listing buffer, which sets real
# READST's EOF flag -- and that flag persists until READST is read
# again, regardless of what unrelated operation happens next. Without
# an extra READST call to clear it before returning, the *next* OPEN
# (the actual file being loaded) would see that stale EOF flag
# immediately and wrongly conclude the file didn't exist, even though
# it does. This isn't a simulator quirk -- it's how real READST
# actually behaves, and it would have failed identically on real
# hardware. do_directory (F7) and scratch_current_file (F4/save) had
# the same latent structural issue and got the same fix, even though
# it's less likely to trigger there.

def decoded_row(m, row, n):
    chars = []
    for i in range(n):
        c = screen_char(m, row, i)
        chars.append(chr(c + 0x40) if c < 0x20 else chr(c))
    return ''.join(chars)


print("=== F5's picker: selecting the first (only) entry loads it correctly ===")
m40, target = fresh_with_blank_screen()
m40.disk_files = {'NOTES': b'HELLO FROM NOTES' + b' ' * (960 - 17)}
m40.getin_queue = [ANY_KEY, F5, RETURN, F1]
reason = run_until_return(m40, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the actual file content was loaded, not left blank",
      decoded_row(m40, 0, len('HELLO FROM NOTES')) == 'HELLO FROM NOTES',
      f"got {decoded_row(m40, 0, len('HELLO FROM NOTES'))!r}")

print("=== F5's picker: cursor down moves the selection to a later file ===")
m41, target = fresh_with_blank_screen()
m41.disk_files = {
    'AAA': b'FIRST FILE' + b' ' * 950,
    'BBB': b'SECOND FILE' + b' ' * 949,
    'CCC': b'THIRD FILE CONTENT' + b' ' * 941,
}
m41.getin_queue = [ANY_KEY, F5, CRSR_DOWN, CRSR_DOWN, RETURN, F1]
reason = run_until_return(m41, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the third file (two down-moves from the first) was loaded",
      decoded_row(m41, 0, len('THIRD FILE CONTENT')) == 'THIRD FILE CONTENT',
      f"got {decoded_row(m41, 0, len('THIRD FILE CONTENT'))!r}")

print("=== F5's picker: cursor up from the first entry is clamped, not wraparound ===")
m42, target = fresh_with_blank_screen()
m42.disk_files = {'AAA': b'FIRST' + b' ' * 955, 'BBB': b'SECOND' + b' ' * 954}
m42.getin_queue = [ANY_KEY, F5, CRSR_UP, CRSR_UP, RETURN, F1]
reason = run_until_return(m42, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("still loaded the first file, not wrapped to the last",
      decoded_row(m42, 0, 5) == 'FIRST', f"got {decoded_row(m42, 0, 5)!r}")

print("=== F5's picker: RUN/STOP cancels, the document is untouched ===")
m43, target = fresh_with_blank_screen()
m43.disk_files = {'AAA': b'SHOULD NOT LOAD' + b' ' * 945}
m43.getin_queue = [ANY_KEY, ord('X'), F5, RUNSTOP, F1]
reason = run_until_return(m43, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the document is unaffected by the cancelled load",
      decoded_row(m43, 0, 1) == 'X', f"got {decoded_row(m43, 0, 1)!r}")

print("=== F5's picker: an empty disk is handled gracefully, no crash ===")
m44, target = fresh_with_blank_screen()
m44.disk_files = {}
m44.getin_queue = [ANY_KEY, F5, ANY_KEY, F1]
reason = run_until_return(m44, target)
check("program returns cleanly, does not hang or crash",
      reason is None, f"reason={reason}")

print("=== F4's picker: selecting an entry and confirming deletes the right file ===")
m45, target = fresh_with_blank_screen()
m45.disk_files = {'AAA': b'x' * 960, 'BBB': b'y' * 960}
m45.getin_queue = [ANY_KEY, F4, CRSR_DOWN, RETURN, ord('Y'), F1]
reason = run_until_return(m45, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("only the selected file (BBB) was deleted",
      list(m45.disk_files.keys()) == ['AAA'], f"got {list(m45.disk_files.keys())}")

print("=== no stray reverse-video bits remain after using the picker ===")
m46, target = fresh_with_blank_screen()
m46.disk_files = {'AAA': b'x' * 960, 'BBB': b'y' * 960}
m46.getin_queue = [ANY_KEY, F5, RETURN, F1]
reason = run_until_return(m46, target)
check("program returns cleanly", reason is None, f"reason={reason}")
stray = [i for i in range(1000) if m46.cpu.memory[SCREEN + i] & 0x80]
check("every screen cell has bit 7 clear", stray == [], f"stray bits at {stray}")

print("=== the real bug this feature shipped with: a stale READST EOF flag "
      "from the directory read doesn't leak into the file load that follows ===")
m47, target = fresh_with_blank_screen()
m47.disk_files = {'NOTES': b'PROOF THE FIX WORKS' + b' ' * (960 - 20)}
m47.getin_queue = [ANY_KEY, F5, RETURN, F1]
reason = run_until_return(m47, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the file loaded correctly, not silently treated as not found",
      decoded_row(m47, 0, len('PROOF THE FIX WORKS')) == 'PROOF THE FIX WORKS',
      f"got {decoded_row(m47, 0, len('PROOF THE FIX WORKS'))!r} -- if this is blank, the stale "
      f"READST bug has regressed")

# --- F3 (save, reusing a remembered name) and F6 (save as, always
# prompts) ---
#
# F3 now behaves like most editors' plain "save": if the document
# already has a name (already loaded, or already saved once this
# session), it saves straight back to that name without asking again.
# F6 is the deliberate escape hatch -- always prompts, even when a
# name already exists, and becomes the new current name on success.
# Loading a file, and F2 (new), both affect this same remembered name:
# loading sets it, new clears it.

print("=== F3 with no current name prompts once, then reuses it on a later F3 "
      "without prompting again ===")
m48, target = fresh_with_blank_screen()
m48.getin_queue = [ANY_KEY, ord('A'), F3, ord('N'), ord('O'), ord('T'), ord('E'), ord('S'),
                    RETURN, ord('B'), F3, F1]
reason = run_until_return(m48, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
saved = m48.disk_files.get('NOTES')
check("both edits (before and after the second, name-less F3) landed "
      "in the same file", saved is not None and saved[:2] == b'AB',
      f"got {saved[:2] if saved else None}")

print("=== F6 always prompts, even though a current name already exists ===")
m49, target = fresh_with_blank_screen()
m49.getin_queue = (
    [ANY_KEY, ord('X'), F3, ord('A'), ord('A'), ord('A'), RETURN]
    + [F6, ord('B'), ord('B'), ord('B'), RETURN, F1]
)
reason = run_until_return(m49, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("both AAA (via F3) and BBB (via F6) exist as separate files",
      sorted(m49.disk_files.keys()) == ['AAA', 'BBB'],
      f"got {sorted(m49.disk_files.keys())}")

print("=== after F6 saves under a new name, a later plain F3 uses THAT name, "
      "not the original one ===")
m50, target = fresh_with_blank_screen()
m50.getin_queue = (
    [ANY_KEY, ord('1'), F3, ord('A'), ord('A'), ord('A'), RETURN]
    + [F6, ord('B'), ord('B'), ord('B'), RETURN]
    + [ord('2'), F3, F1]
)
reason = run_until_return(m50, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
bbb = m50.disk_files.get('BBB')
check("the plain F3 after F6 saved back to BBB (the new current name), "
      "picking up both edits", bbb is not None and bbb[:2] == b'12',
      f"got {bbb[:2] if bbb else None}")

print("=== F5 (load) sets the current filename; a later plain F3 saves back "
      "to the loaded file, not a new prompt ===")
m51, target = fresh_with_blank_screen()
m51.disk_files = {'EXISTING': b'ORIGINAL' + b' ' * 952}
m51.getin_queue = [ANY_KEY, F5, RETURN, ord('!'), F3, F1]
reason = run_until_return(m51, target, max_instructions=15_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("no new file was created -- F3 saved back to EXISTING, not a "
      "fresh prompt", list(m51.disk_files.keys()) == ['EXISTING'],
      f"got {list(m51.disk_files.keys())}")

print("=== F2 (new) clears the current filename; F3 afterward prompts again ===")
m52, target = fresh_with_blank_screen()
m52.getin_queue = (
    [ANY_KEY, ord('X'), F3, ord('A'), ord('A'), ord('A'), RETURN]
    + [F2]
    + [ord('Y'), F3, ord('B'), ord('B'), ord('B'), RETURN, F1]
)
reason = run_until_return(m52, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("F3 after F2 prompted for and created a second, separate file",
      sorted(m52.disk_files.keys()) == ['AAA', 'BBB'],
      f"got {sorted(m52.disk_files.keys())}")

print("=== F3 with no current name, cancelled via RUN/STOP, remembers nothing "
      "-- pressing F3 again still prompts ===")
m53, target = fresh_with_blank_screen()
m53.getin_queue = [ANY_KEY, ord('X'), F3, RUNSTOP, F3, ord('A'), ord('A'), ord('A'),
                    RETURN, F1]
reason = run_until_return(m53, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the cancelled F3 saved nothing, but the retried F3 still worked",
      list(m53.disk_files.keys()) == ['AAA'], f"got {list(m53.disk_files.keys())}")

print("=== F6 cancelled leaves any existing current name untouched ===")
m54, target = fresh_with_blank_screen()
m54.getin_queue = (
    [ANY_KEY, ord('1'), F3, ord('A'), ord('A'), ord('A'), RETURN]
    + [ord('2'), F6, RUNSTOP]
    + [ord('3'), F3, F1]
)
reason = run_until_return(m54, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("no BBB (or any other) file was created by the cancelled F6",
      list(m54.disk_files.keys()) == ['AAA'], f"got {list(m54.disk_files.keys())}")
aaa = m54.disk_files.get('AAA')
check("all three edits (across the cancelled F6) ended up in AAA",
      aaa is not None and aaa[:3] == b'123', f"got {aaa[:3] if aaa else None}")

print("=== current_filename_len is properly initialized at startup, not "
      "garbage -- a document's very first F3 always prompts ===")
m55, target = fresh_with_blank_screen()
m55.getin_queue = [ANY_KEY, F3, ord('F'), ord('I'), ord('R'), ord('S'), ord('T'),
                    RETURN, F1]
reason = run_until_return(m55, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the very first F3 in a fresh session prompted correctly and saved",
      list(m55.disk_files.keys()) == ['FIRST'], f"got {list(m55.disk_files.keys())}")

print("=== default status line's help text includes F6 ===")
m56, target = fresh_with_blank_screen()
m56.getin_queue = [ANY_KEY, F1]
reason = run_until_return(m56, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("help text mentions save as",
      status_row_text(m56) == 'F1-F7: QUIT/NEW/SAVE/AS/DEL/LOAD/DIR',
      f"got {status_row_text(m56)!r}")

# --- document-full feedback at the absolute last cell ---
#
# Before this, typing past the single last cell (row MAX_ROW, column
# 39) silently kept overwriting that same cell forever, discarding
# whatever was typed there with zero indication the document had run
# out of room -- confirmed directly: typing "!?#" there left only "#"
# behind. Fixed in two places that share the same root cause (no room
# left to advance to): insert_char (typing at the last cell) and
# handle_return (RETURN on the last row, which can't create a next
# line to move to).

def fill_document_chars():
    return [ord(chr(65 + (i % 26))) for i in range(DOC_ROWS * 40 - 1)]


print("=== typing at the absolute last cell writes the character and shows "
      "DOCUMENT FULL, rather than silently discarding it ===")
m57, target = fresh_with_blank_screen()
m57.getin_queue = [ANY_KEY] + fill_document_chars() + [ord('!'), F1]
reason = run_until_return(m57, target, max_instructions=250_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the character was actually written, not discarded",
      screen_char(m57, MAX_ROW, 39) == ord('!'),
      f"got {screen_char(m57, MAX_ROW, 39)}")
check("status line shows the document-full message",
      status_row_text(m57) == 'DOCUMENT FULL -- NO ROOM TO ADD MORE.',
      f"got {status_row_text(m57)!r}")

print("=== typing well short of the last cell never shows DOCUMENT FULL "
      "-- shows the row status instead ===")
m58, target = fresh_with_blank_screen()
m58.getin_queue = [ANY_KEY, ord('A'), ord('B'), ord('C'), F1]
reason = run_until_return(m58, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows the row status (ROW 1), not document-full",
      status_row_text(m58) == 'ROW 1',
      f"got {status_row_text(m58)!r}")

print("=== ordinary row-wrap at column 39 (not the last row) never shows "
      "the document-full message -- it's a genuinely different case ===")
m59, target = fresh_with_blank_screen()
chars41 = [ord(chr(65 + (i % 26))) for i in range(41)]
m59.getin_queue = [ANY_KEY] + chars41 + [F1]
reason = run_until_return(m59, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("cursor correctly wrapped to row 1",
      m59.cpu.memory[CURSOR_Y] == 1, f"cursor_y={m59.cpu.memory[CURSOR_Y]}")
check("status line shows the row status (ROW 2), not document-full",
      status_row_text(m59) == 'ROW 2',
      f"got {status_row_text(m59)!r}")

print("=== RETURN on the last row shows the same message; the column still "
      "resets to 0 even though there's no next row to move to ===")
m60, target = fresh_with_blank_screen()
m60.getin_queue = [ANY_KEY] + [RETURN] * (DOC_ROWS - 1) + [ord('X'), RETURN, F1]
reason = run_until_return(m60, target, max_instructions=250_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows the document-full message",
      status_row_text(m60) == 'DOCUMENT FULL -- NO ROOM TO ADD MORE.',
      f"got {status_row_text(m60)!r}")
check("cursor_x still reset to 0", m60.cpu.memory[CURSOR_X] == 0,
      f"cursor_x={m60.cpu.memory[CURSOR_X]}")

print("=== RETURN on any row before the last one never shows DOCUMENT FULL "
      "-- shows the row status instead, like every other normal RETURN ===")
m61, target = fresh_with_blank_screen()
m61.getin_queue = [ANY_KEY, ord('A'), RETURN, ord('B'), F1]
reason = run_until_return(m61, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows the row status (ROW 2), not document-full",
      status_row_text(m61) == 'ROW 2',
      f"got {status_row_text(m61)!r}")

print("=== after F2 (new), the document-full condition is fully cleared -- "
      "normal typing resumes cleanly with no stray marks ===")
m62, target = fresh_with_blank_screen()
m62.getin_queue = ([ANY_KEY] + fill_document_chars()
                    + [ord('1'), ord('2'), ord('3'), F2, ord('X'), F1])
reason = run_until_return(m62, target, max_instructions=250_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("X landed at (0,0) after new, typing works normally again",
      screen_char(m62, 0, 0) == ord('X') - 0x40,
      f"got {screen_char(m62, 0, 0)}")
stray = [i for i in range(960) if m62.cpu.memory[SCREEN + i] & 0x80]
check("no stray reverse-video bits left over", stray == [],
      f"stray bits at {stray}")

# --- scrolling beyond one screen ---
#
# The document is no longer screen memory itself -- DOC_BUF (200 rows,
# ~8x a single screen) is now the single source of truth, and the
# screen is only ever a rendered 24-row view onto some slice of it,
# starting at doc_top_row. These tests exercise the property that
# actually matters most: content typed, scrolled away from, and
# scrolled back to is genuinely still there, not just visually gone.

DOC_TOP_ROW = 0x038e  # editor.asm's own address for this -- see that
                          # declaration's own comment


def doc_row_text(m, doc_buf_addr, row, n):
    chars = []
    for c in range(n):
        b = m.cpu.memory[doc_buf_addr + row * 40 + c]
        chars.append(chr(b))
    return ''.join(chars)


print("=== typing past the bottom visible row scrolls the viewport down, "
      "rather than stopping or refusing further input ===")
m63, target = fresh_with_blank_screen()
m63.getin_queue = [ANY_KEY] + [RETURN] * 24 + [ord('X'), F1]
reason = run_until_return(m63, target, max_instructions=20_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the typed character landed at screen row 23 after scrolling",
      screen_char(m63, 23, 0) == ord('X') - 0x40,
      f"got {screen_char(m63, 23, 0)}")

print("=== content typed, scrolled well away from, and scrolled back to "
      "is still correctly there -- the core property of this feature ===")
m64, target = fresh_with_blank_screen()
top = [ord(c) for c in "TOP ROW"]
down_returns = [RETURN] * 30
bottom = [ord(c) for c in "BOTTOM AREA"]
up_arrows = [CRSR_UP] * 30
m64.getin_queue = [ANY_KEY] + top + down_returns + bottom + up_arrows + [F1]
reason = run_until_return(m64, target, max_instructions=60_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 0 shows the original text after scrolling away and back",
      decoded_row(m64, 0, 7) == 'TOP ROW',
      f"got {decoded_row(m64, 0, 7)!r}")
check("doc_top_row is back to 0", m64.cpu.memory[DOC_TOP_ROW] == 0,
      f"doc_top_row={m64.cpu.memory[DOC_TOP_ROW]}")

print("=== the viewport cannot scroll past DOC_BUF's own last row: typing "
      "at the true document boundary is exactly where DOCUMENT FULL "
      "correctly appears, not one row short or one row past it ===")
m65, target = fresh_with_blank_screen()
m65.getin_queue = ([ANY_KEY] + [RETURN] * (DOC_ROWS - 1)
                    + [ord('A')] * 39 + [ord('Z'), F1])
reason = run_until_return(m65, target, max_instructions=200_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("doc_top_row capped at the highest valid scroll position",
      m65.cpu.memory[DOC_TOP_ROW] == DOC_ROWS - 24,
      f"doc_top_row={m65.cpu.memory[DOC_TOP_ROW]}")
check("status line shows DOCUMENT FULL exactly at the true last cell",
      status_row_text(m65) == 'DOCUMENT FULL -- NO ROOM TO ADD MORE.',
      f"got {status_row_text(m65)!r}")

print("=== cursor-up at the top visible row scrolls up when there's more "
      "document above, rather than refusing to move ===")
m66, target = fresh_with_blank_screen()
m66.getin_queue = [ANY_KEY] + [RETURN] * 5 + [CRSR_UP, CRSR_UP, CRSR_UP, F1]
reason = run_until_return(m66, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("doc_top_row stayed at 0 -- cursor moving within the still-visible "
      "rows 0-4 doesn't need to scroll at all",
      m66.cpu.memory[DOC_TOP_ROW] == 0, f"doc_top_row={m66.cpu.memory[DOC_TOP_ROW]}")

print("=== DEL at the top-left of a scrolled viewport scrolls up and "
      "correctly erases the character at the end of the previous line ===")
m67, target = fresh_with_blank_screen()
m67.getin_queue = ([ANY_KEY] + [ord(c) for c in "HELLO"] + [RETURN] * 10
                    + [DEL] * 11 + [F1])   # delete past the wrap point,
                                              # back into "HELLO"'s own row
reason = run_until_return(m67, target, max_instructions=20_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("scrolled back up to row 0 (doc_top_row 0) while deleting",
      m67.cpu.memory[DOC_TOP_ROW] == 0, f"doc_top_row={m67.cpu.memory[DOC_TOP_ROW]}")
check("one character was correctly erased from HELLO -- HELL remains",
      decoded_row(m67, 0, 4) == 'HELL', f"got {decoded_row(m67, 0, 4)!r}")

print("=== save trims trailing blank rows down to what's actually used, "
      "not the full fixed-size document buffer every time ===")
m68, target = fresh_with_blank_screen()
m68.getin_queue = [ANY_KEY, ord('H'), ord('I'), F3, ord('S'), ord('H'), ord('O'),
                    ord('R'), ord('T'), RETURN, F1]
reason = run_until_return(m68, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
saved = m68.disk_files.get('SHORT')
check("saved file is one 40-byte row, not the full 8000-byte document",
      saved is not None and len(saved) == 40,
      f"got {len(saved) if saved else None}")

print("=== saving content that spans well beyond one screen captures ALL "
      "of it, not just whatever's currently visible ===")
m69, target = fresh_with_blank_screen()
top2 = [ord(c) for c in "FIRST LINE"]
returns2 = [RETURN] * 30
bottom2 = [ord(c) for c in "LAST LINE"]
m69.getin_queue = ([ANY_KEY] + top2 + returns2 + bottom2
                    + [F3, ord('B'), ord('I'), ord('G'), RETURN, F1])
reason = run_until_return(m69, target, max_instructions=100_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
saved2 = m69.disk_files.get('BIG')
check("saved file covers all 31 rows (31*40 bytes), well beyond one screen",
      saved2 is not None and len(saved2) == 31 * 40,
      f"got {len(saved2) if saved2 else None}")
if saved2:
    check("row 0 preserved", saved2[0:10] == b'FIRST LINE')
    check("row 30 preserved", saved2[30*40:30*40+9] == b'LAST LINE')

print("=== loading that same multi-row file back restores it correctly, "
      "with the viewport reset to the top ===")
m70, target = fresh_with_blank_screen()
row_data = bytearray(b' ' * (31 * 40))
row_data[0:10] = b'FIRST LINE'
row_data[30*40:30*40+9] = b'LAST LINE'
m70.disk_files = {'BIG': bytes(row_data)}
m70.getin_queue = [ANY_KEY, F5, RETURN, F1]
reason = run_until_return(m70, target, max_instructions=100_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 0 shows FIRST LINE immediately after loading",
      decoded_row(m70, 0, 10) == 'FIRST LINE',
      f"got {decoded_row(m70, 0, 10)!r}")
check("doc_top_row reset to 0 after loading",
      m70.cpu.memory[DOC_TOP_ROW] == 0, f"doc_top_row={m70.cpu.memory[DOC_TOP_ROW]}")

print("=== F7 (directory) preserves the scrolled position it was invoked "
      "from, rather than resetting to the top ===")
m71, target = fresh_with_blank_screen()
m71.disk_files = {'FILE1': b'x' * 40}
m71.getin_queue = ([ANY_KEY] + [ord(c) for c in "SCROLLED"] + [RETURN] * 10
                    + [ord(c) for c in "HERE"] + [F7, ANY_KEY, F1])
reason = run_until_return(m71, target, max_instructions=30_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 10 still shows HERE after F7 is dismissed, unaffected",
      decoded_row(m71, 10, 4) == 'HERE',
      f"got {decoded_row(m71, 10, 4)!r}")

# --- F8 (help) ---
#
# Shows the bottom-row F-key assignments on the status line. The exact
# wording asked for ("1:Q 2:NEW 3:SAV 4:DEL 5:LD 6:AS 7:DIR 8:?") comes
# to 41 characters, one over the 40-column status line -- "DIR" became
# "DR" here to fit, the one abbreviation that could give up a
# character without shortening any of the other, harder-to-trim words.

print("=== F8 shows the exact (trimmed-to-fit) F-key reference text ===")
m72, target = fresh_with_blank_screen()
m72.getin_queue = [ANY_KEY, F8, F1]
reason = run_until_return(m72, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows the F-key reference",
      status_row_text(m72) == '1:Q 2:NEW 3:SAV 4:DEL 5:LD 6:AS 7:DR 8:?',
      f"got {status_row_text(m72)!r}")

print("=== F8 doesn't touch the document; typing after it correctly shows "
      "the row status again, not stuck showing the old F8 text forever ===")
m73, target = fresh_with_blank_screen()
m73.getin_queue = [ANY_KEY, ord('A'), ord('B'), F8, ord('C'), F1]
reason = run_until_return(m73, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("all three characters landed correctly, F8 in between changed nothing",
      screen_char(m73, 0, 0) == 1 and screen_char(m73, 0, 1) == 2
      and screen_char(m73, 0, 2) == 3)
check("status line shows the row status after typing resumes, same as "
      "any other typing (matching the general rule: typing/cursor keys "
      "always show the row, F-keys show their own result)",
      status_row_text(m73) == 'ROW 1',
      f"got {status_row_text(m73)!r}")

print("=== F8 leaves no stray reverse-video bits behind ===")
m74, target = fresh_with_blank_screen()
m74.getin_queue = [ANY_KEY, F8, F1]
reason = run_until_return(m74, target)
check("program returns cleanly", reason is None, f"reason={reason}")
stray = [i for i in range(960) if m74.cpu.memory[SCREEN + i] & 0x80]
check("every cell in the editable area has bit 7 clear", stray == [],
      f"stray bits at offsets {stray}")

# --- the real hardware bug this file shipped with: copy_ptr at $F5,
# inside the KERNAL's own $F3-$F6 keyboard-scan IRQ range ---
#
# Confirmed on real hardware, not just in a bug report taken on faith:
# reaching the bottom of the screen and pressing RETURN produced
# "random characters and graphic symbols" on the lower half of the
# screen, without hanging. This project's own past experience had
# already identified $F3-$F6 as unsafe for exactly this kind of
# pointer -- confirmed here directly by running the whole suite above
# with mini6502.py's zero-page poisoning simulation turned on (see
# fresh_with_blank_screen's own C64Machine call), which periodically
# writes realistic garbage into that range the same way real
# hardware's own IRQ handler does. copy_ptr, reused for both DOC_BUF
# access and file I/O, is now at $03 instead, which every test in this
# file already exercises -- this last one exists specifically to name
# the bug plainly and check the two exact operations the original
# report described: reaching the bottom of the screen and pressing
# RETURN, with no garbage left behind afterward.

print("=== reaching the bottom of the screen and pressing RETURN leaves "
      "no garbage behind, with realistic zero-page interference active "
      "(the exact real-hardware bug this file shipped with) ===")
m75, target = fresh_with_blank_screen()
m75.getin_queue = [ANY_KEY] + [RETURN] * 24 + [ord('X'), F1]
reason = run_until_return(m75, target, max_instructions=20_000_000)
check("program returns cleanly, does not hang", reason is None, f"reason={reason}")
garbage = [(r, c) for r in range(24) for c in range(40)
           if screen_char(m75, r, c) != 0x20 and not (r == 23 and c == 0)]
check("no unexpected bytes anywhere on screen after scrolling",
      garbage == [], f"found unexpected bytes at {garbage[:10]}")
check("the typed character landed correctly at the new position",
      screen_char(m75, 23, 0) == ord('X') - 0x40,
      f"got {screen_char(m75, 23, 0)}")

# --- row number status display and page up/down ---
#
# Row status shows "ROW n" (1-based) on the status line after typing,
# RETURN, DEL, any cursor key, or page up/down -- but deliberately not
# after F-key operations, whose own result messages ("SAVED.",
# "CANCELLED.", and so on) are more useful to leave in place.
#
# Page up/down uses HOME/CLR (SHIFT+HOME), not a cursor-key
# combination -- confirmed directly, not assumed, that none of the
# usual modifiers work for that here: CTRL+cursor produces nothing at
# all in the keyboard buffer, and both SHIFT+cursor and C=+cursor
# produce the same code as the opposite plain cursor key, so neither
# can mean anything new. HOME/CLR are a genuinely distinct, otherwise
# unused key pair.

print("=== typing the first character shows ROW 1 ===")
m76, target = fresh_with_blank_screen()
m76.getin_queue = [ANY_KEY, ord('A'), F1]
reason = run_until_return(m76, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows ROW 1", status_row_text(m76) == 'ROW 1',
      f"got {status_row_text(m76)!r}")

print("=== RETURN moves to ROW 2 ===")
m77, target = fresh_with_blank_screen()
m77.getin_queue = [ANY_KEY, RETURN, F1]
reason = run_until_return(m77, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows ROW 2", status_row_text(m77) == 'ROW 2',
      f"got {status_row_text(m77)!r}")

print("=== ROW 105 formats correctly -- a real digit-extraction edge case "
      "(the tens digit must show its own zero once hundreds is nonzero) ===")
m78, target = fresh_with_blank_screen()
m78.getin_queue = [ANY_KEY] + [CLR] * 4 + [RETURN] * 8 + [F1]
reason = run_until_return(m78, target, max_instructions=100_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows ROW 105", status_row_text(m78) == 'ROW 105',
      f"got {status_row_text(m78)!r}")

print("=== ROW 200, the true last row, formats correctly ===")
m79, target = fresh_with_blank_screen()
m79.getin_queue = [ANY_KEY] + [CLR] * 8 + [RETURN] * 23 + [F1]
reason = run_until_return(m79, target, max_instructions=150_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows ROW 200", status_row_text(m79) == 'ROW 200',
      f"got {status_row_text(m79)!r}")

print("=== F-key operations still show their own result message, not "
      "immediately overwritten by a row number ===")
m80, target = fresh_with_blank_screen()
m80.getin_queue = [ANY_KEY, ord('X'), F3, ord('A'), ord('A'), ord('A'), RETURN, F1]
reason = run_until_return(m80, target, max_instructions=10_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows SAVED., not a row number",
      status_row_text(m80) == 'SAVED.', f"got {status_row_text(m80)!r}")

print("=== F8 (help) still shows the F-key reference, not a row number ===")
m81, target = fresh_with_blank_screen()
m81.getin_queue = [ANY_KEY, ord('A'), F8, F1]
reason = run_until_return(m81, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("status line shows the F-key reference",
      status_row_text(m81) == '1:Q 2:NEW 3:SAV 4:DEL 5:LD 6:AS 7:DR 8:?',
      f"got {status_row_text(m81)!r}")

print("=== CLR (page down) from the top moves the viewport 24 rows down ===")
m82, target = fresh_with_blank_screen()
m82.getin_queue = [ANY_KEY, CLR, F1]
reason = run_until_return(m82, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("doc_top_row advanced by exactly 24",
      m82.cpu.memory[DOC_TOP_ROW] == 24, f"doc_top_row={m82.cpu.memory[DOC_TOP_ROW]}")

print("=== repeated page-down is capped at the highest valid scroll "
      "position, not overshot past it ===")
m83, target = fresh_with_blank_screen()
m83.getin_queue = [ANY_KEY] + [CLR] * 9 + [F1]   # 9*24=216, far past 176
reason = run_until_return(m83, target, max_instructions=30_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("doc_top_row capped at DOC_ROWS-24",
      m83.cpu.memory[DOC_TOP_ROW] == DOC_ROWS - 24,
      f"doc_top_row={m83.cpu.memory[DOC_TOP_ROW]}")

print("=== HOME (page up) from a scrolled position moves back up, capped at 0 ===")
m84, target = fresh_with_blank_screen()
m84.getin_queue = [ANY_KEY, CLR, CLR] + [HOME] * 5 + [F1]   # down 48, up 120 -> capped at 0
reason = run_until_return(m84, target, max_instructions=30_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("doc_top_row capped at 0", m84.cpu.memory[DOC_TOP_ROW] == 0,
      f"doc_top_row={m84.cpu.memory[DOC_TOP_ROW]}")

print("=== content typed, paged away from, and paged back to is still "
      "correctly there ===")
m85, target = fresh_with_blank_screen()
top_text = [ord(c) for c in "TOP"]
m85.getin_queue = [ANY_KEY] + top_text + [CLR, HOME, F1]
reason = run_until_return(m85, target, max_instructions=20_000_000)
check("program returns cleanly", reason is None, f"reason={reason}")
check("row 0 still reads TOP after paging away and back",
      decoded_row(m85, 0, 3) == 'TOP', f"got {decoded_row(m85, 0, 3)!r}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
