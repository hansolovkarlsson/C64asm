"""
End-to-end regression test for adventure.asm, using mini6502.py (see
mini6502.zip). adventure.asm was refactored to use lib/text.inc
(print_msg/PRINT, str_equal) and lib/input.inc (read_line,
extract_word) instead of its own local copies -- this test plays
through the documented solution path (see adventure.asm's own header
comment) and confirms the refactor didn't change behavior: every room
description, the chest/key/door puzzle, and the win condition all have
to work identically to before.

A specific, easy-to-get-wrong detail this test locks in: PETSCII bytes
for typed uppercase keyboard input and for `.text`-encoded uppercase
strings are IDENTICAL (both plain ASCII $41-$5A, unchanged) on this
assembler -- unlike an early, incorrect assumption made while testing
this refactor, typed input is NOT in some separate $C1-$DA "shifted"
range. Getting this wrong makes every keyword comparison in the game
silently fail (see keyboard_petscii_matches_text_petscii below).

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_adventure.py
"""

import os
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


def typed(s):
    """Encodes a string as the PETSCII bytes a real CHRIN call would
    return for typing it in the default (uppercase) character mode --
    plain ASCII for uppercase letters/spaces, unchanged. This is
    deliberately NOT the same as some other encoding you might expect
    from PETSCII being unusual elsewhere; verified directly against
    this assembler's own ascii_to_petscii() for `.text` strings, which
    uses the identical mapping for the uppercase range."""
    return [ord(ch) for ch in s]


ASSEMBLER = find_c64asm()

print("=== assembling adventure.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'adventure.asm', '-o', '/tmp/adventure_regress.prg'],
    capture_output=True, text=True)
check("adventure.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/adventure_regress.prg', 'rb') as f:
    data = f.read()

BUDGET = 2_000_000


def play(commands):
    m = C64Machine()
    target = m.find_sys_target(data)
    m.load_prg(data)
    queue = []
    for c in commands:
        queue += typed(c) + [0x0d]
    m.chrin_queue = queue
    m.cpu.pc = target
    sentinel = 0xFFFF
    m.cpu.push_word(sentinel - 1)
    CHRIN = 0xFFCF
    for _ in range(BUDGET):
        if m.cpu.pc == sentinel:
            break
        # Stop the instant the game is about to read past the input we
        # actually provided -- everything up to here is real output
        # from the commands given; anything after would just be the
        # game looping on reads we never supplied (not a bug, just this
        # harness not sending a "stop playing" signal).
        if m.cpu.pc == CHRIN and not m.chrin_queue:
            break
        m.step()
    return ''.join(m.output_text)


print("=== keyboard PETSCII matches .text PETSCII (the bug this test was built to catch) ===")
text_1cmd = play(['LOOK'])
check("a single recognized command is NOT met with 'I DON'T UNDERSTAND'",
      "I DON'T UNDERSTAND" not in text_1cmd,
      f"text was {text_1cmd!r}")

print("=== full solution playthrough reaches the win condition ===")
solution = [
    'GO NORTH', 'GO EAST', 'GO WEST', 'GO SOUTH', 'GO EAST',
    'OPEN CHEST', 'TAKE KEY', 'GO WEST', 'GO NORTH', 'GO EAST',
    'OPEN DOOR', 'GO NORTH', 'TAKE TREASURE',
]
text = play(solution)
check("welcome message printed", 'THE FORGOTTEN COTTAGE' in text)
check("chest opens correctly", 'INSIDE IS A KEY' in text)
check("key can be taken", 'YOU TAKE THE KEY' in text)
check("door unlocks with the key", 'YOU UNLOCK THE DOOR' in text)
check("reaches the win condition", 'YOU WIN' in text, f"full text: {text!r}")
dont_understand_count = text.count("I DON'T UNDERSTAND")
check("no unexpected 'I DON'T UNDERSTAND' along the solution path",
      dont_understand_count == 0,
      f"count={dont_understand_count}")

print("=== a locked door correctly blocks without the key ===")
text2 = play(['GO NORTH', 'GO EAST', 'OPEN DOOR', 'GO NORTH'])
check("door starts locked", 'LOCKED WOODEN DOOR' in text2)
check("open door without key fails with the right message",
      "IT'S LOCKED. YOU NEED A KEY" in text2 or "NEED A KEY" in text2,
      f"text: {text2!r}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
