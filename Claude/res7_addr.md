res7_addr.md

# Prompt

Add some explaining of the different addressing modes to the opcode reference

# Response

Now let's verify the new syntax examples actually assemble before shipping:All new examples assemble cleanly. Shipping the updated doc:Added a new **Addressing Modes** section right after the flag legend, covering all 13 modes: implied, accumulator, immediate, zero page, zero page,X/Y, absolute, absolute,X/Y, indirect, indexed indirect, indirect indexed, and relative. Each one explains what it means, how the effective address is actually computed, a working example, and the practical gotchas that trip people up — including:

- immediate `#$10` vs. zero-page `$10` being one of the most common beginner mix-ups
- the NMOS 6502/6510 indirect-JMP page-boundary hardware bug (don't put a jump-table pointer at an address ending in `$FF`)
- the difference between `(zp,X)` (index before dereferencing — picks a pointer) and `(zp),Y` (dereference then index — offsets into what the pointer points to), which is the other classic point of confusion

I re-verified every new example against the real assembler before including it, same as the instruction examples earlier.

