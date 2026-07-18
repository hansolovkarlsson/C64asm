# c64asm standard library

Five `.include`-able files providing the register constants, text
output, input handling, graphics setup, and sound effects that this
project's own demo programs (`hello.asm`, `bounce.asm`, `pong.asm`,
`adventure.asm`, `lander.asm`) each implemented from scratch. Everything
here is extracted from, and cross-checked against, that already-proven
code — nothing in this library is new, untested logic; it's existing
patterns generalized into reusable form.

This is a real exercise of the `.include` feature it depends on: every
file below `.include`s `hardware.inc`, and a program that pulls in
several of them together only pays for `hardware.inc` being processed
once, with no "already defined" collisions — see `demo.asm` for a
program that does exactly this with all five files at once, and
`c64asm-reference.md`'s "Includes" section for why that works.

**Using this library from more than one project directory:** by
default, `.include "lib/text.inc"` resolves relative to the file that
contains the `.include` line, so each project needs its own copy of
this `lib/` folder sitting next to its source. To keep a single shared
copy instead and point every project at it, pass
`--lib-dir <path to this lib/ folder itself>` on the command line —
e.g. if you've unpacked this zip to `/opt/c64lib/lib`, use
`--lib-dir /opt/c64lib/lib` (not `--lib-dir /opt/c64lib`). It's a pure
fallback, only consulted when a project's own local `lib/` doesn't
have the file, so it's safe to pass on every build without risk of it
silently shadowing a project's own files. See `c64asm-reference.md` §1
for the full behavior.

## Files

| File | Provides |
|---|---|
| `lib/hardware.inc` | VIC-II, SID, CIA, and KERNAL register constants. No code — always safe to `.include`. |
| `lib/text.inc` | `PRINT msg`, `CLS`, `NEWLINE`, the `print_msg` subroutine they're built on, and `str_equal` for comparing typed input against known keywords. |
| `lib/input.inc` | `CIA_KEYBOARD_SETUP`, `read_joy2`, `READ_KEY column, mask` for joystick/keyboard-matrix input; `read_line` and `extract_word` for reading and tokenizing a typed line via `CHRIN`. |
| `lib/graphics.inc` | `BITMAP_MODE_ON addr`, `BITMAP_MODE_OFF`, `CLEAR_BITMAP addr`, `SET_SCREEN_COLOR value`, `SPRITE_INIT data, color, x, y` for bitmap/sprite setup; `wait_frame` for raster-synced timing and `sprite0_bounce_step` for animating a sprite bouncing within a rectangular area. |
| `lib/sound.inc` | `SID_INIT`, `PLAY_SOUND freq_hi, ad, sr, waveform`, `engine_sound_on`/`engine_sound_off`. |

## Using a library file

```
        .include "lib/text.inc"
```

resolved relative to the file that contains the `.include` line (see
`c64asm-reference.md` §10) — so if your project keeps `lib/` alongside
your source file, this line works unchanged regardless of what
directory you actually run `c64asm` from.

Some routines need a small amount of setup the including program must
provide, since which addresses are safe to use depends on what else
your program has already claimed. **Every item below is required as
soon as you `.include` that file — even if you never call the specific
routine that uses it.** `.include` splices in a file's code
unconditionally, the same as any other source; a routine's own body
still references its zero-page location whether or not your program
ever `jsr`s to it, so assembly fails on an undefined symbol otherwise.

- **`text.inc`** needs THREE 2-byte zero-page locations: `str_ptr`
  (used by `print_msg`/`PRINT`), and `cmp_ptr`/`kw_ptr` (used by
  `str_equal`) — all three, even if your program only ever calls
  `PRINT`.
- **`input.inc`** needs one 2-byte zero-page location, `word_dest_ptr`
  (used by `extract_word`) — even if your program only ever calls
  `read_joy2`/`READ_KEY`.
- **`graphics.inc`** needs one 2-byte zero-page location, `gfx_ptr`
  (used by `CLEAR_BITMAP`/`SET_SCREEN_COLOR`), plus eight more,
  unrelated, used by `sprite0_bounce_step`: `xpos`, `ypos`, `xdir`,
  `ydir` (one byte each, not necessarily zero page — see the file's own
  header comment) and `XMIN`, `XMAX`, `YMIN`, `YMAX` (compile-time
  constants) — all nine, even if your program only ever calls
  `BITMAP_MODE_ON`/`BITMAP_MODE_OFF`.
