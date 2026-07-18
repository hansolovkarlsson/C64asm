"""
End-to-end regression test for bounce.asm, using mini6502.py (see
mini6502.zip). bounce.asm was refactored to use lib/graphics.inc
(BITMAP_MODE_ON, CLEAR_BITMAP, SET_SCREEN_COLOR, SPRITE_INIT,
wait_frame, sprite0_bounce_step) and lib/sound.inc (SID_INIT,
PLAY_SOUND) instead of its own local copies -- wait_frame and the
bounce-off-the-edges movement logic were originally written here
(wait_frame independently duplicated again, byte-for-byte, in pong.asm
and lander.asm), then generalized into the library; the bounce sound
itself reuses pong.asm's own already-proven play_wall_bounce envelope.

This test checks the setup bounce.asm's start: code produces (bitmap
mode correctly switched on and cleared, screen memory correctly
colored, sprite correctly pointed/colored/positioned/enabled, SID
volume set), then simulates many animation frames and checks the
ball's position stays within its configured bounds, actually reverses
direction at them (not just that it holds still or drifts
unboundedly), and -- the main thing this test exists to catch -- that
the bounce sound fires on every single frame a bound is hit and never
fires on any frame it isn't, across the whole run. sprite0_bounce_step
signals which axis (if either) bounced via the CPU's X/Y index
registers rather than anything graphics.inc and sound.inc would need
to share; see graphics.inc's own comment on the routine for the exact
convention this test is also implicitly checking bounce.asm follows
correctly.

Also guards against a real, since-fixed bug: sprite0_bounce_step
originally tracked X as a single byte (0-255), so XMAX could never be
set past 255 -- but the visible screen's right edge sits at X=344,
well past that. The result wasn't a crash or an assembly error, just a
sprite that visibly stopped bouncing well short of the right edge (and
similarly, before YMAX was separately recalibrated, a bit short of the
bottom edge too) -- exactly the kind of thing that's easy to miss
reading a listing and only obvious watching it actually run. This test
checks the ball's own far edge reaches the screen's true right (X=344)
and bottom (Y=250) edges, not just that it stays inside *some* bound,
and cross-checks SPRITE0_X/the X-MSB register ($d010 bit 0) stay
correctly in sync with the real 16-bit X position on every frame.

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
SPRITE_DATA = symbol_address(listing, 'sprite_data')
XPOS_ADDR = symbol_address(listing, 'xpos')  # 2 bytes: XPOS_ADDR, XPOS_ADDR+1
SPRITE_W, SPRITE_H = 24, 21
XMIN, XMAX = 24, 320   # XMAX > 255 -- needs the X-MSB register; this is
YMIN, YMAX = 50, 229    # the specific thing this test exists to check
TRUE_RIGHT_EDGE = 344   # the visible screen's actual right/bottom edges
TRUE_BOTTOM_EDGE = 250   # -- XMAX/YMAX above should place the sprite's
                           # own far edge exactly on these, not short of them

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
check("sprite 0 pointer set", mem[0x07f8] == SPRITE_DATA // 64,
      f"sprite_data=${SPRITE_DATA:04X}, expected pointer {SPRITE_DATA // 64}, got {mem[0x07f8]}")
check("sprite starts at XMIN", mem[0xd000] == XMIN, f"got {mem[0xd000]}")
check("sprite starts at YMIN", mem[0xd001] == YMIN, f"got {mem[0xd001]}")
check("X-MSB bit starts cleared (XMIN < 256)", mem[0xd010] & 1 == 0)
check("SID volume set (SID_INIT ran)", mem[0xd418] == 0x0f)

print("=== simulated animation: reaches the TRUE screen edges, sound fires exactly on impact ===")
x16s, ys = [], []
frame_count = 0
sound_this_frame = False
sound_frames = []          # frame indices where the bounce sound fired
false_positive_frames = []  # sound fired but neither axis was at a bound
missed_bounce_frames = []   # an axis was at a bound but sound didn't fire
msb_sync_errors = []        # SPRITE0_X/SPRITE_X_MSB out of sync with xpos
for _ in range(40_000_000):
    if m.cpu.pc == WAIT_FRAME:
        m.cpu.memory[0xd012] = 0xfb
    if m.cpu.pc == MAIN_LOOP:
        if frame_count > 0:
            x16 = mem[XPOS_ADDR] | (mem[XPOS_ADDR + 1] << 8)
            y = mem[0xd001]
            at_edge = x16 in (XMIN, XMAX) or y in (YMIN, YMAX)
            if sound_this_frame:
                sound_frames.append(frame_count)
            if sound_this_frame and not at_edge:
                false_positive_frames.append(frame_count)
            if at_edge and not sound_this_frame:
                missed_bounce_frames.append(frame_count)
            if mem[0xd000] != (x16 & 0xff) or (mem[0xd010] & 1) != (1 if x16 >= 256 else 0):
                msb_sync_errors.append(frame_count)
            x16s.append(x16)
            ys.append(y)
        frame_count += 1
        sound_this_frame = False
        if frame_count > 1200:
            break
    before = mem[0xd404]
    m.step()
    after = mem[0xd404]
    # $10001 = triangle waveform + gate on -- the specific byte
    # PLAY_SOUND's own call in bounce.asm writes last (see sound.inc);
    # a plain gate-off ($00, written first by every PLAY_SOUND call
    # to force-retrigger the envelope) doesn't count as "fired".
    if after != before and after & 0b00010001:
        sound_this_frame = True

check("1200 frames simulated without hanging", frame_count == 1201)
check("X position (16-bit) never leaves [XMIN, XMAX]",
      all(XMIN <= x <= XMAX for x in x16s),
      f"range was {min(x16s)}-{max(x16s)}")
check("Y position never leaves [YMIN, YMAX]",
      all(YMIN <= y <= YMAX for y in ys),
      f"range was {min(ys)}-{max(ys)}")
check("SPRITE0_X / X-MSB bit always in sync with the real 16-bit X position",
      len(msb_sync_errors) == 0,
      f"out of sync at frames {msb_sync_errors[:5]}")

check("ball's right edge actually reaches the true screen edge (X=344)",
      max(x16s) + SPRITE_W == TRUE_RIGHT_EDGE,
      f"max X seen was {max(x16s)}, sprite right edge at {max(x16s) + SPRITE_W}, expected {TRUE_RIGHT_EDGE}")
check("ball's bottom edge actually reaches the true screen edge (Y=250)",
      max(ys) + SPRITE_H == TRUE_BOTTOM_EDGE,
      f"max Y seen was {max(ys)}, sprite bottom edge at {max(ys) + SPRITE_H}, expected {TRUE_BOTTOM_EDGE}")
check("X-MSB bit actually gets set at some point (X crosses 256)",
      any(x >= 256 for x in x16s))

x_bounces = sum(1 for i in range(1, len(x16s) - 1) if (x16s[i] - x16s[i - 1]) * (x16s[i + 1] - x16s[i]) < 0)
y_bounces = sum(1 for i in range(1, len(ys) - 1) if (ys[i] - ys[i - 1]) * (ys[i + 1] - ys[i]) < 0)
check("ball actually reverses direction on X (not just static/drifting)", x_bounces >= 1,
      f"observed {x_bounces} reversals")
check("ball actually reverses direction on Y (not just static/drifting)", y_bounces >= 1,
      f"observed {y_bounces} reversals")

check("bounce sound fires at least once over the run", len(sound_frames) > 0)
check("bounce sound never fires without actually hitting a bound",
      len(false_positive_frames) == 0,
      f"false positives at frames {false_positive_frames}")
check("bounce sound fires on every single frame a bound is hit",
      len(missed_bounce_frames) == 0,
      f"missed at frames {missed_bounce_frames}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
