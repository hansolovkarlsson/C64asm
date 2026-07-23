"""
End-to-end regression test for dir_raw.asm, using mini6502.py (see
mini6502.zip). dir_raw.asm exists purely as a diagnostic tool -- it
prints the raw bytes OPEN/CHRIN actually return for a directory
listing, with no interpretation, specifically to debug dir_demo.asm
not behaving as expected on real hardware/VICE. See dir_raw.asm's own
header comment for the full reasoning.

One of these checks exists because of a real bug caught while writing
this file, not a hypothetical one: an early version read READST's
value, then called PRINT (which clobbers A internally) before actually
printing that value, so what appeared on screen was leftover garbage
from the print routine, not the real status byte -- self-contradictory
output ("STOPPED -- READST NONZERO: 00") that would have been actively
misleading for someone trying to use this tool to diagnose a different
problem. Fixed by saving the value across the PRINT call, and the test
below checks that the reported value is real, not just present.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_dir_raw.py
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

print("=== assembling dir_raw.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'dir_raw.asm', '-o', '/tmp/dir_raw_regress.prg',
     '--listing', '/tmp/dir_raw_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("dir_raw.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/dir_raw_regress.prg', 'rb') as f:
    data = f.read()


def fresh():
    m = C64Machine(simulate_zp_poisoning=False)
    target = m.find_sys_target(data)
    m.load_prg(data)
    return m, target


def run_until_return(m, start_pc, max_instructions=2_000_000):
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


print("=== a normal directory dump shows the exact expected byte sequence ===")
m1, target = fresh()
m1.disk_files = {'HELLO': b'H' * 300}
reason = run_until_return(m1, target)
check("program returns cleanly", reason is None, f"reason={reason}")
text = ''.join(m1.output_text)
check("reports OPEN success", 'CARRY CLEAR (SUCCESS)' in text)
check("reports READST as 00 before any CHRIN", 'READST BEFORE ANY CHRIN: 00' in text)
check("first bytes are the fake load address (01 04)", '01 04 01 01 00 00' in text)
check("disk name text follows in hex (12 22 56 49 52 54 55 41 4C = "
      "reverse-on, quote, VIRTUAL)", '12 22 56 49 52 54 55 41 4C' in text)
check("ends by reporting a nonzero READST, not a fabricated/leftover value",
      re.search(r'STOPPED -- READST NONZERO: (?!00)[0-9A-F]{2}', text) is not None,
      "the exact bug this test exists to catch: printing garbage instead "
      "of the real status byte")
check("reports DONE at the end", text.rstrip().endswith('DONE'))

print("=== OPEN failure (no drive/disk present) is reported with its real error code ===")
m2, target = fresh()
m2.device_present = False
reason = run_until_return(m2, target, max_instructions=500_000)
check("program returns cleanly, does not hang", reason is None, f"reason={reason}")
text2 = ''.join(m2.output_text)
check("reports OPEN failure", 'CARRY SET (FAILED)' in text2)
check("reports the real error code (05, DEVICE NOT PRESENT)",
      'ERROR CODE: 05' in text2, f"got: {text2!r}")
check("no directory bytes were printed after a failed OPEN",
      'READST BEFORE' not in text2)

print("=== the byte dump stops at 128 bytes on a very large directory, not unbounded ===")
m3, target = fresh()
m3.disk_files = {f'FILE{i}': b'x' * 100 for i in range(50)}   # a large directory
reason = run_until_return(m3, target, max_instructions=5_000_000)
check("program returns cleanly, does not run away", reason is None, f"reason={reason}")
check("reports hitting the 128-byte cap",
      'REACHED 128 BYTES' in ''.join(m3.output_text))

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
