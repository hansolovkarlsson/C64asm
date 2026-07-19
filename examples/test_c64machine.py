import sys
sys.path.insert(0, '/home/claude/emu_dev')
from mini6502 import C64Machine, CPU6502

passed = 0
failed = 0


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {name}  {detail}")


print("=== CHROUT trap: captures and decodes printed text ===")
m = C64Machine()
# LDA #<msg ; LDY #>msg ; JSR print_loop  where print_loop prints until $00
# Simpler: just call CHROUT directly per character.
prog = bytes([
    0xA9, ord('H'), 0x20, 0xD2, 0xFF,   # LDA #'H' ; JSR CHROUT
    0xA9, ord('I'), 0x20, 0xD2, 0xFF,   # LDA #'I' ; JSR CHROUT
    0xA9, 0x0D,       0x20, 0xD2, 0xFF, # LDA #$0D ; JSR CHROUT (return)
    0x60,                                # RTS
])
m.cpu.memory[0xC000:0xC000+len(prog)] = prog
reason = m.run_until_return(0xC000)
check("CHROUT test ran to completion", reason is None, f"reason={reason}")
check("CHROUT captured text", ''.join(m.output_text) == 'HI\n',
      f"got {m.output_text!r}")

print()
print("=== THE CRITICAL TEST: active-low CIA1 keyboard/joystick polarity ===")
# This is exactly the scenario that matters: read CIA1_PRB after
# selecting a column, and check the polarity convention a real 6502
# program must use to correctly detect "key held".
m = C64Machine()
# Simulate: W key held (matrix column 1 = %11111101, bit 1 = W)
m.press_key(0b11111101, 0b00000010)

prog = bytes([
    0xA9, 0b11111101, 0x8D, 0x00, 0xDC,   # LDA #$FD ; STA $DC00 (select column 1)
    0xAD, 0x01, 0xDC,                       # LDA $DC01             (read row data)
    0x29, 0b00000010,                        # AND #%00000010          (isolate W's bit)
    0x60,                                      # RTS
])
m.cpu.memory[0xC000:0xC000+len(prog)] = prog
reason = m.run_until_return(0xC000)
check("active-low test ran cleanly", reason is None, f"reason={reason}")
check(
    "held key reads as a CLEAR (zero) bit, not a set one -- "
    "AND result must be 0, meaning Z flag SET (BEQ branch taken)",
    m.cpu.get_flag(0x02) == 1,   # FLAG_Z
    f"a={m.cpu.a:02X}, Z={m.cpu.get_flag(0x02)} "
    f"(if this fails, it confirms: BNE-means-pressed is WRONG; "
    f"BEQ means pressed, matching every proven demo in this project)"
)

# Now release it and confirm the OPPOSITE result.
m2 = C64Machine()
prog2 = bytes([
    0xA9, 0b11111101, 0x8D, 0x00, 0xDC,
    0xAD, 0x01, 0xDC,
    0x29, 0b00000010,
    0x60,
])
m2.cpu.memory[0xC000:0xC000+len(prog2)] = prog2
m2.run_until_return(0xC000)
check("key NOT held -> AND result nonzero -> Z flag clear",
      m2.cpu.get_flag(0x02) == 0, f"a={m2.cpu.a:02X}")

print()
print("=== joystick active-low polarity ===")
m = C64Machine()
m.joystick2 = 0b00000001   # "up" held, test-convention active-high input
prog = bytes([
    0xAD, 0x00, 0xDC,     # LDA $DC00  (raw joystick read)
    0x60,
])
m.cpu.memory[0xC000:0xC000+len(prog)] = prog
m.run_until_return(0xC000)
check("joystick 'up' held reads as bit0 CLEAR on the real register",
      (m.cpu.a & 0x01) == 0, f"a={m.cpu.a:02X}")

print()
print("=== zero-page poisoning simulation actually corrupts $F3-$F6 ===")
m = C64Machine(simulate_zp_poisoning=True, poison_every_n_instructions=5)
m.cpu.memory[0xF4] = 0xAB   # a program "relying" on this byte staying put
prog = bytes([0xEA] * 20 + [0x60])   # 20 NOPs then RTS -- long enough to
                                        # guarantee at least one poison event
m.cpu.memory[0xC000:0xC000+len(prog)] = prog
m.run_until_return(0xC000)
check("simulated KERNAL activity corrupted the poisoned zero-page range",
      m.cpu.memory[0xF4] != 0xAB,
      f"mem[$F4]={m.cpu.memory[0xF4]:02X} (should differ from the original $AB)")

m_safe = C64Machine(simulate_zp_poisoning=True, poison_every_n_instructions=5)
m_safe.cpu.memory[0x09] = 0xCD   # this project's documented SAFE range
m_safe.cpu.memory[0xC000:0xC000+len(prog)] = prog
m_safe.run_until_return(0xC000)
check("a byte OUTSIDE the poisoned range is left alone",
      m_safe.cpu.memory[0x09] == 0xCD, f"mem[$09]={m_safe.cpu.memory[0x09]:02X}")

print()
print("=== .basic SYS-target parsing ===")
import subprocess
result = subprocess.run(['python3', '/mnt/user-data/outputs/c64asm.py',
                          '/home/claude/c64asm/hello.asm', '-o', '/tmp/mini6502_hello.prg'],
                         capture_output=True, text=True)
check("hello.asm assembled for this test", result.returncode == 0, result.stderr)
with open('/tmp/mini6502_hello.prg', 'rb') as f:
    data = f.read()
m = C64Machine()
load_addr = m.load_prg(data)
target = m.find_sys_target(data)
check("SYS target parsed from the BASIC stub", target > 0x0801, f"target=${target:04X}")

print()
print("=== CIA1_PRA reads are data-direction-register aware ===")
# A pin configured as OUTPUT (DDRA bit=1) must read back the output
# latch, not simulated external (joystick) state -- this is what a
# real 6526 CIA does, and what exposed a real bug in this project's
# own input.inc (CIA_KEYBOARD_SETUP leaving DDRA in output mode broke
# read_joy2 for the rest of the program).
m = C64Machine()
m.joystick2 = 0   # nothing held -- if DDR-awareness were missing, a
                    # naive read would see this, not the output latch
prog = bytes([
    0xA9, 0xFF, 0x8D, 0x02, 0xDC,   # LDA #$FF ; STA $DC02 (DDRA=all-output)
    0xA9, 0xFB, 0x8D, 0x00, 0xDC,   # LDA #$FB ; STA $DC00 (write a latch value)
    0xAD, 0x00, 0xDC,                 # LDA $DC00 (read it back)
    0x60,
])
m.cpu.memory[0xC000:0xC000+len(prog)] = prog
reason = m.run_until_return(0xC000)
check("DDRA=output test ran cleanly", reason is None, f"reason={reason}")
check("reading a pin configured as output returns the output latch, "
      "not simulated joystick state",
      m.cpu.a == 0xFB, f"a={m.cpu.a:02X} (expected $FB, the latched value)")

m2 = C64Machine()
m2.joystick2 = 0
prog2 = bytes([
    0xA9, 0x00, 0x8D, 0x02, 0xDC,   # DDRA=all-input
    0xAD, 0x00, 0xDC,                 # LDA $DC00 -- now reads real joystick state
    0x60,
])
m2.cpu.memory[0xC000:0xC000+len(prog2)] = prog2
m2.run_until_return(0xC000)
check("reading a pin configured as input returns real (simulated) "
      "external state, not a leftover output latch value",
      m2.cpu.a == 0xFF, f"a={m2.cpu.a:02X} (expected $FF -- active-low, nothing held)")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
