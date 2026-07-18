"""
End-to-end regression test for demo.asm, using mini6502.py (see
mini6502.zip). Guards against three real bugs found in this library's
first release, all invisible from an assembled listing alone:

  1. .basic's SYS stub landing inside a library-included subroutine
     instead of the real entry point (fixed with "jmp start").
  2. Bitmap mode being switched on without clearing the bitmap or
     resetting screen memory's color data first, which reinterprets
     leftover text/CLS-fill bytes as scrambled colored pixels (fixed
     with CLEAR_BITMAP/SET_SCREEN_COLOR).
  3. read_joy2 reading garbage (a leftover keyboard column-select
     byte, misinterpreted as joystick input) once CIA_KEYBOARD_SETUP
     had left DDRA permanently in output mode -- read_joy2 and
     READ_KEY now each set DDRA to whatever they need before using the
     port, rather than relying on a one-time setup.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_demo.py
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


def symbol_address(listing_text, name):
    """Parses 'name = $XXXX' out of a --listing file's symbol table --
    used so this test doesn't hardcode addresses that shift whenever
    the library's own code size changes."""
    m = re.search(rf'^\s*{re.escape(name)}\s*=\s*\$([0-9A-Fa-f]+)\s*$',
                  listing_text, re.MULTILINE)
    if not m:
        sys.exit(f"symbol '{name}' not found in listing")
    return int(m.group(1), 16)


ASSEMBLER = find_c64asm()

print("=== assembling demo.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'demo.asm', '-o', '/tmp/demo_regress.prg',
     '--listing', '/tmp/demo_regress.lst'],
    capture_output=True, text=True)
check("demo.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/demo_regress.prg', 'rb') as f:
    data = f.read()
with open('/tmp/demo_regress.lst') as f:
    listing = f.read()

JOY_STATE = symbol_address(listing, 'joy_state')
ENGINE_PLAYING = symbol_address(listing, 'engine_playing')

# CLEAR_BITMAP alone is ~8000 byte-writes; comfortably covers setup
# plus a handful of main_loop iterations.
BUDGET = 300000


def fresh():
    m = C64Machine()
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


print("=== entry point: SYS must land at start:, not inside a library subroutine ===")
m, target = fresh()
# The first instruction at the SYS target must NOT immediately read/
# write through an uninitialized pointer the way print_msg would if
# entered directly -- concretely, check that execution reaches the
# welcome message correctly rather than returning almost immediately
# with little or no output (the "/7 and quits" symptom).
reason = m.run_until_return(target, max_instructions=BUDGET)
text = ''.join(m.output_text)
check("program does not return within a handful of instructions",
      m.cpu.instructions_run > 100,
      f"only ran {m.cpu.instructions_run} instructions -- likely landed "
      f"inside a subroutine instead of start:")
check("welcome message actually printed", 'WELCOME TO THE DEMO' in text,
      f"captured text was {text!r}")

print("=== bitmap mode: must be cleared, not left as garbage ===")
m2, target = fresh()
m2.press_key(0b11111011, 0b00000001)  # hold Y so it exits promptly
reason2 = m2.run_until_return(target, max_instructions=BUDGET)
check("program returns cleanly", reason2 is None, f"reason={reason2}")
check("GOODBYE printed on exit", 'GOODBYE' in ''.join(m2.output_text))

BITMAP = 0x2000
bitmap_bytes = m2.cpu.memory[BITMAP:BITMAP + 8000]
check("bitmap data is actually cleared to zero, not leftover garbage",
      all(b == 0 for b in bitmap_bytes))

SCREEN = 0x0400
screen_bytes = m2.cpu.memory[SCREEN:SCREEN + 1016]  # excludes the sprite
                                                       # pointer's own 8 bytes
check("screen memory is set to a real color byte, not leftover text/CLS bytes",
      all(b == 0b00010000 for b in screen_bytes),
      f"first few bytes: {list(screen_bytes[:8])}")

print("=== joystick corruption: read_joy2 must not read a leftover keyboard column ===")
m3, target = fresh()
m3.joystick2 = 0   # nothing held on the joystick
reason3 = m3.run_until_return(target, max_instructions=BUDGET)
check("still looping (correctly not exiting with nothing held)",
      reason3 is not None and 'exceeded' in reason3, f"reason={reason3}")
check("joy_state stays zero with nothing held -- CIA_KEYBOARD_SETUP's "
      "DDRA=all-output must not leak into a later read_joy2 call",
      m3.cpu.memory[JOY_STATE] == 0,
      f"joy_state=${m3.cpu.memory[JOY_STATE]:02X} (should be $00)")
gate_writes = [v for a, v in m3.io_writes if a == 0xD404]
check("no spurious sound triggered while idle",
      0x81 not in gate_writes,
      f"VOICE1_CTRL writes: {[hex(v) for v in gate_writes]}")

print("=== fire button: sound must still correctly trigger when actually held ===")
m4, target = fresh()
m4.joystick2 = 0b00010000
m4.press_key(0b11111011, 0b00000001)
reason4 = m4.run_until_return(target, max_instructions=BUDGET)
check("program returns cleanly", reason4 is None, f"reason={reason4}")
gate_writes4 = [v for a, v in m4.io_writes if a == 0xD404]
check("fire button correctly triggers the sound effect",
      0x81 in gate_writes4)

print("=== W key: engine sound must still correctly trigger when actually held ===")
m5, target = fresh()
m5.press_key(0b11111101, 0b00000010)  # W
m5.press_key(0b11111011, 0b00000001)  # Y, to exit promptly
reason5 = m5.run_until_return(target, max_instructions=BUDGET)
check("program returns cleanly", reason5 is None, f"reason={reason5}")
check("W key correctly triggers the engine sound",
      m5.cpu.memory[ENGINE_PLAYING] == 1)

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
