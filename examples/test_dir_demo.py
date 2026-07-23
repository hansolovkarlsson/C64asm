"""
End-to-end regression test for dir_demo.asm, using mini6502.py (see
mini6502.zip). This program was pulled out of editor.asm specifically
to make the directory-listing code easier to test and debug on its
own -- see dir_demo.asm's own header comment for what changed along
the way and why.

Two of the checks below exercise failure paths mini6502.py couldn't
simulate before this file needed them: a missing/not-present drive
(m.device_present = False) and a directory stream that ends before a
proper terminator ever arrives (a monkeypatched
_generate_directory_listing). Both are real gaps this project's own
prior testing had -- a simulated drive that always succeeds and always
returns well-formed data can't catch code that never checks whether
OPEN actually worked, or that has no way to stop a read loop early.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_dir_demo.py
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

print("=== assembling dir_demo.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'dir_demo.asm', '-o', '/tmp/dir_demo_regress.prg',
     '--listing', '/tmp/dir_demo_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("dir_demo.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/dir_demo_regress.prg', 'rb') as f:
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


print("=== a normal listing with several files, including one over 254 bytes ===")
m1, target = fresh()
m1.disk_files = {'HELLO': b'H' * 300, 'NOTES': b'short text', 'BIGFILE': b'X' * 10000}
reason = run_until_return(m1, target)
check("program returns cleanly", reason is None, f"reason={reason}")
text = ''.join(m1.output_text)
check("disk name line appears", '"VIRTUAL DISK"' in text)
check("HELLO listed with 2 blocks (300 bytes -> ceil(300/254))", '2    "HELLO"' in text)
check("NOTES listed with 1 block", '1    "NOTES"' in text)
check("BIGFILE listed with 40 blocks (10000 bytes -> ceil(10000/254))",
      '40    "BIGFILE"' in text)
check("free blocks line appears", 'BLOCKS FREE' in text)
check("end-of-listing message appears", '(END OF LISTING)' in text)

print("=== an empty disk (zero files) lists cleanly, no crash ===")
m2, target = fresh()
m2.disk_files = {}
reason = run_until_return(m2, target)
check("program returns cleanly", reason is None, f"reason={reason}")
text2 = ''.join(m2.output_text)
check("disk name line still appears", '"VIRTUAL DISK"' in text2)
check("no file entries shown",
      'VIRTUAL DISK"' in text2
      and '"' not in text2.split('BLOCKS FREE')[0].split('VIRTUAL DISK"')[1])
check("free blocks line still appears", 'BLOCKS FREE' in text2)

print("=== reverse video from the disk name line doesn't bleed into later lines ===")
m3, target = fresh()
m3.disk_files = {'A': b'x'}
reason = run_until_return(m3, target)
check("program returns cleanly", reason is None, f"reason={reason}")
raw = m3.output_raw
# every line this program prints ends with $92 (reverse off) right
# before the $0D -- confirms the disk name line's own $12 (reverse on)
# can't visually carry over into the file listing or the free-blocks
# line, regardless of whether a real drive already sends its own $92
reverse_off_count = raw.count(0x92)
check("reverse-off sent after every printed line (disk name, 1 file, blocks free)",
      reverse_off_count == 3, f"found {reverse_off_count}")

print("=== OPEN failure (no drive/disk present) is reported, not garbage or a hang ===")
m4, target = fresh()
m4.device_present = False
reason = run_until_return(m4, target, max_instructions=500_000)
check("program returns cleanly, does not hang", reason is None, f"reason={reason}")
text4 = ''.join(m4.output_text)
check("a clear error message is shown", 'PRESENT' in text4 and 'ATTACHED' in text4)
check("no directory content was printed", '"VIRTUAL DISK"' not in text4)

print("=== a stream that ends before a $00 terminator stops cleanly instead of hanging ===")
m5, target = fresh()
malformed = bytes([0x01, 0x04,      # fake load address
                    0x01, 0x01,      # link pointer (nonzero = "more")
                    0x00, 0x00,      # line number 0 (disk name line)
                    0x12]) + b'"BROKEN DISK'   # text with NO null terminator --
                                                 # the stream just ends here
m5._generate_directory_listing = lambda: malformed
reason = run_until_return(m5, target, max_instructions=500_000)
check("program returns cleanly, does not hang on the missing terminator",
      reason is None, f"reason={reason}")
check("end-of-listing message still appears despite the truncation",
      '(END OF LISTING)' in ''.join(m5.output_text))

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
