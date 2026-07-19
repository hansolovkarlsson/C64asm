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

Also covers demo.asm's later additions: waiting for a keypress before
switching to bitmap mode (so the welcome/instructions text is actually
readable), a visible sprite (a star, not blank pixels), and -- most
recently -- W/A/S/D moving that star around the screen with a border
stop and a short sound on each successful move, Q to exit.

mini6502 doesn't simulate the VIC-II's raster line advancing on its
own, so testing anything past main_loop's first `jsr wait_frame` needs
the same technique test_bounce.py/test_pong.py already use: step the
CPU one instruction at a time, and poke $d012 to the value wait_frame
polls for every time execution reaches it, advancing one simulated
"frame" per main_loop iteration instead of hanging forever on the
busy-wait itself (or burning millions of real instructions on it, on
real hardware where the raster line genuinely does advance every
cycle). See run_frames_until_return below.

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
SPRITE_DATA = symbol_address(listing, 'sprite_data')
WAIT_FRAME = symbol_address(listing, 'wait_frame')
MAIN_LOOP = symbol_address(listing, 'main_loop')
XPOS = symbol_address(listing, 'xpos')   # 2 bytes: XPOS, XPOS+1
YPOS = symbol_address(listing, 'ypos')

SPRITE0_X, SPRITE0_Y = 0xD000, 0xD001
SPRITE_ENABLE = 0xD015
SPRITE_PTR0 = 0x07F8
VIC_CTRL1 = 0xD011
VOICE1_CTRL = 0xD404

XMIN, XMAX = 24, 320
YMIN, YMAX = 50, 229

KEY_W = (0b11111101, 0b00000010)
KEY_A = (0b11111101, 0b00000100)
KEY_S = (0b11111101, 0b00100000)
KEY_D = (0b11111011, 0b00000100)
KEY_Q = (0b01111111, 0b01000000)
KEY_SPACE = (0b01111111, 0b00010000)   # neutral -- doesn't share a
                                          # column or row with any of
                                          # W/A/S/D/Q above

# CLEAR_BITMAP alone is ~8000 byte-writes; comfortably covers setup
# plus a handful of main_loop iterations.
BUDGET = 300000


def fresh():
    m = C64Machine()
    m.joystick2 = 0   # nothing held -- C64Machine's own default (0x1F) has
                         # fire "held", which would add spurious VOICE1_CTRL
                         # writes to every test below that doesn't
                         # specifically care about the joystick; the tests
                         # that DO care about it set this explicitly anyway
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


def run_frames_until_return(m, start_pc, max_instructions=BUDGET):
    """Like m.run_until_return, but also pokes $d012 to the value
    wait_frame polls for every time execution reaches it -- see this
    file's own docstring for why that's necessary here. Returns the
    halt reason string (None if it returned normally)."""
    SENTINEL = 0xFFFF
    m.cpu.push_word((SENTINEL - 1) & 0xFFFF)
    m.cpu.pc = start_pc
    m.cpu.halted = False
    m.cpu.instructions_run = 0
    while not m.cpu.halted and m.cpu.instructions_run < max_instructions:
        if m.cpu.pc == WAIT_FRAME:
            m.cpu.memory[0xd012] = 0xfb
        if m.cpu.pc == SENTINEL:
            return None
        m.step()
    if m.cpu.halted:
        return m.cpu.halt_reason
    return f"exceeded {max_instructions} instructions without returning"


def start_execution(m, target):
    """Sets m up to begin executing at target, ready for one or more
    run_more_frames calls. Must be called exactly once per machine,
    before the first run_more_frames call."""
    m.cpu.pc = target
    m._sentinel = 0xFFFF
    m.cpu.push_word((m._sentinel - 1) & 0xFFFF)
    m._frame_count = 0


def run_more_frames(m, n):
    """Runs n MORE main_loop iterations, continuing from wherever m's
    execution currently is -- unlike naively resetting cpu.pc and
    re-pushing the sentinel return address on every call (an earlier,
    broken version of this test did exactly that), which would
    silently restart the whole program from target instead of
    actually continuing, discarding whatever state (like the star's
    position) the previous run_more_frames call had already reached."""
    stop_at = m._frame_count + n
    for _ in range(20_000_000):
        if m.cpu.pc == WAIT_FRAME:
            m.cpu.memory[0xd012] = 0xfb
        if m.cpu.pc == MAIN_LOOP:
            m._frame_count += 1
            if m._frame_count > stop_at:
                return
        if m.cpu.pc == m._sentinel:
            return
        m.step()


