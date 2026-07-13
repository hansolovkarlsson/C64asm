res3_bug.md

# Prompt


The prg doesn't work. I used Vice and the C64SC option. It produces some graphical characters and then it doesn't do anything else. Is there a particular setting I have to do in the Vice emulator?

# Result

Good catch — that's not a VICE setting issue, it's a real bug in the assembler's text-encoding logic that I should have caught. Let me explain what's happening and fix it.

**The bug:** My `ascii_to_petscii()` function has the upper/lowercase mapping backwards. On the C64's default character set, PETSCII codes `$41`–`$5A` display as uppercase letters (same as plain ASCII), but codes `$C1`–`$DA` display as line-drawing/card-suit graphics symbols — not letters. My code mapped already-uppercase input (`"HELLO, C64!"`) into that `$C1`–`$DA` graphics range, so instead of "HELLO, C64!" you got a row of box-drawing characters. That matches exactly what you saw. After that the program falls into its intentional infinite border-color-cycling loop, which is quiet/subtle — that part's not actually broken, just easy to mistake for "it stopped."

Let me fix and re-verify both versions:Now the same fix in the Python version, so both stay in sync:Now rebuild and re-verify with both assemblers, checking the actual message bytes:The message bytes are now `48 45 4C 4C 4F...` — plain ASCII values for "HELLO, C64!", which is exactly correct PETSCII for the default C64 charset. Both assemblers agree. Let me update the outputs:Fixed in both `c64asm.c` and `c64asm.py`, and I've regenerated `hello.prg` with the corrected bytes.

Nothing wrong with your VICE setup — you were doing it right, and the fact that you saw *something* on screen right where text should go confirms the program loaded and ran correctly; it was purely a wrong-byte-value bug on my end in the text encoder.

Try the new `hello.prg` — you should now see `HELLO, C64!` print out character by character, followed by the border color cycling continuously (that loop never ends by design, so the program will just sit there pulsing the border — that's expected, not a hang).

One more thing worth knowing for future programs you assemble with this: if you ever want genuine **lowercase** text on screen (not just folded to uppercase), that requires an extra step I haven't implemented — sending PETSCII control code `$0E` first to switch the C64 into lowercase/uppercase charset mode, then using the `$C1`–`$DA` range for lowercase letters. Right now the assembler just folds all lowercase input up to uppercase, which covers the common case but not true mixed-case text. Let me know if you want that added.