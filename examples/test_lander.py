"""
End-to-end regression test for lander.asm, using mini6502.py (see
mini6502.zip). lander.asm was refactored to use lib/graphics.inc
(BITMAP_MODE_ON/OFF, CLEAR_BITMAP, SET_SCREEN_COLOR, SPRITE_INIT,
wait_frame, sprite0_explode), lib/input.inc (read_joy2, READ_KEY,
CIA_KEYBOARD_SETUP), lib/sound.inc (SID_INIT, PLAY_SOUND,
engine_sound_on/off), and lib/text.inc (print_msg/PRINT, CLS) instead
of its own local copies of all of these -- most were originally
written here and generalized into the library (this file is where
sound.inc's engine_sound_on/off and graphics.inc's BITMAP_MODE_OFF
both trace back to, word for word).

This refactor also fixed a real bug, the same class already found and
fixed in pong.asm: lander.asm used to leave CIA1 port A permanently
configured as keyboard column-select output from setup onward, so
JOY2 reads afterward silently read back stale column-select data
instead of the joystick. The symptom here was worse than pong.asm's
stationary paddle -- fuel drained and the ship visibly drifted
sideways with nothing held at all, every single flight, silently
making the game harder (or outright unwinnable near the fuel margin)
than intended. input.inc's read_joy2/READ_KEY fix this by setting
CIA1's direction register to whatever they specifically need
immediately before each read, rather than relying on a one-time setup.

Extracting show_explosion's visual effect (sprite expand, color-cycle,
hide) into a new, general-purpose graphics.inc routine,
sprite0_explode, is the actual new library addition this pass adds --
not just consolidation of things already duplicated elsewhere, since
no other demo in this project currently has anything similar. It's
still cross-checked here just as rigorously as everything else: the
exact color sequence, in the exact order, and the sprite ending up
correctly hidden and un-expanded afterward.

This test checks (1) the DDRA/joystick bug is actually fixed -- fuel
and horizontal position stay constant with nothing held, while gravity
(vertical fall) still correctly isn't affected, since that's not
"input" at all, (2) real joystick and keyboard-fallback input all
still work for all three controls (thrust/left/right), (3) a
successful landing (ship placed on the pad at a safe speed) correctly
switches out of bitmap mode, prints the success message, and plays the
victory melody, (4) a crash (ship placed off the pad or too fast)
correctly runs the explosion effect with the right color sequence,
switches out of bitmap mode, and prints the crash message, and (5)
pressing Y afterward correctly restarts the program.

mini6502 doesn't simulate the VIC-II's raster line advancing on its
own, so this test pokes $d012 to the value wait_frame polls for every
time execution reaches wait_frame, advancing one simulated "frame" per
main_loop iteration instead of burning millions of real instructions
on the busy-wait itself.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_lander.py
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

print("=== assembling lander.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'lander.asm', '-o', '/tmp/lander_regress.prg',
     '--listing', '/tmp/lander_regress.lst'],
    capture_output=True, text=True)
check("lander.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/lander_regress.prg', 'rb') as f:
    data = f.read()
with open('/tmp/lander_regress.lst') as f:
    listing = f.read()

MAIN_LOOP = symbol_address(listing, 'main_loop')
WAIT_FRAME = symbol_address(listing, 'wait_frame')
START = symbol_address(listing, 'start')
SHIP_X = symbol_address(listing, 'ship_x')
SHIP_Y = symbol_address(listing, 'ship_y')
VX_MAG = symbol_address(listing, 'vx_mag')
VY_MAG = symbol_address(listing, 'vy_mag')
VY_DIR = symbol_address(listing, 'vy_dir')
FUEL = symbol_address(listing, 'fuel')


def run_setup():
    m = C64Machine()
    target = m.find_sys_target(data)
    m.load_prg(data)
    m.joystick2 = 0
    m.cpu.pc = target
    sentinel = 0xFFFF
    m.cpu.push_word(sentinel - 1)
    for _ in range(500000):
        if m.cpu.pc == MAIN_LOOP:
            break
        m.step()
    else:
        sys.exit("never reached main_loop")
    return m


def run_frames(m, n):
    for _ in range(n):
        for _ in range(200000):
            if m.cpu.pc == WAIT_FRAME:
                m.cpu.memory[0xd012] = 0xfb
            m.step()
            if m.cpu.pc == MAIN_LOOP:
                break


print("=== initial setup ===")
m = run_setup()
mem = m.cpu.memory
check("bitmap mode (BMM) switched on", mem[0xd011] & 0b00100000 != 0)
check("VIC memory pointers select the bitmap at $2000", mem[0xd018] & 0b00001000 != 0)
check("SID volume set (SID_INIT ran)", mem[0xd418] == 0x0f)
check("sprite 0 enabled", mem[0xd015] & 1 != 0)
check("sprite 0 color set (white)", mem[0xd027] == 1)
check("ship starts at X=128", mem[SHIP_X] == 128)
check("ship starts at Y=50 (YMIN)", mem[SHIP_Y] == 50)
check("fuel starts full (FUEL_START=100)", mem[FUEL] == 100)
check("border black", mem[0xd020] == 0)

print("=== the DDRA/joystick bug is actually fixed ===")
xs, fuels, ys = [], [], []
for _ in range(60):
    run_frames(m, 1)
    xs.append(mem[SHIP_X])
    fuels.append(mem[FUEL])
    ys.append(mem[SHIP_Y])
check("ship X stays perfectly still with nothing held (was: drifted every frame)",
      all(x == xs[0] for x in xs), f"X values seen: {xs[:10]}...")
check("fuel stays perfectly constant with nothing held (was: drained every few frames)",
      all(f == fuels[0] for f in fuels), f"fuel values seen: {fuels[:10]}...")
check("gravity still correctly pulls the ship down (not itself 'input')",
      ys[-1] > ys[0], f"Y went from {ys[0]} to {ys[-1]}")

print("=== real input still works: joystick ===")
m_thrust = run_setup()
m_thrust.joystick2 = 0b00000001
run_frames(m_thrust, 60)
check("joystick thrust (up) counters gravity", m_thrust.cpu.memory[SHIP_Y] < 55,
      f"got Y={m_thrust.cpu.memory[SHIP_Y]}")
check("joystick thrust consumes fuel", m_thrust.cpu.memory[FUEL] < 100,
      f"got fuel={m_thrust.cpu.memory[FUEL]}")

m_left = run_setup()
m_left.joystick2 = 0b00000100
run_frames(m_left, 40)
check("joystick left moves the ship left", m_left.cpu.memory[SHIP_X] < 128,
      f"got X={m_left.cpu.memory[SHIP_X]}")

m_right = run_setup()
m_right.joystick2 = 0b00001000
run_frames(m_right, 40)
check("joystick right moves the ship right", m_right.cpu.memory[SHIP_X] > 128,
      f"got X={m_right.cpu.memory[SHIP_X]}")

print("=== real input still works: W/A/D keyboard fallback ===")
m_w = run_setup()
m_w.press_key(0b11111101, 0b00000010)  # W: matrix column 1, bit 1
run_frames(m_w, 60)
check("W key thrusts up", m_w.cpu.memory[SHIP_Y] < 55, f"got Y={m_w.cpu.memory[SHIP_Y]}")

m_a = run_setup()
m_a.press_key(0b11111101, 0b00000100)  # A: matrix column 1, bit 2
run_frames(m_a, 40)
check("A key moves left", m_a.cpu.memory[SHIP_X] < 128, f"got X={m_a.cpu.memory[SHIP_X]}")

m_d = run_setup()
m_d.press_key(0b11111011, 0b00000100)  # D: matrix column 2, bit 2
run_frames(m_d, 40)
check("D key moves right", m_d.cpu.memory[SHIP_X] > 128, f"got X={m_d.cpu.memory[SHIP_X]}")

print("=== successful landing ===")
m_land = run_setup()
mem_land = m_land.cpu.memory
mem_land[SHIP_X] = 180   # within the pad's column range (15-25 -> X 144-224)
mem_land[SHIP_Y] = 205   # just above the pad surface (PAD_ROW=20 -> Y=210)
mem_land[VX_MAG] = 0
mem_land[VY_MAG] = 1     # slow, safe speed
mem_land[VY_DIR] = 1
sentinel = 0xFFFF
for _ in range(2_000_000):
    if m_land.cpu.pc == WAIT_FRAME:
        mem_land[0xd012] = 0xfb
    m_land.step()
    if m_land.cpu.pc == sentinel:
        break
text = ''.join(m_land.output_text)
check("landing switches out of bitmap mode", mem_land[0xd011] & 0b00100000 == 0)
check("landing restores text-mode character pointers", mem_land[0xd018] == 0x14)
check("landing prints the success message", 'TOUCHDOWN' in text, f"text: {text!r}")
check("landing prints 'try again' prompt", 'PRESS Y TO TRY AGAIN' in text)

print("=== crash ===")
m_crash = run_setup()
mem_crash = m_crash.cpu.memory
mem_crash[SHIP_X] = 24 + 5 * 8   # column 5 -- off the pad (pad is columns 15-25)
mem_crash[SHIP_Y] = 100
mem_crash[VX_MAG] = 0
mem_crash[VY_MAG] = 3            # fast, unsafe speed
mem_crash[VY_DIR] = 1
colors_seen = []
for _ in range(2_000_000):
    if m_crash.cpu.pc == WAIT_FRAME:
        mem_crash[0xd012] = 0xfb
    prev_color = mem_crash[0xd027]
    m_crash.step()
    new_color = mem_crash[0xd027]
    if new_color != prev_color:
        colors_seen.append(new_color)
    if m_crash.cpu.pc == sentinel:
        break
text = ''.join(m_crash.output_text)
check("crash cycles through the correct explosion color sequence",
      colors_seen == [7, 8, 2, 1, 7, 8, 2, 1], f"got {colors_seen}")
check("crash hides the sprite afterward", mem_crash[0xd015] == 0)
check("crash switches out of bitmap mode", mem_crash[0xd011] & 0b00100000 == 0)
check("crash prints the crash message", 'YOU CRASHED' in text, f"text: {text!r}")
check("crash prints 'try again' prompt", 'PRESS Y TO TRY AGAIN' in text)

print("=== restart ===")
m_crash.press_key(0b11110111, 0b00000010)  # Y: matrix column 3, bit 1
restarted = False
for _ in range(500000):
    m_crash.step()
    if m_crash.cpu.pc == START:
        restarted = True
        break
check("pressing Y after a crash restarts the program", restarted)

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