- **`sound.inc`** needs a 1-byte flag, `engine_playing` (used by
  `engine_sound_on`/`engine_sound_off`; anywhere in RAM, not
  necessarily zero page).

Define these with `=` before the relevant `.include` line:

```
str_ptr = $fb
cmp_ptr = $02
kw_ptr  = $04
word_dest_ptr = $fb    ; safe to alias with str_ptr -- see text.inc's
                          ; header comment for why
engine_playing = $09
gfx_ptr = $fd
xpos = $06              ; sprite0_bounce_step's state -- see
ypos = $07                ; graphics.inc's header comment for why
xdir = $08                 ; these don't need to be zero page
ydir = $0a
XMIN = 24                ; sprite0_bounce_step's bounds -- any
XMAX = 250                 ; values work if you're not actually
YMIN = 50                    ; using it yet, but see bounce.asm
YMAX = 220                     ; for a real, worked example
        .include "lib/text.inc"
        .include "lib/input.inc"
        .include "lib/sound.inc"
        .include "lib/graphics.inc"
```

`$02`–`$0F` and `$FB`–`$FE` are the community-documented zero-page
range this project's own demos use for exactly this kind of scratch
storage (see `c64-memory-reference.md`).

### A `.basic` gotcha these libraries will expose

`.basic`'s generated `SYS` stub always jumps to whatever code comes
**immediately after** the `.basic` line in the assembled output —
*not* to a label named `start` or anything else specific. If you
`.include` a library file that emits real subroutine code (as
`text.inc`, `input.inc`, and `sound.inc` all do — `print_msg`,
`read_joy2`, `engine_sound_on`, and so on aren't macros, they're real
code) between `.basic` and your own entry point, `SYS` lands inside
the *first* included subroutine instead, running it with whatever
garbage happens to be in the registers at power-on rather than your
actual program.

This is exactly the bug an earlier build of `demo.asm` had: `.basic`
was immediately followed by `.include "lib/text.inc"`, so `SYS` jumped
straight into `print_msg` with an uninitialized pointer, which printed
a couple of garbage bytes and immediately `rts`'d back to BASIC instead
of running the program at all.

The fix — and this project's own demos hit this same mistake often
enough that it's now built into the assembler rather than something
you have to remember — is to give `.basic` your entry point's label
directly:

```
        .basic start
        .include "lib/text.inc"
        .include "lib/input.inc"
        ; ... rest of your program, including start: ...
```

`.basic start` auto-emits `jmp start` right after the loader stub, so
it doesn't matter what ends up between it and your real entry point —
see `c64asm-reference.md` §7 for the directive's full behavior. Both
`demo.asm` and `adventure.asm` use this form; it's the right default
for any program that `.include`s library code before its entry point,
and there's no real downside to using it even when you don't strictly
need it yet (three bytes, one harmless jump).

### An `input.inc` gotcha `text.inc`'s `NEWLINE` fixes

Printing a response immediately after `read_line` returns, with
nothing in between, was found (on real hardware and in VICE, not just
in theory) to leave that response running onto the same screen line as
whatever had just been typed — not a separate line below it, the way
you'd expect after pressing RETURN to submit the input:

```
> go north

```

became, without the fix:

```
> go northYou are standing in a forest...
```

`adventure.asm` hit exactly this. The fix is to call `text.inc`'s
`NEWLINE` right after `read_line`, before printing anything else:

```
        jsr read_line
        NEWLINE
        jsr tokenize
        jsr dispatch
```

If your program reads typed input at all, call `NEWLINE` right after
the read, before the first thing you print in response — the same
place `adventure.asm` calls it.

### A `graphics.inc` gotcha this library will also expose

`BITMAP_MODE_ON` only flips the VIC-II into bitmap mode — it doesn't
clear anything. Two things need clearing before the screen looks
sensible, and forgetting either produces a genuinely confusing result
that doesn't look like an obvious "graphics bug" at first:

- **The bitmap data itself** (`CLEAR_BITMAP`) — skip this and whatever
  garbage RAM already held shows up as scrambled, meaningless pixels.
