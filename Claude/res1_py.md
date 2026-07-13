res1_py.md

# Make a complete Commodore 64 assembler

## Output

I'll build this as a full two-pass 6502/6510 assembler that takes standard assembly source and produces a C64-loadable `.prg` file. Let me set it up and test it against a real program.Let me fix a bug in the BASIC stub handling — the address-length feedback loop needs a proper fixed-point calculation rather than the tangled logic I first wrote.Now let's replace the old `_basic_stub` method with a correct fixed-point version:Now let's write a test program to exercise the assembler — a classic "print HELLO and change border color" C64 program:The stub math checks out (SYS 2061 → code starts at $080D, matching the listing). Let me test a few more edge cases — zero-page vs absolute disambiguation, indirect addressing, `.word`, and low/high byte operators:

Excellent — every addressing mode, forward reference, and directive resolved correctly, including a `.word` reference to a symbol defined *after* its use via `=`. Let me quickly confirm error handling works too, then package everything up:Both error paths work correctly — out-of-range branches and undefined symbols are caught with clear, line-numbered messages instead of silently producing bad code. Now let's package the final deliverables with documentation:Done — a complete, tested two-pass 6502/6510 assembler for the C64.

## What's included:
- `c64asm.py` — the assembler itself (run with `python3 c64asm.py input.asm -o output.prg`)
- `hello.asm` — a sample program (prints text via KERNAL CHROUT, cycles the border color) demonstrating the syntax
- `hello.prg` — that program assembled and ready to load in VICE or on real hardware
- `hello.lst` — an assembly listing showing addresses, bytes, and source side by side

## Coverage:
- All 56 official 6502/6510 mnemonics, every addressing mode, with automatic zero-page vs. absolute selection
- Two-pass assembly so forward references (labels used before they're defined) just work
- Full expression parser: `+ - * /`, parentheses, `<`/`>` for low/high byte, `$hex`, `%binary`, decimal, `'char'` literals, `*` for current PC
- Directives: `.org`/`*=`, `.byte`/`.db`, `.word`/`.dw`, `.text`/`.asc` (with ASCII→PETSCII mapping), `.fill`, constant assignment (`=`/`.equ`)
- `.basic` directive that auto-generates a correct `10 SYS xxxx` loader stub, with a fixed-point calculation so the SYS address is always right even when its digit-count affects the stub's own length
- Real error checking: undefined symbols, branches out of ±127 range, invalid addressing modes — all reported with line numbers, not silently corrupted output

I verified it against hand-computed opcodes for every addressing mode, checked the BASIC stub's byte-for-byte structure, and confirmed forward/backward branch offsets and symbol resolution are correct.

One thing worth knowing: I don't have network access in this sandbox, so I couldn't run the output in an actual C64 emulator — I verified correctness by hand-checking the generated machine code against the 6502 opcode reference instead. If you load `hello.prg` into VICE (or drag it onto an emulator) and it doesn't behave as expected, let me know and I'll dig in.
