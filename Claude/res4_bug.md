res4_bug.md

# Prompt

That's better. Now it prints the correct message, however it doesn't cycle the border colors as the code says. Could it be the wrong memory address used for border color?

# Result

`$D020` is exactly right — that's the correct VIC-II border color register on every C64 memory map, and the listing confirms the assembler encoded it correctly (`STA $D020` / `INC $D020`, absolute mode, little-endian). So the address isn't the problem.

The real issue is **timing**: the delay loop only spins Y through 256 values —

```asm
delay:
        iny
        bne delay
```

— which is about 1,280 CPU cycles, or roughly **1.3 milliseconds** at ~1 MHz. A single video frame takes ~16–20 ms, so that loop lets the border color change something like 10–15 times *within one frame*. The result isn't "no cycling" — it's cycling far too fast to perceive as color changes; it just reads as noise or a flat blur, which looks the same as "nothing happening."

The fix is a longer delay. Let me nest two loops so each color holds for a visible fraction of a second:Both assemblers still agree byte-for-byte. Timing now works out to roughly 250,000–330,000 cycles per color step — around a third of a second each, so you'll see a clean, visible cycle through all 16 border colors (full cycle repeating every ~5 seconds) instead of a flicker blur.To recap for future programs: **the assembler and address are both correct** — this one was purely a timing bug in the test program itself, not the tool. A single `iny`/`bne` loop (256 iterations, ~1,280 cycles) is a common building block, but on its own it's too fast for anything visible on-screen — it's really only useful as a short pause or as one leg of a bigger nested delay, which is what I did here (outer `dex`/`bne` loop around the inner one, giving ~65,536 iterations instead of 256).

Load the new `hello.prg` and you should now clearly see the border sweep through all 16 C64 colors, holding each for roughly a third of a second.