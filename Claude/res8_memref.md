res8_memref.md

# Prompt

create a document layout out common and useful memory addresses for the C64, like graphic modes, sprites, and sound, and some sample code how to use them.

# Response

This one benefits from testing every code sample against the real assembler before it ships, given how much binary sprite data and indirect addressing will be involved. Let me build and verify it carefully.Now let's extract every code sample and verify each one actually assembles:I hit a genuine bug in the assembler itself — not the sample, the tool. Let me isolate it precisely:Found it. `*` is used for two different things in expression syntax — "multiply" and "current program counter" — and the tokenizer has always classified every `*` as the PC token, unconditionally, before the multiplication operator ever gets a chance. That means **`A*B` has been silently broken in both the Python and C assemblers all along** — none of my earlier test programs happened to use multiplication in an expression, so it slipped through. Let me fix it properly in both.Now the same fix in the C version:Now let's rebuild both, and specifically test multiplication plus re-run the full regression suite:Fixed — multiplication works correctly now, and `*` still correctly means "current PC" when used standalone. Let's verify the C build too, and re-run the full regression suite on both:Both fixed and matching. Now the critical part — full regression to make sure this fix didn't break anything else:No regressions. Now let's continue verifying the rest of the memory-map document's examples (the earlier batch failure was exactly this bug):All four pass now. Continuing with the rest of the document's examples — bitmap mode, sprites, sound, joystick, KERNAL:Correct — branch offsets and the `ptr+1` zero-page arithmetic all check out. Now sprites, sound, joystick, and KERNAL:Confirmed — `#(SPRITE_DATA / 64)` correctly evaluates to `$80`, and the pointer address resolves to `$07F8`. Now sound, joystick, and KERNAL:All 13 test files across every section assembled cleanly. Let's do one final cross-check with the C build for good measure, then clean up and ship the doc:Every example is verified byte-identical between both assembler implementations. Let's clean up and ship all three deliverables — the fixed assemblers plus the new memory reference:Two things came out of this:

**The new document**, `c64-memory-reference.md`, covers:
1. Memory map overview
2. Screen & color RAM — including the PETSCII-vs-screen-code distinction, a common source of confusion
3. VIC-II: border/background colors, `$D011`/`$D016`/`$D018` bit layouts, and worked examples for all five graphics modes (standard text, multicolor text, ECM text, hi-res bitmap, multicolor bitmap)
4. Sprites: full register table, data/pointer mechanics, positioning past X=255, multicolor sprites, collision detection
5. SID sound: register layout, waveform/gate bits, ADSR, a complete play-a-note example
6. Joystick reading via CIA1, with a sprite-movement example
7. Common KERNAL routines
8. Zero-page usage notes

**More importantly, building it surfaced a real bug in the assembler itself**, not just the sample code: `*` is overloaded in 6502 assembly syntax to mean both "multiply" and "current program counter," and the tokenizer in *both* `c64asm.py` and `c64asm.c` was unconditionally treating every `*` as the PC token — meaning **any expression using multiplication has been silently broken since the very first version**, in both languages. It just never showed up because none of the earlier example programs happened to multiply anything.

I've fixed it in both files (disambiguation now happens by parser position — `*` only means "current PC" when the parser is expecting a standalone value, and only means multiply when it appears between two already-parsed operands), confirmed the fix against a battery of multiplication tests, and re-ran the full existing regression suite (`hello.asm`, `edge.asm`, error-handling tests) to confirm nothing else broke. Every example in the new document — 13 test files covering every section — assembles cleanly and produces byte-identical output on both implementations. Updated `c64asm.py` and `c64asm.c` are included above alongside the new reference.