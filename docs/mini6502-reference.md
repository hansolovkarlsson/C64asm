# mini6502 — a 6502 emulator for testing c64asm output

`mini6502.py` runs the `.prg` files this project's assembler produces
and lets you check what they actually *do* — registers, memory, flags,
printed text, and which hardware registers got written — instead of
only what a listing says they *should* do. It exists because two real
bugs in this project's own standard library (see `lib-reference.md`)
were completely invisible from assembled bytes and a careful listing
read, and were only found by actually running the code.

This is not a general-purpose C64 emulator. There's no VIC-II pixel
output, no real KERNAL ROM, and no cycle-accurate timing. It implements
every official 6502/6510 opcode and addressing mode — cross-checked
directly against `c64asm`'s own opcode table, all 151 (opcode, mode)
pairs matching exactly — plus the specific hardware behavior this
project has actually needed:

- **CIA1 keyboard/joystick I/O** ($DC00–$DC03), correctly **active-low**
  (a held key or direction reads as a *clear* bit, not a set one) —
  this is the exact detail an earlier draft of this library's
  `READ_KEY` macro got backwards, and the bug that motivated building
  this emulator in the first place. Reads are also **data-direction-
  register aware**: $DC00 (`CIA1_PRA`) is shared between joystick input
  and keyboard column-select output, and on real 6526 CIA hardware, a
  pin configured as an *output* (via `DDRA`) reads back whatever was
  last *written* to it, not the actual external switch state. An
  earlier version of this emulator always returned simulated joystick
  state regardless of `DDRA`, which masked a real bug in this
  project's own library (`CIA_KEYBOARD_SETUP` leaving `DDRA` in
  output mode broke `read_joy2` for the rest of the program) —
  finding that required fixing the emulator's own fidelity first, then
  re-tracing with it.
- **`CHROUT`/`CHRIN` call trapping.** There's no real KERNAL ROM to
  execute, so a `JSR CHROUT`/`JSR CHRIN` is intercepted the moment
  execution would reach that address, handled behaviorally (append the
  decoded character to a captured output buffer; pop a queued input
  byte), and returned from immediately — the same observable effect a
  real KERNAL call has, without needing the ROM.
- **KERNAL zero-page poisoning.** On real hardware, the KERNAL's own
  interrupt-driven housekeeping periodically overwrites $F3–$F6, which
  is *why* this project's own zero-page usage avoids that range (see
  `c64-memory-reference.md`). `mini6502.py` simulates this by
  periodically corrupting those bytes during emulation, so a program
  that picks a colliding scratch address shows visibly wrong behavior
  in emulation instead of only failing later on real hardware.
- **The `JMP ($xxFF)` page-boundary bug**, faithfully reproduced (the
  real 6502 wraps within the page for the indirect jump's high byte
  instead of crossing into the next page) — `c64asm-opcode-reference.md`
  already documented this as a real hardware gotcha to avoid; the
  emulator reproduces the actual buggy behavior rather than "fixing"
  it, so code that accidentally triggers it is caught here too.

Deliberately not modeled: decimal (BCD) mode for `ADC`/`SBC` (nothing
in this project ever sets the `D` flag), and real VIC-II video timing
or pixel rendering — register *writes* to VIC-II/SID addresses are
recorded (`machine.io_writes`) so a test can assert on them, but
nothing renders them.

## Quick start

```python
from mini6502 import C64Machine

m = C64Machine()
with open('hello.prg', 'rb') as f:
    data = f.read()

target = m.find_sys_target(data)   # parses the .basic stub's SYS line
m.load_prg(data)

reason = m.run_until_return(target, max_instructions=100000)
assert reason is None, reason      # None means it returned cleanly

print(''.join(m.output_text))      # captured, PETSCII-decoded CHROUT output
```

## Simulating input

```python
m.press_key(0b11111101, 0b00000010)   # W: matrix column 1, bit 1
m.release_key(0b11111101, 0b00000010)
m.joystick2 = 0b00010000               # fire button, active-HIGH
                                          # (the test-facing convention;
                                          # inverted internally to match
                                          # the real active-low register)
```

Matrix (column, bit) pairs match the table this project's own demos
(`pong.asm`, `lander.asm`) already verified on real hardware — the same
values you'd pass to `input.inc`'s `READ_KEY` macro.

## Running without a clean return

`run_until_return()` is for programs with a single top-level `rts`
(this project's own convention — see `demo.asm`). For a program that
spins forever (`hello.asm`'s border-color-cycling loop, for instance),
step it directly instead:

```python
m.cpu.pc = target
for _ in range(3000):
    if m.cpu.halted:
        break
    m.step()
```

## Checking hardware register writes

```python
sid_writes = [(hex(a), hex(v)) for a, v in m.io_writes if 0xD400 <= a <= 0xD41F]
```

`m.io_writes` records every write to $D000–$DFFF (VIC-II/SID/CIA) in
order, capped at 10,000 entries so a runaway loop can't exhaust memory.

## Tests

`test_cpu_core.py` validates the CPU core in isolation (addressing
modes, arithmetic and flag semantics, stack behavior, the JMP-indirect
bug) against hand-computed expected results. `test_c64machine.py`
validates the C64-specific layer — including the active-low CIA test
that would have caught the `READ_KEY` bug immediately — plus an
end-to-end run of `hello.asm`'s already-proven, independently-built
`.prg` as a sanity check that the emulator itself is trustworthy before
using it to validate anything new.
