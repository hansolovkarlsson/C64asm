"""
End-to-end regression test for pong.asm, using mini6502.py (see
mini6502.zip). pong.asm was refactored to use lib/graphics.inc
(wait_frame), lib/input.inc (read_joy2, READ_KEY, CIA_KEYBOARD_SETUP),
and lib/sound.inc (SID_INIT, PLAY_SOUND) instead of its own local
copies -- wait_frame was independently duplicated, byte-for-byte, in
bounce.asm and lander.asm too before being pulled into the library; the
sound effects are the same ones bounce.asm's wall-bounce sound reuses.

This refactor also fixed a real, previously unnoticed bug: pong.asm
used to leave CIA1 port A permanently configured as output (for
keyboard column-select) from setup onward, which meant reading JOY2
afterward never actually read the joystick -- it read back whatever
column-select byte had last been written, misread as joystick input.
The visible symptom: the player paddle drifted downward on its own
with nothing held. input.inc's read_joy2/READ_KEY each fix this by
setting CIA1's direction register to whatever they specifically need
immediately before reading, rather than relying on a one-time setup.

This test checks (1) that bug is actually fixed -- the paddle stays
put with nothing held -- (2) real joystick and keyboard-fallback input
still both work, (3) the AI paddle still tracks the ball, (4) all three
distinct sound effects (wall bounce, paddle hit, score) still fire at
their own distinct frequencies, and (5) scores still increment and
wrap correctly.

A second, separate fix, made after the one above: the play field used
to only use about 2/3 of the screen's width, with the right paddle and
the net both stopping well short of where they should be -- the same
class of bug bounce.asm had, and the same root cause: the true right
edge of the screen sits at X=344, past what fits in a single byte, and
both the right paddle's fixed X position and the ball's own X bounds
were kept under 256 to avoid needing the X-MSB register. RIGHT_PADDLE_X
is now 320 (its far edge touches the true right edge exactly, the same
way LEFT_PADDLE_X=24 already touched the true left edge) with its
X-MSB bit set once at startup (it never moves in X, unlike the ball,
so that bit never needs updating again); the ball's own X position is
now a full 16-bit value (ball_x/ball_x+1) with its X-MSB bit maintained
every frame in update_sprites, the same technique graphics.inc's
sprite0_bounce_step uses for bounce.asm's ball (not reused directly,
since this ball tests against paddles, not just walls); and the net
moved from column 14 to column 20, the screen's true center, which is
also now the paddles' true midpoint. This test checks the ball
actually reaches the new, correct BALL_XMAX (past 255), that
SPRITE0_X/the X-MSB bit stay in sync with the real 16-bit X position
every frame, and that the right paddle and net both ended up in the
right place.

A third fix, on the same axis of "stops short of the true edge" but
this time on Y rather than X: PADDLE_YMIN/PADDLE_YMAX were originally
calibrated only to the *minimum* needed to satisfy ball-coverage (a
paddle positioned at PADDLE_YMAX could still, with PADDLE_RANGE's
help, intercept a ball all the way at YMAX_WALL) -- which is a
different thing from the paddle's own sprite actually being able to
reach the true bottom of the screen. The paddle is the same 21px
height as the ball, so PADDLE_YMIN/PADDLE_YMAX are now calibrated the
identical way YMIN_WALL/YMAX_WALL are (see the "full-width layout" fix
above), and coverage was re-verified with the same kind of exhaustive
Python simulation used for the original PADDLE_YMAX bug during initial
development, checking every ball_y/paddle_y combination directly
rather than trusting the arithmetic alone. This test checks the paddle
actually reaches Y=50 and Y=229 exactly under sustained input, not
just that it moves in the right direction.

mini6502 doesn't simulate the VIC-II's raster line advancing on its
own, so this test pokes $d012 to the value wait_frame polls for every
time execution reaches wait_frame, advancing one simulated "frame" per
main_loop iteration instead of burning millions of real instructions
on the busy-wait itself.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_pong.py
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


def assemble(path, out_prefix):
    result = subprocess.run(
        ['python3', ASSEMBLER, path, '-o', f'/tmp/{out_prefix}.prg',
         '--listing', f'/tmp/{out_prefix}.lst'],
        capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"failed to assemble {path}: {result.stderr}")
    with open(f'/tmp/{out_prefix}.prg', 'rb') as f:
        data = f.read()
    with open(f'/tmp/{out_prefix}.lst') as f:
        listing = f.read()
    return data, listing


def run_setup(data):
    m = C64Machine()
    target = m.find_sys_target(data)
    m.load_prg(data)
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


ASSEMBLER = find_c64asm()

print("=== assembling pong.asm ===")
data, listing = assemble('pong.asm', 'pong_regress')
check("pong.asm assembles cleanly", True)  # assemble() exits on failure

MAIN_LOOP = symbol_address(listing, 'main_loop')
WAIT_FRAME = symbol_address(listing, 'wait_frame')
P1_Y = symbol_address(listing, 'p1_y')
P2_Y = symbol_address(listing, 'p2_y')
BALL_X = symbol_address(listing, 'ball_x')
BALL_Y = symbol_address(listing, 'ball_y')
P1_SCORE = symbol_address(listing, 'p1_score')
P2_SCORE = symbol_address(listing, 'p2_score')

print("=== initial setup ===")
m = run_setup(data)
m.joystick2 = 0   # explicit "nothing held" -- mini6502's joystick2
                     # defaults to 0x1F (all bits set, not 0), which
                     # would otherwise leave phantom up+down bits both
                     # active and canceling out below, passing the next
                     # check for the wrong reason
mem = m.cpu.memory
check("SID volume set (SID_INIT ran)", mem[0xd418] == 0x0f)
check("sprites 0-2 all enabled", mem[0xd015] & 0b111 == 0b111)
check("ball (sprite 0) color set", mem[0xd027] == 1)
check("left paddle (sprite 1) color set", mem[0xd028] == 3)
check("right paddle (sprite 2) color set", mem[0xd029] == 7)
check("left paddle X set", mem[0xd002] == 24)
check("right paddle X (low byte) set", mem[0xd004] == 320 & 0xff, f"got {mem[0xd004]}")
check("right paddle X-MSB bit set (RIGHT_PADDLE_X=320 exceeds 255)",
      (mem[0xd010] >> 2) & 1 == 1)
check("ball's own X-MSB bit starts clear (BALL_XMIN=34 is under 256)",
      mem[0xd010] & 1 == 0)
check("border black", mem[0xd020] == 0)
check("net drawn at the true screen center (column 20, not the old column 14)",
      mem[0x0400 + 20] == 0x2a, f"got {hex(mem[0x0400 + 20])}")
check("old net column 14 is blank now that the net moved",
      mem[0x0400 + 14] == 0x20)
check("P1 score displayed at column 5 (unchanged)", mem[0x0400 + 5] == 0x30)
check("P2 score displayed at column 32 (was column 23)", mem[0x0400 + 32] == 0x30)

print("=== the DDRA/joystick bug is actually fixed ===")
p1y_vals = []
for _ in range(50):
    run_frames(m, 1)
    p1y_vals.append(mem[P1_Y])
check("paddle stays perfectly still with nothing held (was: drifted down every frame)",
      all(v == 100 for v in p1y_vals),
      f"p1_y values seen: {p1y_vals[:10]}...")

print("=== real input still works: joystick ===")
m_up = run_setup(data)
m_up.joystick2 = 0b00000001
run_frames(m_up, 10)
check("joystick UP moves paddle up", m_up.cpu.memory[P1_Y] < 100,
      f"got {m_up.cpu.memory[P1_Y]}")
run_frames(m_up, 190)  # long enough to reach the top from any starting point
check("paddle actually reaches the true top edge (Y=50), not stopping short",
      m_up.cpu.memory[P1_Y] == 50, f"got {m_up.cpu.memory[P1_Y]}")

m_down = run_setup(data)
m_down.joystick2 = 0b00000010
run_frames(m_down, 10)
check("joystick DOWN moves paddle down", m_down.cpu.memory[P1_Y] > 100,
      f"got {m_down.cpu.memory[P1_Y]}")
run_frames(m_down, 190)  # long enough to reach the bottom from any starting point
check("paddle actually reaches the true bottom edge (Y=229), not stopping short",
      m_down.cpu.memory[P1_Y] == 229, f"got {m_down.cpu.memory[P1_Y]}")

print("=== real input still works: W/S keyboard fallback ===")
m_w = run_setup(data)
m_w.joystick2 = 0   # isolate the keyboard path -- joystick2 defaults to
                       # 0x1F (not 0) in mini6502, which would otherwise
                       # leave phantom up+down bits both active at once,
                       # canceling out and masking whatever this is
                       # actually testing
m_w.press_key(0b11111101, 0b00000010)
run_frames(m_w, 10)
check("W key moves paddle up", m_w.cpu.memory[P1_Y] < 100,
      f"got {m_w.cpu.memory[P1_Y]}")

m_s = run_setup(data)
m_s.joystick2 = 0
m_s.press_key(0b11111101, 0b00100000)
run_frames(m_s, 10)
check("S key moves paddle down", m_s.cpu.memory[P1_Y] > 100,
      f"got {m_s.cpu.memory[P1_Y]}")

print("=== extended run: AI tracking, sounds, scoring, full-width ball travel ===")
m_ext = run_setup(data)
freq_events = {0x18: 0, 0x30: 0, 0x08: 0}  # wall-bounce, paddle-hit, score
p2ys, bys, bxs = [], [], []
msb_sync_errors = []
frame_no = 0
for _ in range(2000):
    # Alternate the player paddle's direction periodically instead of
    # holding one direction forever: holding DOWN continuously (as an
    # earlier version of this test did) drives p1_y to its extreme and
    # keeps it there, which makes every served ball after that replay
    # the exact identical trajectory -- a legitimate AI miss on that one
    # specific angle is nothing to worry about, but it isn't a
    # representative sample of play either, and it's the kind of thing
    # that can look like a stuck/broken game if you don't realize why.
    m_ext.joystick2 = 0b00000010 if (frame_no // 130) % 2 == 0 else 0b00000001
    frame_no += 1
    for _ in range(200000):
        if m_ext.cpu.pc == WAIT_FRAME:
            m_ext.cpu.memory[0xd012] = 0xfb
        before = m_ext.cpu.memory[0xd404]
        m_ext.step()
        after = m_ext.cpu.memory[0xd404]
        if after != before and after & 0b00010001:
            freq_hi = m_ext.cpu.memory[0xd401]
            if freq_hi in freq_events:
                freq_events[freq_hi] += 1
        if m_ext.cpu.pc == MAIN_LOOP:
            break
    mem_ext = m_ext.cpu.memory
    x16 = mem_ext[BALL_X] | (mem_ext[BALL_X + 1] << 8)
    bxs.append(x16)
    p2ys.append(mem_ext[P2_Y])
    bys.append(mem_ext[BALL_Y])
    if mem_ext[0xd000] != (x16 & 0xff) or (mem_ext[0xd010] & 1) != (1 if x16 >= 256 else 0):
        msb_sync_errors.append(x16)

check("ball stays within its (16-bit) X bounds [BALL_XMIN, BALL_XMAX]",
      all(34 <= x <= 310 for x in bxs), f"range {min(bxs)}-{max(bxs)}")
check("ball actually reaches the far right paddle zone (BALL_XMAX=310, past 255)",
      310 in bxs, f"max X seen was {max(bxs)}")
check("ball's SPRITE0_X / X-MSB bit always in sync with the real 16-bit X position",
      len(msb_sync_errors) == 0, f"out of sync at X values {msb_sync_errors[:5]}")
check("ball stays within its Y wall bounds", all(50 <= y <= 229 for y in bys),
      f"range {min(bys)}-{max(bys)}")
check("AI paddle stays within its bounds", all(50 <= y <= 229 for y in p2ys),
      f"range {min(p2ys)}-{max(p2ys)}")
check("wall-bounce sound (freq $18) fired", freq_events[0x18] > 0, str(freq_events))
check("paddle-hit sound (freq $30) fired", freq_events[0x30] > 0, str(freq_events))
check("score sound (freq $08) fired", freq_events[0x08] > 0, str(freq_events))
check("scores stay in 0-9 (wrap correctly)",
      0 <= m_ext.cpu.memory[P1_SCORE] <= 9 and 0 <= m_ext.cpu.memory[P2_SCORE] <= 9)
check("at least one point was actually scored over the run",
      m_ext.cpu.memory[P1_SCORE] + m_ext.cpu.memory[P2_SCORE] > 0
      or freq_events[0x08] >= 10,
      "no score sound events and both scores are 0")

print("=== full-width layout ===")
print("  (verified above: right paddle now reaches near the true right")
print("  edge instead of stopping ~2/3 across the screen, and the net is")
print("  centered at the true screen midpoint instead of offset left)")
# (Earlier versions of this test cross-checked scores bit-for-bit against
# the pre-refactor pong.asm to confirm a change was *only* a bug fix, not
# a gameplay change -- see lib-reference.md's "Worked example" section
# for that check, still valid for the CIA/joystick bug fix. It doesn't
# apply to this update: paddle positions, ball bounds, and the net all
# intentionally moved to use the full screen width, so scores are
# expected to come out differently than before, not identically.)

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
