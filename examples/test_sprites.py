"""
End-to-end regression test for sprites.asm, using mini6502.py (see
mini6502.zip). This demo's whole reason for existing is to exercise
.incbin for real (c64asm-reference.md section 11) -- loading a
4-frame sprite animation from star_anim.bin, an external binary asset,
instead of hand-transcribing it as .byte data the way demo.asm's own
(single-frame, static) star does. The checks below confirm the bytes
.incbin pulled out of that file actually ended up in the right,
64-byte-aligned places in the assembled program -- not just that
assembly succeeded without error.

mini6502 doesn't simulate the VIC-II's raster line advancing on its
own, so testing anything past main_loop's first `jsr wait_frame` needs
the same technique test_demo.py/test_bounce.py/test_pong.py already
use: step the CPU one instruction at a time, and poke $d012 to the
value wait_frame polls for every time execution reaches it. See
run_frames_until_return/run_more_frames below.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_sprites.py
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

print("=== assembling sprites.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'sprites.asm', '-o', '/tmp/sprites_regress.prg',
     '--listing', '/tmp/sprites_regress.lst', '--lib-dir', 'lib'],
    capture_output=True, text=True)
check("sprites.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/sprites_regress.prg', 'rb') as f:
    data = f.read()
with open('/tmp/sprites_regress.lst') as f:
    listing = f.read()
with open('star_anim.bin', 'rb') as f:
    SHEET = f.read()

FRAME_ADDRS = [symbol_address(listing, f'frame{i}') for i in range(4)]
WAIT_FRAME = symbol_address(listing, 'wait_frame')
MAIN_LOOP = symbol_address(listing, 'main_loop')
XPOS = symbol_address(listing, 'xpos')
YPOS = symbol_address(listing, 'ypos')
FRAME_PTRS = symbol_address(listing, 'frame_ptrs')

SPRITE_PTR0 = 0x07F8
SPRITE_ENABLE = 0xD015

XMIN, XMAX = 24, 320
YMIN, YMAX = 50, 229

KEY_W = (0b11111101, 0b00000010)
KEY_A = (0b11111101, 0b00000100)
KEY_S = (0b11111101, 0b00100000)
KEY_D = (0b11111011, 0b00000100)
KEY_Q = (0b01111111, 0b01000000)
KEY_SPACE = (0b01111111, 0b00010000)

BUDGET = 300000


def fresh():
    m = C64Machine()
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


def run_frames_until_return(m, start_pc, max_instructions=BUDGET):
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
    m.cpu.pc = target
    m._sentinel = 0xFFFF
    m.cpu.push_word((m._sentinel - 1) & 0xFFFF)
    m._frame_count = 0


def run_more_frames(m, n):
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


def xpos16(m):
    return m.cpu.memory[XPOS] | (m.cpu.memory[XPOS + 1] << 8)


print("=== .incbin: each frame's bytes actually match the source file ===")
m0, target = fresh()
for i, addr in enumerate(FRAME_ADDRS):
    check(f"frame{i} is 64-byte aligned (a real sprite pointer requirement)",
          addr % 64 == 0, f"frame{i} = ${addr:04X}")
    assembled = bytes(m0.cpu.memory[addr:addr + 63])
    expected = SHEET[i * 63:(i + 1) * 63]
    check(f"frame{i}'s 63 bytes match star_anim.bin's own bytes at offset {i*63}",
          assembled == expected)
check("no two frames landed at the same address (offsets were applied correctly)",
      len(set(FRAME_ADDRS)) == 4, FRAME_ADDRS)

print("=== entry point and welcome text ===")
m1, target = fresh()
reason1 = run_frames_until_return(m1, target)
text1 = ''.join(m1.output_text)
check("welcome message printed", 'SPRITE ANIMATION FROM AN EXTERNAL' in text1,
      f"captured text: {text1!r}")
check("times out waiting -- correctly does not proceed with nothing pressed",
      reason1 is not None and 'exceeded' in reason1)

print("=== Q: exits cleanly and disables the sprite ===")
m2, target = fresh()
m2.press_key(*KEY_Q)
reason2 = run_frames_until_return(m2, target)
check("program returns cleanly", reason2 is None, f"reason={reason2}")
check("GOODBYE printed on exit", 'GOODBYE' in ''.join(m2.output_text))
check("sprite disabled on exit", m2.cpu.memory[SPRITE_ENABLE] & 1 == 0)

print("=== animation: cycles through all four frames' pointers in order ===")
m3, target = fresh()
m3.press_key(*KEY_SPACE)
start_execution(m3, target)
run_more_frames(m3, 1)   # get past wait_any_key
expected_ptrs = [FRAME_ADDRS[i] // 64 for i in range(4)]
check("starts on frame 0's pointer", m3.cpu.memory[SPRITE_PTR0] == expected_ptrs[0],
      f"got {m3.cpu.memory[SPRITE_PTR0]}, expected {expected_ptrs[0]}")
seen = [m3.cpu.memory[SPRITE_PTR0]]
for _ in range(3):
    run_more_frames(m3, 8)   # advances one frame every 8 ticks
    seen.append(m3.cpu.memory[SPRITE_PTR0])
check("cycles through frame pointers 0,1,2,3 in order over 24 frames",
      seen == expected_ptrs, f"saw {seen}, expected {expected_ptrs}")
run_more_frames(m3, 8)
check("wraps back around to frame 0 after the fourth frame",
      m3.cpu.memory[SPRITE_PTR0] == expected_ptrs[0])

print("=== W/A/S/D: movement and border clamping (same pattern as demo.asm) ===")
start_x, start_y = 172, 140
for key, axis, expect_delta, label in [
    (KEY_D, 'x', +1, 'D moves right'),
    (KEY_A, 'x', -1, 'A moves left'),
    (KEY_S, 'y', +1, 'S moves down'),
    (KEY_W, 'y', -1, 'W moves up'),
]:
    md, target = fresh()
    md.press_key(*KEY_SPACE)
    start_execution(md, target)
    run_more_frames(md, 1)
    md.release_all_keys()
    md.press_key(*key)
    run_more_frames(md, 5)
    if axis == 'x':
        check(label, xpos16(md) - start_x == expect_delta * 5,
              f"xpos went from {start_x} to {xpos16(md)}")
    else:
        check(label, md.cpu.memory[YPOS] - start_y == expect_delta * 5,
              f"ypos went from {start_y} to {md.cpu.memory[YPOS]}")

for key, axis, bound, label in [
    (KEY_A, 'x', XMIN, 'left edge'),
    (KEY_D, 'x', XMAX, 'right edge'),
    (KEY_W, 'y', YMIN, 'top edge'),
    (KEY_S, 'y', YMAX, 'bottom edge'),
]:
    mb, target = fresh()
    mb.press_key(*KEY_SPACE)
    start_execution(mb, target)
    run_more_frames(mb, 1)
    mb.release_all_keys()
    mb.press_key(*key)
    run_more_frames(mb, 400)
    if axis == 'x':
        check(f"stops exactly at {label}", xpos16(mb) == bound,
              f"xpos={xpos16(mb)}, expected {bound}")
    else:
        check(f"stops exactly at {label}", mb.cpu.memory[YPOS] == bound,
              f"ypos={mb.cpu.memory[YPOS]}, expected {bound}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