print("=== entry point: SYS must land at start:, not inside a library subroutine ===")
m, target = fresh()
# The first instruction at the SYS target must NOT immediately read/
# write through an uninitialized pointer the way print_msg would if
# entered directly -- concretely, check that execution reaches the
# welcome message correctly rather than returning almost immediately
# with little or no output (the "/7 and quits" symptom). This run
# never presses a key, so it now deliberately blocks at wait_any_key
# and times out rather than returning -- that's fine here, since
# neither assertion below depends on the run actually finishing; see
# the dedicated wait_any_key test further down for that.
reason = run_frames_until_return(m, target)
text = ''.join(m.output_text)
check("program does not return within a handful of instructions",
      m.cpu.instructions_run > 100,
      f"only ran {m.cpu.instructions_run} instructions -- likely landed "
      f"inside a subroutine instead of start:")
check("welcome message actually printed", 'WELCOME TO THE DEMO' in text,
      f"captured text was {text!r}")

print("=== bitmap mode: must be cleared, not left as garbage ===")
m2, target = fresh()
m2.press_key(*KEY_Q)  # hold Q so it exits promptly
reason2 = run_frames_until_return(m2, target)
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
m3.press_key(*KEY_SPACE)  # get past the wait_any_key prompt at start
                             # without disturbing this test's real point,
                             # which is what happens in main_loop with
                             # nothing *relevant* held
reason3 = run_frames_until_return(m3, target)
check("still looping (correctly not exiting with nothing held)",
      reason3 is not None and 'exceeded' in reason3, f"reason={reason3}")
check("joy_state stays zero with nothing held -- CIA_KEYBOARD_SETUP's "
      "DDRA=all-output must not leak into a later read_joy2 call",
      m3.cpu.memory[JOY_STATE] == 0,
      f"joy_state=${m3.cpu.memory[JOY_STATE]:02X} (should be $00)")
gate_writes = [v for a, v in m3.io_writes if a == VOICE1_CTRL]
check("no spurious sound triggered while idle",
      0x81 not in gate_writes,
      f"VOICE1_CTRL writes: {[hex(v) for v in gate_writes]}")

print("=== fire button: sound must still correctly trigger when actually held ===")
m4, target = fresh()
m4.joystick2 = 0b00010000
m4.press_key(*KEY_Q)  # to exit promptly
reason4 = run_frames_until_return(m4, target)
check("program returns cleanly", reason4 is None, f"reason={reason4}")
gate_writes4 = [v for a, v in m4.io_writes if a == VOICE1_CTRL]
check("fire button correctly triggers the sound effect",
      0x81 in gate_writes4)

print("=== wait_any_key: bitmap mode must not start until a key is pressed ===")
# This is the actual bug this file's wait_any_key call was added to
# fix: previously, bitmap mode began immediately after the welcome
# text was printed, wiping it off the screen before there was any
# real chance to read it.
m6, target = fresh()
reason6 = run_frames_until_return(m6, target)
check("times out waiting -- correctly does not proceed with nothing pressed",
      reason6 is not None and 'exceeded' in reason6, f"reason={reason6}")
check("bitmap mode was NOT entered while still waiting for a keypress",
      m6.cpu.memory[VIC_CTRL1] & 0b00100000 == 0,
      f"VIC_CTRL1=${m6.cpu.memory[VIC_CTRL1]:02X} (bit 5 should be clear)")
check("instructions and directions were printed before the wait",
      'USE W,A,S,D TO MOVE THE STAR' in ''.join(m6.output_text) and
      'PRESS ANY KEY TO CONTINUE' in ''.join(m6.output_text))

m7, target = fresh()
m7.press_key(*KEY_SPACE)  # an unrelated key, just to get past the prompt
reason7 = run_frames_until_return(m7, target)
check("bitmap mode WAS entered once a key was pressed",
      m7.cpu.memory[VIC_CTRL1] & 0b00100000 != 0,
      f"VIC_CTRL1=${m7.cpu.memory[VIC_CTRL1]:02X} (bit 5 should be set)")

print("=== sprite: must actually be visible, not blank ===")
sprite_bytes = m7.cpu.memory[SPRITE_DATA:SPRITE_DATA + 63]
check("sprite data is not all zero bytes",
      any(b != 0 for b in sprite_bytes),
      "sprite_data is entirely $00 -- nothing will be visible on screen "
      "even with SPRITE_ENABLE set")
check("sprite 0 is enabled", m7.cpu.memory[SPRITE_ENABLE] & 1 == 1)
check("sprite 0's pointer correctly targets sprite_data",
      m7.cpu.memory[SPRITE_PTR0] * 64 == SPRITE_DATA,
      f"pointer -> ${m7.cpu.memory[SPRITE_PTR0]*64:04X}, expected ${SPRITE_DATA:04X}")