- **Screen memory's color data** (`SET_SCREEN_COLOR`) — this is the
  *same* memory `CHROUT` writes character codes into for ordinary text.
  In bitmap mode it's reinterpreted as color data instead. Any text
  printed (or `CLS`'s own fill character) *before* switching to bitmap
  mode is still sitting there as leftover character codes, which show
  up as scrambled colored pixels wherever that text was, plus a solid
  stripe of one color across the rest of the screen from `CLS`'s fill.

This is exactly the second bug an earlier build of `demo.asm` had: it
printed a welcome message, immediately called `BITMAP_MODE_ON`, and
went straight to `SPRITE_INIT` — never clearing either. The fix,
matching this project's own proven `bounce.asm`, is to always call both
immediately after `BITMAP_MODE_ON`:

```
        BITMAP_MODE_ON BITMAP
        CLEAR_BITMAP BITMAP
        SET_SCREEN_COLOR %00010110   ; fg=white(1), bg=blue(6), for example
        SPRITE_INIT sprite_data, 1, 100, 100   ; after, not before --
                                                  ; SET_SCREEN_COLOR's fill
                                                  ; reaches into the sprite
                                                  ; pointer bytes too
```

### An `input.inc` bug this library used to have (now fixed, no action needed)

An earlier version of this library had `CIA_KEYBOARD_SETUP` permanently
configure the joystick/keyboard port's data-direction register for
keyboard scanning, which — on real CIA hardware — silently breaks
joystick reading for the rest of the program: a pin configured as an
*output* reads back whatever was last *written* to it, not the actual
external switch state. Once any `READ_KEY` call had run, a later
`read_joy2` would read back a leftover keyboard column-select byte
instead of real joystick input, misinterpreted as directions and fire
being held — which could manifest as a stray sound effect firing with
nothing pressed, among other phantom input.

This is fixed as of this version: `read_joy2` and `READ_KEY` each
configure the direction register to whatever *they* need immediately
before using the port, so the two can be called in either order
without corrupting each other. There's nothing for a program using
this library to do differently — this section exists to document what
the bug looked like (an intermittent, seemingly random phantom input,
often a fire-button sound effect that fires on its own) in case it's
useful for recognizing a similar issue in code that doesn't use this
library.

## Worked example

`demo.asm` (alongside this file) uses all five libraries together —
text, input, graphics, and sound — in one small program. It's a genuine
integration test, not just a usage sample: it was assembled and its
output cross-checked byte-for-byte across all three implementations
(Python, single-file C, split multi-file C), the same way every other
feature in this project has been validated — and, since this project
added `mini6502.py` (see below), it's also been *executed*, not just
assembled: welcome text, the fire-button sound effect, the W-key engine
sound, the Y-key exit path, the bitmap/screen memory contents, and
`joy_state` staying correctly zero with nothing held were each
confirmed by actually running the program and inspecting the results,
not just by reading the listing. That's how all three bugs described
above — the `.basic`/`.include` entry-point bug, the
uncleared-bitmap-mode bug, and the DDRA/joystick-corruption bug — were
actually found; none was visible from the assembled bytes or a careful
listing read alone. `test_demo.py` (alongside `demo.asm`) is a
permanent regression test for all three.

## Testing your own programs with mini6502.py

`mini6502.py`, in this project's outputs alongside the library itself,
is a small 6502 CPU emulator with just enough C64-specific behavior
(CIA1 keyboard/joystick I/O, KERNAL `CHROUT`/`CHRIN` call trapping, and
a simulation of real KERNAL zero-page interference) to actually *run*
a `.prg` this assembler produces and check what it did — not just that
it assembled. If you're writing a program against this library, running
it through `mini6502.py` and asserting on the captured text output,
memory, or register state is a much stronger check than reading a
listing, for exactly the reason `demo.asm`'s bugs above show: neither
was visible from the assembled bytes alone. `test_demo.py` is a working
example of exactly this kind of test, including checking actual bitmap
and screen memory contents after the program runs, not just that it
completed without crashing.

## A note on how these are built

Where a routine is called with the same fixed inputs every time (like
`print_msg`, which always takes "an address" and does the same thing
with it), it's a real subroutine, called with `jsr`, so the code exists
once no matter how many times it's used. Where a routine's job is
fundamentally a short, parameterized sequence of register writes with
different constants baked in on each use (like `PLAY_SOUND`, or
`READ_KEY`, which needs a *compile-time* immediate value for its
`and #...` check — something a subroutine call can't easily pass), it's
a macro instead, expanded fresh at each call site. This mirrors the
distinction this project's own reference documentation draws between
the two — see `c64asm-reference.md` §8's introduction to macros for
more on that trade-off.

`print_msg`'s internal loop uses `@`-prefixed local labels (§9), so its
labels can never collide with anything else in a program that includes
it, no matter how many other things are also called `loop` or `done`.
