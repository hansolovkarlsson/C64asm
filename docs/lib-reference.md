# c64asm standard library

Eight `.include`-able files providing the register constants, text
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
| `lib/text.inc` | `PRINT msg`, `CLS`, `NEWLINE`, the `print_msg` subroutine they're built on, `str_equal` for comparing typed input against known keywords, and `SET_LOWERCASE_CHARSET`/`SET_UPPERCASE_CHARSET`/`DISABLE_CHARSET_SWITCH`/`ENABLE_CHARSET_SWITCH` for the runtime character-set switch this assembler's `.charset lower` directive needs paired with it — see `c64asm-reference.md` §6. |
| `lib/input.inc` | `CIA_KEYBOARD_SETUP`, `read_joy2`, `READ_KEY column, mask` for joystick/keyboard-matrix input; `read_line` and `extract_word` for reading and tokenizing a typed line via `CHRIN`. |
| `lib/keyboard.inc` | Named `KEY_<NAME>_COL`/`KEY_<NAME>_ROW`/`KEY_<NAME>_CODE` constants for every key on the keyboard matrix (pairs with `input.inc`'s `READ_KEY`), and `wait_any_key` for a blocking "press any key" read that returns which key in A. |
| `lib/graphics.inc` | `BITMAP_MODE_ON addr`, `BITMAP_MODE_OFF`, `CLEAR_BITMAP addr`, `SET_SCREEN_COLOR value`, `SPRITE_INIT data, color, x, y` for bitmap/sprite setup; `wait_frame` for raster-synced timing; `sprite0_bounce_step` for animating a sprite bouncing within a rectangular area; `sprite0_explode` for a caller-colored expand-flash-hide effect (an explosion, a hit, anything that needs a sprite to visibly go away). |
| `lib/sound.inc` | `SID_INIT`, `PLAY_SOUND freq_hi, ad, sr, waveform`, `engine_sound_on`/`engine_sound_off`. |
| `lib/music.inc` | `MUSIC_INIT melody_wave, melody_ad, melody_sr, bass_wave, bass_ad, bass_sr`, `music_tick` (call once per frame), `music_stop`. A two-voice sequencer, not a fixed sound effect — the note data (frequency/duration tables) is the caller's own, the same way this file doesn't provide the tune itself, only the player. See `music_demo.asm` for a complete, real worked example. |
| `lib/math.inc` | `MULT_2`/`MULT_4`/`MULT_8`/`MULT_16` and `DIV_2`/`DIV_4`/`DIV_8`/`DIV_16` — multiply or (truncating, unsigned) divide A in place by a small power of two, via left/right shifts (the 6502 has no multiply or divide instruction); need no zero page. Also `MULT_3`/`MULT_5`/`MULT_6`/`MULT_7`/`MULT_9`/`MULT_10`/`MULT_12` for the smallest non-power-of-two sizes, which — unlike the power-of-two macros — need a 1-byte zero-page `mult_scratch` declared first (no non-power-of-two `DIV_N`; see the file's own header comment for why). Meant for indexing an array of `.struct`-sized records and the reverse — see `c64asm-reference.md` §10's "Indexing an array of records". |

## Using a library file

```
        .include "lib/text.inc"
```

resolved relative to the file that contains the `.include` line (see
`c64asm-reference.md` §14) — so if your project keeps `lib/` alongside
your source file, this line works unchanged regardless of what
directory you actually run `c64asm` from.

Some routines need a small amount of setup the including program must
provide, since which addresses are safe to use depends on what else
your program has already claimed. **Every item below is required as
soon as you `.include` that file — even if you never call the specific
routine that uses it.** `.include` splices in a file's code
unconditionally, the same as any other source; a routine's own body
still references its zero-page location whether or not your program
ever `jsr`s to it.

Every library file below checks for its own required symbols right at
the top, using `.ifdef`/`.error` (`c64asm-reference.md` §15), and
fails with a specific message naming exactly what's missing:

```
Assembly error: text.inc requires str_ptr (2-byte zero page) defined before this .include (lib/text.inc, line 79: .error "text.inc requires str_ptr (2-byte zero page) defined before this .include")
```

rather than a generic `Undefined symbol` error surfacing later, from
somewhere inside the routine that actually uses it. `.error` is
recoverable, so forgetting more than one required symbol at once shows
every missing one together, in a single run, not just the first.

- **`text.inc`** needs THREE 2-byte zero-page locations: `str_ptr`
  (used by `print_msg`/`PRINT`), and `cmp_ptr`/`kw_ptr` (used by
  `str_equal`) — all three, even if your program only ever calls
  `PRINT`.
- **`input.inc`** needs one 2-byte zero-page location, `word_dest_ptr`
  (used by `extract_word`) — even if your program only ever calls
  `read_joy2`/`READ_KEY`.
- **`keyboard.inc`** needs one 1-byte zero-page location,
  `key_scratch` (used by `wait_any_key`) — safe to alias with any
  other file's own required byte(s), since it's never live across a
  call to anything else in this library.
- **`graphics.inc`** needs one 2-byte zero-page location, `gfx_ptr`
  (used by `CLEAR_BITMAP`/`SET_SCREEN_COLOR`), plus eight more,
  unrelated, used by `sprite0_bounce_step`: `xpos` (TWO bytes — low/high
  byte of a 16-bit X position, since the screen's right edge is itself
  past 255), `ypos`, `xdir`, `ydir` (one byte each, none needing to be
  zero page — see the file's own header comment) and `XMIN`, `XMAX`,
  `YMIN`, `YMAX` (compile-time constants; `XMAX` will normally need to
  be greater than 255 for a sprite that should reach the right portion
  of the screen) — all of it, even if your program only ever calls
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
xpos = $06              ; sprite0_bounce_step's state -- xpos needs
                           ; TWO bytes (xpos and xpos+1); see
ypos = $0b                  ; graphics.inc's header comment for why
xdir = $0c                    ; none of these need to be zero page
ydir = $0e
XMIN = 24                ; sprite0_bounce_step's bounds -- any
XMAX = 320                 ; values work if you're not actually
YMIN = 50                    ; using it yet, but see bounce.asm
YMAX = 229                     ; for a real, worked example
        .include "lib/text.inc"
        .include "lib/input.inc"
        .include "lib/sound.inc"
        .include "lib/graphics.inc"
```

`$02`–`$0F` and `$FB`–`$FE` are the community-documented zero-page
range this project's own demos use for exactly this kind of scratch
storage (see `c64-memory-reference.md`).

Forgetting one (or several) now looks like this, rather than a single
`Undefined symbol` error from deep inside a routine you never called
directly:

```
$ python3 c64asm.py mygame.asm -o mygame.prg --lib-dir lib
Assembly error: graphics.inc requires gfx_ptr (2-byte zero page) defined before this .include (lib/graphics.inc, line 81: .error "graphics.inc requires gfx_ptr (2-byte zero page) defined before this .include")
Assembly error: graphics.inc requires XMAX (a compile-time constant) defined before this .include (lib/graphics.inc, line 99: .error "graphics.inc requires XMAX (a compile-time constant) defined before this .include")
2 errors.
```

### A note on `--warn-unused` and this library

`c64asm-reference.md` §21 documents `--warn-unused`, a flag that warns
about every symbol defined but never referenced. By default it's
scoped to your own main file, not anything it `.include`s — which
matters here specifically, since `.include`ing a file defines
*everything* in it whether your program uses all of it or not, and
this library defines a lot (`keyboard.inc` alone is 192 constants for
keys most programs never check). Plain `--warn-unused` skips all of
that automatically, so it stays useful even on a program built
entirely on this library: both `demo.asm` and `adventure.asm` (this
project's own demos) report zero unused symbols with plain
`--warn-unused`, despite using only a fraction of what the library
defines.

`--warn-unused-all` removes that scoping if you ever want the full
picture — auditing the library itself, say, rather than a program
built on it. Expect it to be noisy: concretely, `--warn-unused-all`
against `demo.asm` reports 184 unused symbols, almost entirely
`keyboard.inc` entries for keys it doesn't use. That's expected, not a
sign anything's wrong — it's just not the question plain
`--warn-unused` is trying to answer.

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

### Lowercase text needs two things, not one

Writing `.charset lower` before a `.text`/`.asc`/`.byte` line (see
`c64asm-reference.md` §6, "Text and PETSCII") changes which PETSCII
bytes get assembled, but by itself that's not enough to actually
*see* lowercase letters on screen — the C64 also needs to be
switched, at **runtime**, onto its lowercase/uppercase character set,
which is a completely separate piece of hardware state the assembler
has no way to touch. Forgetting the second part is an easy mistake:
the program assembles fine, runs fine, and the text just silently
comes out looking exactly like it always did (uppercase), with
nothing about that pointing at "you forgot a step."

The fix is to call `SET_LOWERCASE_CHARSET` before printing anything
assembled under `.charset lower`:

```asm
        .charset lower
hello_msg:
        .text "Hello, World!"
        .byte 13, 0

start:
        SET_LOWERCASE_CHARSET     ; switch the actual hardware character
                                     ; set -- without this line, hello_msg
                                     ; still displays in uppercase
        PRINT hello_msg
```

`SET_UPPERCASE_CHARSET` switches back. `DISABLE_CHARSET_SWITCH` /
`ENABLE_CHARSET_SWITCH` stop/allow the player's own CBM+SHIFT keypress
from changing it back unexpectedly mid-program — call
`DISABLE_CHARSET_SWITCH` right after `SET_LOWERCASE_CHARSET` if your
program wants to guarantee the character set stays put for the rest
of the run.

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

### A `sprite0_bounce_step` bug this library used to have (now fixed, no action needed)

An earlier version of `sprite0_bounce_step` tracked X as a single byte
(0-255). That's not a bug by itself — plenty of sprites never need to
go further right than that — but it quietly capped how far right a
sprite bouncing off the screen edges could actually go, since the
visible screen's right edge sits at X≈344, well past 255. There was no
error, no crash, nothing wrong in a listing: the ball in `bounce.asm`
simply stopped and reversed noticeably short of the right edge (and,
separately, a bit short of the bottom edge too, before `YMAX` was
recalibrated against the actual screen dimensions) — the kind of thing
that's only obvious watching the program actually run, not reading its
source.

This is fixed as of this version: `xpos` is now two bytes (a proper
16-bit X position), and `sprite0_bounce_step` maintains the sprite's
X-MSB bit (`$D010` bit 0) itself as X crosses 256, the same technique
`c64-memory-reference.md`'s "Positioning a sprite beyond X=255" shows
doing by hand. There's nothing for a program using this library to do
differently beyond the setup change already described above (`xpos`
needing two bytes, and `XMAX` now able to — and normally needing to —
exceed 255) — this section exists mainly so the symptom (a bounce that
stops well short of an edge, with no error anywhere) is recognizable if
something similar ever turns up in code that doesn't use this library.

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

### A `demo.asm` bug `keyboard.inc` was built to make impossible (now fixed, no action needed)

An earlier version of `demo.asm` checked for a "Y (restart)" keypress
using `READ_KEY %11111011, %00000001` — which, verified against the
keyboard matrix (see `keyboard.inc`'s header comment and
[the standard published reference for it](https://sta.c64.org/cbm64kbdlay.html)),
is actually the matrix position of the **5** key, not Y. The
corresponding test in `test_demo.py` had the identical value baked in
as "hold Y to exit promptly," so the test passed — it was quietly
exercising the 5 key and calling it Y, the same mistake on both sides,
which is exactly why it went unnoticed.

This is fixed as of this version: `demo.asm` now uses
`READ_KEY KEY_Y_COL, KEY_Y_ROW` from `keyboard.inc`, and
`test_demo.py`'s simulated keypresses were corrected to match. This is
precisely the class of mistake `keyboard.inc` exists to prevent —
writing out a `%binary` literal by hand for a specific key is easy to
get subtly wrong in a way that still assembles fine and still "mostly
works" (any single wrong key still toggles based on *some* real key,
just not the one the comment says), where a named constant would
either be right or fail to assemble at all.

(A later version of `demo.asm` replaced Y with Q for exiting
altogether, once Y stopped being the only other key in use besides
W — see "Worked example" below for the current control scheme. The
`KEY_Y_COL`/`KEY_Y_ROW` mistake and fix described above is still
accurate history, just no longer matches `demo.asm`'s present-day
source.)

### Using `keyboard.inc`

```asm
key_scratch = $fb
        .include "lib/keyboard.inc"
        .include "lib/input.inc"

        ; Polling style, via input.inc's READ_KEY -- checks one
        ; specific key right now, doesn't wait:
        READ_KEY KEY_SPACE_COL, KEY_SPACE_ROW
        bne space_is_held

        ; Blocking style, via keyboard.inc's wait_any_key -- pauses
        ; the whole program until *some* key is pressed, then reports
        ; which one:
        jsr wait_any_key
        cmp #KEY_Y_CODE
        beq handle_yes
        cmp #KEY_N_CODE
        beq handle_no
```

Reach for `READ_KEY` when the rest of the program needs to keep
running while checking for a key (the usual case in a game's main
loop — see `pong.asm`/`lander.asm`). Reach for `wait_any_key` for a
"press any key to continue" pause, or any other spot where blocking
completely until *some* key is pressed is genuinely what's wanted.

## Worked example

`demo.asm` (alongside this file) uses all six libraries together —
text, input, keyboard, graphics, and sound (plus hardware, which the
others all pull in themselves) — in one small program. It's a genuine
integration test, not just a usage sample: it was assembled and its
output cross-checked byte-for-byte across all three implementations
(Python, single-file C, split multi-file C), the same way every other
feature in this project has been validated — and, since this project
added `mini6502.py` (see below), it's also been *executed*, not just
assembled: welcome text, the fire-button sound effect, W/A/S/D moving
the star with a short sound on each successful move, the border stop
at each edge, Q exiting cleanly, the bitmap/screen memory contents,
`joy_state` staying correctly zero with nothing held, and bitmap mode
genuinely not starting until a key is pressed — giving a player time
to actually read the welcome/instruction text `wait_any_key` pauses
on — were each confirmed by actually running the program and
inspecting the results, not just by reading the listing. That's how
all three bugs described above — the `.basic`/`.include` entry-point
bug, the uncleared-bitmap-mode bug, and the DDRA/joystick-corruption
bug — were actually found; none was visible from the assembled bytes
or a careful listing read alone. `test_demo.py` (alongside `demo.asm`)
is a permanent regression test for all of it, including a technique
worth knowing about if you write your own frame-stepped tests against
`wait_frame`-based code: mini6502 doesn't simulate the VIC-II's raster
line advancing on its own, so `test_demo.py` (like `test_bounce.py`
and `test_pong.py` before it) steps the CPU one instruction at a time
and pokes `$d012` to the value `wait_frame` polls for whenever
execution reaches it, advancing one simulated "frame" per `main_loop`
iteration instead of hanging forever on the busy-wait.

`bounce.asm` is a smaller, more focused example of the same idea:
`graphics.inc` (bitmap/sprite setup, `wait_frame`, `sprite0_bounce_step`)
composed with `sound.inc` (`PLAY_SOUND`) to play a sound effect exactly
on the frames the ball hits a wall, and not on any others. Since
`sprite0_bounce_step` reports which axis (if either) bounced via the
CPU's own X/Y registers rather than a shared flag graphics.inc and
sound.inc would both need to know about, the two library files stay
completely independent of each other — see `graphics.inc`'s own
comment on `sprite0_bounce_step` for the exact pattern, and
`test_bounce.py` for a regression test that checks the sound fires on
every single frame a bound is hit and never on any frame it isn't,
across hundreds of simulated frames — not just that it fires *at all*.

`pong.asm` is the clearest case yet of refactoring onto this library
catching a real bug rather than just removing duplication. Its own,
hand-rolled joystick reading left CIA1's port A permanently configured
for keyboard column-select output from setup onward, so reading the
joystick afterward silently read back stale column-select data instead
— the player paddle drifted downward on its own with nothing held or
the joystick unplugged, indistinguishable at a glance from "the AI is
just very good." Switching to `input.inc`'s `read_joy2`/`READ_KEY`
(which set CIA1's direction register to whatever they specifically
need immediately before each read, rather than trusting a one-time
setup to have left it right) fixed this outright. `test_pong.py`
checks the paddle now stays perfectly still with nothing held, and
(at the time that fix was made) separately cross-checked the
refactored file against the pre-refactor one under *real*, deliberate
joystick input to confirm only the bug had changed and nothing else
about how the game played.

A second, separate `pong.asm` fix followed the same pattern bounce.asm
needed: the right paddle and the net both stopped noticeably short of
where they should be, using only about 2/3 of the screen's width, for
the exact same reason bounce.asm's ball did -- the true right edge
sits at X=344, past what fits in a single byte, and both the paddle's
X position and the ball's own X bounds were kept under 256 to avoid
needing the X-MSB register. Unlike the ball in bounce.asm, the right
paddle never moves in X, so its X-MSB bit only needs setting once, at
startup, not maintained every frame the way `sprite0_bounce_step`
maintains the ball's own bit in that file -- pong.asm's ball *does*
move in X every frame here too (it tests against paddles, not just
walls, so it doesn't reuse `sprite0_bounce_step` directly), and
maintains its own X-MSB bit in `update_sprites` using the identical
technique. `test_pong.py` checks the ball actually reaches the
corrected `BALL_XMAX` (now past 255), that `SPRITE0_X`/the X-MSB bit
stay in sync with the real 16-bit ball position every frame, and that
the right paddle and net both ended up in the right place.

A third fix, same axis of "stops short of the true edge" but on Y this
time: `PADDLE_YMIN`/`PADDLE_YMAX` had originally been calibrated only
to the *minimum* needed for ball coverage (a paddle positioned at
`PADDLE_YMAX` could still, with `PADDLE_RANGE`'s help, reach a ball all
the way at the bottom wall) -- a different thing from the paddle's own
sprite being able to visually reach the true bottom of the screen.
Recalibrating them the same way as `YMIN_WALL`/`YMAX_WALL` (the paddle
is the same height as the ball) surfaced a genuinely new, more subtle
bug: when a point is scored, the ball serves from roughly the missing
paddle's own center (`paddle_y + 8`), and once `PADDLE_YMAX` and
`YMAX_WALL` sat close together, that serve position could land at or
past `YMAX_WALL` itself -- serving the ball already at (or past) the
wall it's about to test against, moving toward it. `move_ball`'s own
wall-bounce check only fires the instant `ball_y` *becomes*
`YMAX_WALL` via increment, so a ball that starts there already, still
moving down, sails straight past 255 and wraps. The fix clamps the
served position to strictly less than `YMAX_WALL`, not "close enough":
landing exactly on it has the identical bug even without any actual
clamping being involved, since `PADDLE_YMAX + 8` can coincide with
`YMAX_WALL` on perfectly ordinary, non-clamped serves too, depending
on exactly where the paddle happened to be. Coverage was re-verified
with the same kind of exhaustive Python simulation used for the
original `PADDLE_YMAX` bug during this program's initial development,
checking every `ball_y`/`paddle_y` combination directly rather than
trusting the arithmetic alone.

`lander.asm` is where `graphics.inc`'s `BITMAP_MODE_OFF` and
`sound.inc`'s `engine_sound_on`/`engine_sound_off` were originally
extracted from, word for word, so bringing it onto the library mostly
means deleting its own copies of things and calling the shared ones
instead — including `update_engine_sound`, which shrinks from a full
copy of the gate-retriggering logic to a three-line dispatcher that
just decides which of `engine_sound_on`/`engine_sound_off` to call
each frame. The one genuinely new library addition this pass adds is
`sprite0_explode` (see the Files table above), generalized from this
file's own `show_explosion` — the sprite expand/color-cycle/hide
mechanics stayed in the library, while the specific fire-colored
sequence and the crash sound that accompanies it stayed here, passed
in as a plain color table.

Refactoring the joystick reading onto `read_joy2`/`READ_KEY` fixed the
same class of bug found in `pong.asm`, and here the visible symptom
was worse: with nothing held, fuel drained and the ship visibly
drifted sideways on every single flight, silently eating into (or
outright destroying) the fuel margin the whole game's balance was
tuned around. `test_lander.py` checks fuel and horizontal position
both stay perfectly constant with nothing held, while gravity (a
vertical fall, not "input" at all) still correctly isn't affected —
and separately drives both the success-landing and crash paths
directly by placing the ship at a specific position/speed rather than
trying to fly a full simulated approach, checking the exact color
sequence `sprite0_explode` produces on a crash, that both outcomes
correctly leave bitmap mode, and that Y correctly restarts the program
afterward.

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
completed without crashing. `test_sprites.py` (alongside `sprites.asm`)
is another: it reads `star_anim.bin` itself and compares those exact
bytes against what ended up in the running program's memory at each
frame's address, confirming `.incbin` actually pulled the right slice
of the file into the right place — not just that the byte *count*
came out right, which a less careful test could pass on accident.

Testing `keyboard.inc`-based code works the same way `press_key`
already does for `read_joy2`/`READ_KEY` elsewhere in this file's own
tests: `m.press_key(0b11110111, 0b00000010)` (the same COL/ROW values
`KEY_Y_COL`/`KEY_Y_ROW` expand to — a Python test can't reference an
assembler-time symbol by name, so write out the matching binary
literal, ideally with a comment naming which key it is, the same way
`test_demo.py`'s own `press_key` calls already do) simulates holding
that key before calling `run_until_return`. For `wait_any_key`
specifically, checking the returned matrix code against the matching
numeric value (`KEY_Y_CODE` is 25, computed as `column_index*8 +
row_index` — see `keyboard.inc`'s header comment for the full table)
is the same idea `test_demo.py`'s existing assertions already use for
other return values.

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

`print_msg`'s internal loop uses `@`-prefixed local labels (§13), so its
labels can never collide with anything else in a program that includes
it, no matter how many other things are also called `loop` or `done`.
