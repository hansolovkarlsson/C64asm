"""
End-to-end regression test for music_demo.asm, using mini6502.py (see
mini6502.zip). This demo's whole reason for existing is to exercise
lib/music.inc's two-voice sequencer for real -- the checks below
confirm the actual SID register values match the actual intended
melody and bass line, note by note, computed independently here from
the same equal-temperament/PAL-clock formula the demo's own note
tables were generated from, rather than just checking that assembly
succeeds or that some plausible-looking sound happens.

mini6502 doesn't simulate the VIC-II's raster line advancing on its
own, so testing anything past main_loop's first `jsr wait_frame` needs
the same technique test_demo.py/test_bounce.py/test_pong.py/
test_sprites.py already use: step the CPU one instruction at a time,
and poke $d012 to the value wait_frame polls for every time execution
reaches it. See run_frames_until_return/run_more_frames below.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_music_demo.py
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

print("=== assembling music_demo.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'music_demo.asm', '-o', '/tmp/music_demo_regress.prg',
     '--listing', '/tmp/music_demo_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("music_demo.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/music_demo_regress.prg', 'rb') as f:
    data = f.read()
with open('/tmp/music_demo_regress.lst') as f:
    listing = f.read()

WAIT_FRAME = symbol_address(listing, 'wait_frame')
MAIN_LOOP = symbol_address(listing, 'main_loop')

VOICE1_FREQ_LO, VOICE1_FREQ_HI, VOICE1_CTRL = 0xD400, 0xD401, 0xD404
VOICE2_FREQ_LO, VOICE2_FREQ_HI, VOICE2_CTRL = 0xD407, 0xD408, 0xD40B

KEY_SPACE = (0b01111111, 0b00010000)
KEY_Q = (0b01111111, 0b01000000)

# Independently computed, from the same equal-temperament/PAL-clock
# formula the demo's own note tables were generated from -- not copied
# from the .asm source, so a transcription mistake in either place
# would actually be caught here rather than both sides agreeing by
# construction.
PAL_CLOCK = 985248.0


def sid_freq(note_name, octave):
    notes = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    midi = (octave + 1) * 12 + notes.index(note_name)
    hz = 440.0 * (2 ** ((midi - 69) / 12))
    return round(hz * 16777216 / PAL_CLOCK)


_phrase1 = [('C', 4)] * 2 + [('G', 4)] * 2 + [('A', 4)] * 2 + [('G', 4)] + [None]
_phrase2 = [('F', 4)] * 2 + [('E', 4)] * 2 + [('D', 4)] * 2 + [('C', 4)] + [None]
_phrase3 = [('G', 4)] * 2 + [('F', 4)] * 2 + [('E', 4)] * 2 + [('D', 4)] + [None]
EXPECTED_MELODY = _phrase1 + _phrase2 + _phrase3 + _phrase3 + _phrase1 + _phrase2
EXPECTED_BASS = [('C', 3), ('F', 3), ('G', 3), ('G', 3), ('C', 3), ('F', 3)]
NOTE_FRAMES = 15
BASS_FRAMES = 120


def fresh():
    m = C64Machine(simulate_zp_poisoning=False)
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


def start_execution(m, target):
    m.cpu.pc = target
    m._sentinel = 0xFFFF
    m.cpu.push_word((m._sentinel - 1) & 0xFFFF)
    m._frame_count = 0


def run_more_frames(m, n):
    stop_at = m._frame_count + n
    for _ in range(30_000_000):
        if m.cpu.pc == WAIT_FRAME:
            m.cpu.memory[0xd012] = 0xfb
        if m.cpu.pc == MAIN_LOOP:
            m._frame_count += 1
            if m._frame_count > stop_at:
                return
        if m.cpu.pc == m._sentinel:
            return
        m.step()


def run_frames_until_return(m, start_pc, max_instructions=300000):
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
    return m.cpu.halt_reason if m.cpu.halted else f"exceeded {max_instructions} instructions"


print("=== melody voice (voice 1): all 48 notes, in order ===")
m, target = fresh()
m.press_key(*KEY_SPACE)  # past wait_any_key
start_execution(m, target)
run_more_frames(m, 1)
for i, note in enumerate(EXPECTED_MELODY):
    fl = m.cpu.memory[VOICE1_FREQ_LO]
    fh = m.cpu.memory[VOICE1_FREQ_HI]
    ctrl = m.cpu.memory[VOICE1_CTRL]
    if note is None:
        check(f"melody note {i} is a rest (gate off)", ctrl & 1 == 0,
              f"ctrl=${ctrl:02X}")
    else:
        want = sid_freq(*note)
        got = fl | (fh << 8)
        check(f"melody note {i} ({note[0]}{note[1]}) frequency correct",
              got == want, f"got ${got:04X}, want ${want:04X}")
        check(f"melody note {i} gate is on", ctrl & 1 == 1, f"ctrl=${ctrl:02X}")
    run_more_frames(m, NOTE_FRAMES)

print("=== melody loops back to note 0 after all 48 notes ===")
m2, target = fresh()
m2.press_key(*KEY_SPACE)
start_execution(m2, target)
run_more_frames(m2, 1 + len(EXPECTED_MELODY) * NOTE_FRAMES)
fl = m2.cpu.memory[VOICE1_FREQ_LO]
fh = m2.cpu.memory[VOICE1_FREQ_HI]
want = sid_freq(*EXPECTED_MELODY[0])
check("melody wraps back to note 0 (C4) after a full cycle",
      (fl | (fh << 8)) == want, f"got ${fl | (fh<<8):04X}, want ${want:04X}")

print("=== bass voice (voice 2): all 6 phrase notes, in order ===")
m3, target = fresh()
m3.press_key(*KEY_SPACE)
start_execution(m3, target)
run_more_frames(m3, 1)
for i, note in enumerate(EXPECTED_BASS):
    fl = m3.cpu.memory[VOICE2_FREQ_LO]
    fh = m3.cpu.memory[VOICE2_FREQ_HI]
    want = sid_freq(*note)
    got = fl | (fh << 8)
    check(f"bass note {i} ({note[0]}{note[1]}) frequency correct",
          got == want, f"got ${got:04X}, want ${want:04X}")
    run_more_frames(m3, BASS_FRAMES)

print("=== border color pulses once per new melody note ===")
m4, target = fresh()
m4.press_key(*KEY_SPACE)
start_execution(m4, target)
border_before = m4.cpu.memory[0xd020]
run_more_frames(m4, 2)
border_after = m4.cpu.memory[0xd020]
check("border color changed when the first note started",
      border_after != border_before, f"before={border_before}, after={border_after}")

print("=== Q: exits cleanly and silences both voices ===")
m5, target = fresh()
m5.press_key(*KEY_Q)
reason = run_frames_until_return(m5, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("voice 1 silenced on exit", m5.cpu.memory[VOICE1_CTRL] & 1 == 0)
check("voice 2 silenced on exit", m5.cpu.memory[VOICE2_CTRL] & 1 == 0)
check("GOODBYE printed on exit", 'GOODBYE' in ''.join(m5.output_text))

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