def xpos16(m):
    return m.cpu.memory[XPOS] | (m.cpu.memory[XPOS + 1] << 8)


print("=== W/A/S/D: each direction moves the star by one pixel per frame ===")
start_x, start_y = 172, 140   # matches demo.asm's own SPRITE_INIT/xpos/ypos setup
for key, axis, expect_delta, label in [
    (KEY_D, 'x', +1, 'D moves right'),
    (KEY_A, 'x', -1, 'A moves left'),
    (KEY_S, 'y', +1, 'S moves down'),
    (KEY_W, 'y', -1, 'W moves up'),
]:
    md, target = fresh()
    md.press_key(*KEY_SPACE)
    start_execution(md, target)
    run_more_frames(md, 1)   # get past wait_any_key/into bitmap mode
    md.release_all_keys()
    md.press_key(*key)
    run_more_frames(md, 5)   # a handful of frames holding the direction
    if axis == 'x':
        moved = xpos16(md) - start_x
        check(label, moved == expect_delta * 5,
              f"xpos went from {start_x} to {xpos16(md)} over 5 frames "
              f"(expected delta {expect_delta*5})")
        check(f"{label}: SPRITE0_X stays in sync",
              md.cpu.memory[SPRITE0_X] == xpos16(md) & 0xFF)
    else:
        moved = md.cpu.memory[YPOS] - start_y
        check(label, moved == expect_delta * 5,
              f"ypos went from {start_y} to {md.cpu.memory[YPOS]} over 5 "
              f"frames (expected delta {expect_delta*5})")
        check(f"{label}: SPRITE0_Y stays in sync",
              md.cpu.memory[SPRITE0_Y] == md.cpu.memory[YPOS])

print("=== border check: movement stops exactly at the edge, doesn't run past it ===")
for key, axis, bound, label in [
    (KEY_A, 'x', XMIN, 'left edge (A, XMIN)'),
    (KEY_D, 'x', XMAX, 'right edge (D, XMAX)'),
    (KEY_W, 'y', YMIN, 'top edge (W, YMIN)'),
    (KEY_S, 'y', YMAX, 'bottom edge (S, YMAX)'),
]:
    mb, target = fresh()
    mb.press_key(*KEY_SPACE)
    start_execution(mb, target)
    run_more_frames(mb, 1)
    mb.release_all_keys()
    mb.press_key(*key)
    # Comfortably more frames than needed to travel from the start
    # position to any edge (worst case is XMAX-start_x ~= 150 pixels).
    run_more_frames(mb, 400)
    if axis == 'x':
        check(f"stops exactly at {label}", xpos16(mb) == bound,
              f"xpos={xpos16(mb)}, expected {bound}")
    else:
        check(f"stops exactly at {label}", mb.cpu.memory[YPOS] == bound,
              f"ypos={mb.cpu.memory[YPOS]}, expected {bound}")

print("=== movement sound: plays while moving, falls silent at the border ===")
ms, target = fresh()
ms.press_key(*KEY_SPACE)
start_execution(ms, target)
run_more_frames(ms, 1)
ms.release_all_keys()
ms.press_key(*KEY_D)
run_more_frames(ms, 3)   # a few frames of genuine movement
gate_writes_moving = [v for a, v in ms.io_writes if a == VOICE1_CTRL]
check("sound gate fires while the star is actually moving",
      0x11 in gate_writes_moving,
      f"VOICE1_CTRL writes: {[hex(v) for v in gate_writes_moving]}")

ms2, target = fresh()
ms2.press_key(*KEY_SPACE)
start_execution(ms2, target)
run_more_frames(ms2, 1)
ms2.release_all_keys()
ms2.press_key(*KEY_D)
run_more_frames(ms2, 400)   # long enough to reach and sit at XMAX
check("star actually reached the right edge for this check to be meaningful",
      xpos16(ms2) == XMAX)
ms2.io_writes.clear()
run_more_frames(ms2, 3)   # a few MORE frames, continuing from right where
                             # the machine already is (sitting at XMAX) --
                             # not restarting from scratch, which would
                             # reset xpos back to the start position and
                             # make this check meaningless
gate_writes_at_edge = [v for a, v in ms2.io_writes if a == VOICE1_CTRL]
check("sound does NOT fire while held against the edge (no movement happened)",
      0x11 not in gate_writes_at_edge,
      f"VOICE1_CTRL writes after reaching the edge: {[hex(v) for v in gate_writes_at_edge]}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
