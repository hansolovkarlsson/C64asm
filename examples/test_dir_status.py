"""
End-to-end regression test for dir_status.asm, using mini6502.py (see
mini6502.zip). dir_status.asm reads and prints the drive's own status
message off the command channel (secondary address 15) -- built after
dir_raw.asm's real-hardware output showed something that looked like
the KERNAL reading garbage without ever reporting an error via
READST, to check the drive's own health independent of any directory-
specific logic. See dir_status.asm's own header comment for the full
reasoning.

Testing this needed mini6502.py to grow actual command-channel
simulation (self.drive_status) -- opening secondary address 15 didn't
do anything special before this file needed it to.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_dir_status.py
"""

import os
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

print("=== assembling dir_status.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'dir_status.asm', '-o', '/tmp/dir_status_regress.prg',
     '--listing', '/tmp/dir_status_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("dir_status.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/dir_status_regress.prg', 'rb') as f:
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


print("=== a healthy drive's status message is read and printed correctly ===")
m1, target = fresh()
reason = run_until_return(m1, target)
check("program returns cleanly", reason is None, f"reason={reason}")
text = ''.join(m1.output_text)
check("reports OPEN success", 'CARRY CLEAR (SUCCESS)' in text)
check("the drive's own status message appears verbatim", '00, OK,00,00' in text)
check("reports DONE at the end", text.rstrip().endswith('DONE'))

print("=== a drive reporting a real error shows that error's exact text ===")
m2, target = fresh()
m2.drive_status = "21,READ ERROR,18,00\r"
reason = run_until_return(m2, target)
check("program returns cleanly", reason is None, f"reason={reason}")
check("the actual error text is shown, not swallowed or paraphrased",
      '21,READ ERROR,18,00' in ''.join(m2.output_text))

print("=== OPEN failure (no drive/disk present) is reported with its real error code ===")
m3, target = fresh()
m3.device_present = False
reason = run_until_return(m3, target, max_instructions=500_000)
check("program returns cleanly, does not hang", reason is None, f"reason={reason}")
text3 = ''.join(m3.output_text)
check("reports OPEN failure", 'CARRY SET (FAILED)' in text3)
check("reports the real error code (05, DEVICE NOT PRESENT)", 'ERROR CODE: 05' in text3)
check("no status message printed after a failed OPEN", 'STATUS MESSAGE FOLLOWS' not in text3)

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
