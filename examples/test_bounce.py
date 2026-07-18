"""
End-to-end regression test for bounce.asm, using mini6502.py (see
mini6502.zip). bounce.asm was refactored to use lib/graphics.inc
(BITMAP_MODE_ON, CLEAR_BITMAP, SET_SCREEN_COLOR, SPRITE_INIT,
wait_frame, sprite0_bounce_step) instead of its own local copies --
wait_frame and the bounce-off-the-edges movement logic were originally
written here (wait_frame independently duplicated again, byte-for-byte,
in pong.asm and lander.asm), then generalized into the library.

This test checks the setup bounce.asm's start: code produces (bitmap
mode correctly switched on and cleared, screen memory correctly
colored, sprite correctly pointed/colored/positioned/enabled), then
simulates many animation frames and checks the ball's position stays
within its configured bounds and actually reverses direction at them
(not just that it holds still or drifts unboundedly) -- confirming
sprite0_bounce_step's extraction didn't change bounce.asm's behavior.

mini6502 doesn't simulate the VIC-II's raster line advancing on its
own, so this test pokes $d012 to the value wait_frame polls for every
time execution reaches wait_frame, advancing one simulated "frame" per
main_loop iteration instead of burning millions of real instructions
on the busy-wait itself.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_bounce.py
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
    m = re.search(rf'^\s*{re.escape(name)}\s*=\s*\$([0-9A-Fa-f]+)\s*$',
                  listing_text, re.MULTILINE)
    if not m:
        sys.exit(f"symbol '{name}' not found in listing")
    return int(m.group(1), 16)


ASSEMBLER = find_c64asm()

print("=== assembling bounce.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'bounce.asm', '-o', '/tmp/bounce_regress.prg',
     '--listing', '/tmp/bounce_regress.lst'],
    capture_output=True, text=True)
check("bounce.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/bounce_regress.prg', 'rb') as f:
    data = f.read()
with open('/tmp/bounce_regress.lst') as f:
    listing = f.read()

MAIN_LOOP = symbol_address(listing, 'main_loop')
WAIT_FRAME = symbol_address(listing, 'wait_frame')
XMIN, XMAX = 24, 250
YMIN, YMAX = 50, 220

m = C64Machine()
target = m.find_sys_target(data)
m.load_prg(data)
m.cpu.pc = target
sentinel = 0xFFFF
m.cpu.push_word(sentinel - 1)

print("=== initial setup, before the first frame ===")
for _ in range(200000):
    if m.cpu.pc == MAIN_LOOP:
        break
    m.step()
else:
    sys.exit("never reached main_loop")

mem = m.cpu.memory
check("bitmap mode (BMM) switched on", mem[0xd011] & 0b00100000 != 0)
check("VIC memory pointers select the bitmap at $2000", mem[0xd018] & 0b00001000 != 0)
check("bitmap data fully cleared", all(b == 0 for b in mem[0x2000:0x2000 + 8000]))
check("screen/color area correctly filled", all(b == 0b00010110 for b in mem[0x0400:0x0400 + 1000]))
check("border color set", mem[0xd020] == 6)
check("sprite 0 enabled", mem[0xd015] & 1 != 0)
check("sprite 0 color set", mem[0xd027] == 1)
check("sprite 0 pointer set", mem[0x07f8] == 0x0900 // 64)
check("sprite starts at XMIN", mem[0xd000] == XMIN, f"got {mem[0xd000]}")
check("sprite starts at YMIN", mem[0xd001] == YMIN, f"got {mem[0xd001]}")

print("=== simulated animation: bounces within bounds on both axes ===")
xs, ys = [], []
frame_count = 0
for _ in range(20_000_000):
    if m.cpu.pc == WAIT_FRAME:
        m.cpu.memory[0xd012] = 0xfb
    if m.cpu.pc == MAIN_LOOP:
        frame_count += 1
        xs.append(m.cpu.memory[0xd000])
        ys.append(m.cpu.memory[0xd001])
        if frame_count >= 600:
            break
    m.step()

check("600 frames simulated without hanging", frame_count == 600)
check("X position never leaves [XMIN, XMAX]",
      all(XMIN <= x <= XMAX for x in xs),
      f"range was {min(xs)}-{max(xs)}")
check("Y position never leaves [YMIN, YMAX]",
      all(YMIN <= y <= YMAX for y in ys),
      f"range was {min(ys)}-{max(ys)}")

x_bounces = sum(1 for i in range(1, len(xs) - 1) if (xs[i] - xs[i - 1]) * (xs[i + 1] - xs[i]) < 0)
y_bounces = sum(1 for i in range(1, len(ys) - 1) if (ys[i] - ys[i - 1]) * (ys[i + 1] - ys[i]) < 0)
check("ball actually reverses direction on X (not just static/drifting)", x_bounces >= 1,
      f"observed {x_bounces} reversals")
check("ball actually reverses direction on Y (not just static/drifting)", y_bounces >= 1,
      f"observed {y_bounces} reversals")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
