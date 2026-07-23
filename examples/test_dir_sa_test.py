"""
End-to-end regression test for dir_sa_test.asm, using mini6502.py (see
mini6502.zip). dir_sa_test.asm opens the disk directory ("$") with
three different secondary addresses (0, 2, 4) in turn and prints the
first 8 bytes each returns, side by side -- built to test a real gap
in this project's own verification: the "$" directory request was
always opened with secondary address 4, based on the general rule
that any value 2-14 is a valid data channel for an ordinary file, but
that specific claim -- that the special "$" request behaves the same
way regardless of which secondary address is used -- was assumed, not
actually confirmed. See dir_sa_test.asm's own header comment.

Running this tool on real hardware confirmed the gap was real: only
secondary address 0 (matching BASIC's own LOAD"$",8) produces a
well-formed listing; 2 and 4 both returned garbage. mini6502.py's own
_do_open was updated to match -- it didn't distinguish by secondary
address at all before this was confirmed, which is exactly why this
bug passed every test here right up until it was caught on real
hardware instead. See CHANGELOG.md for the full story. The checks
below confirm the simulation (and this project's own now-fixed
dir_demo.asm/dir_raw.asm/editor.asm) reflect that confirmed behavior,
not the old, too-permissive assumption.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_dir_sa_test.py
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

print("=== assembling dir_sa_test.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'dir_sa_test.asm', '-o', '/tmp/dir_sa_test_regress.prg',
     '--listing', '/tmp/dir_sa_test_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("dir_sa_test.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/dir_sa_test_regress.prg', 'rb') as f:
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


print("=== all three secondary addresses are tried, clearly labeled, in order ===")
m1, target = fresh()
m1.disk_files = {'HELLO': b'H' * 300}
reason = run_until_return(m1, target)
check("program returns cleanly", reason is None, f"reason={reason}")
text = ''.join(m1.output_text)
check("tries SA=$00", 'SA=$00' in text)
check("tries SA=$02", 'SA=$02' in text)
check("tries SA=$04", 'SA=$04' in text)
check("SA=$00 comes before SA=$02, which comes before SA=$04 (in that order)",
      text.index('SA=$00') < text.index('SA=$02') < text.index('SA=$04'))

print("=== SA=0 shows the well-formed sequence; SA=2 and SA=4 do not "
      "(confirmed against real hardware -- see CHANGELOG.md) ===")
expected_bytes = '01 04 01 01 00 00 12 22'
occurrences = text.count(expected_bytes)
check("the well-formed sequence appears exactly once, under SA=$00 only",
      occurrences == 1, f"found {occurrences} occurrences")
sa00_section = text.split('SA=$02')[0]
sa02_and_later = text.split('SA=$02', 1)[1]
check("SA=$00's own row contains the well-formed sequence",
      expected_bytes in sa00_section)
check("neither SA=$02 nor SA=$04 (everything after SA=$00) contains it",
      expected_bytes not in sa02_and_later)

print("=== ends with a clear prompt to compare the three rows ===")
check("final message present", 'COMPARE THE THREE ROWS ABOVE' in text)

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
