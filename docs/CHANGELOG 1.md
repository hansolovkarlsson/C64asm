# Changelog — C64 Assembler & Tools Project

Work log for the first extended chat session on this project. Organized
chronologically by topic. The assembler (`c64asm.py` / `c64asm.c` /
`c64asm-split-src.zip`) is maintained as three independent
implementations that must stay byte-for-byte identical in their output;
every entry that touches the assembler itself was verified across all
three unless noted otherwise.

---

## `.include` search path: `--lib-dir` option

**Added** a `--lib-dir <dir>` command-line option so a single shared
`lib/` directory can be reused across multiple separate project
folders, instead of every project needing its own copy sitting next to
its source. It's a pure fallback — the existing relative-to-includer
resolution is always tried first and always wins if it finds the file,
so passing `--lib-dir` is safe even for projects that keep their own
local `lib/`.

**Fixed** a bug in the same feature shortly after shipping it:
`--lib-dir` was resolving paths relative to the *parent* of the `lib/`
folder instead of the folder itself, so `--lib-dir $(PWD)/lib` with
`.include "lib/text.inc"` looked for `$(PWD)/lib/lib/text.inc` — one
`lib` too many. Fixed by stripping a leading `lib/` from the `.include`
path before joining it with `--lib-dir`, so the option now points
directly at the folder holding `text.inc`, `input.inc`, etc.

---

## `adventure.asm`: missing newline bug

**Fixed**: the response to a typed command was printing on the same
screen line as what the player had just typed and pressed Enter on
(`> GO NORTHYou are standing...` instead of a line break in between).

**Added** `NEWLINE` macro to `text.inc` (prints PETSCII `$0D`) as the
general-purpose fix, alongside the existing `CLS`. `adventure.asm` now
calls it right after `read_line`, before printing any response.

---

## `bounce.asm`: moved onto the standard library

**Refactored** `bounce.asm` to use `lib/graphics.inc` instead of its
own hand-rolled bitmap/sprite setup and raster-sync code.

**Added** to `graphics.inc`: `BITMAP_MODE_ON`/`BITMAP_MODE_OFF`,
`CLEAR_BITMAP`, `SET_SCREEN_COLOR`, `SPRITE_INIT` macros; `wait_frame`
subroutine (this one had been independently duplicated byte-for-byte in
`bounce.asm`, `pong.asm`, *and* `lander.asm` before extraction);
`sprite0_bounce_step` subroutine for animating a sprite bouncing within
a rectangular boundary.

### Bug: sprite stopping short of the true screen edges

**Fixed**: the ball visibly stopped and reversed direction well before
reaching the right and bottom edges of the screen (~2/3 across instead
of the full width). Root cause: sprite X position was tracked in a
single byte (0–255), but the visible screen's right edge sits at
X≈344, past what a byte can hold.

**Added** `SPRITE_X_MSB` constant to `hardware.inc` (was missing).
`sprite0_bounce_step` now tracks X as a full 16-bit value and maintains
the sprite's X-MSB bit (`$D010`) itself every frame. Y bound
recalibrated too (didn't need MSB, just a miscalibrated constant).

### Sound effect added

`bounce.asm` now plays a sound effect on wall hits, using the new
`sound.inc` (`PLAY_SOUND`). Since `sprite0_bounce_step` lives in the
library and the actual bounce logic happens inside it, it now reports
*which* axis (if either) bounced via the CPU's otherwise-unused X/Y
index registers on return — letting `bounce.asm` decide whether to play
a sound without `graphics.inc` needing any dependency on `sound.inc`.

---

## `pong.asm`: moved onto the standard library

**Refactored** `pong.asm` to use `lib/graphics.inc` (`wait_frame`),
`lib/input.inc` (`read_joy2`, `READ_KEY`, `CIA_KEYBOARD_SETUP`), and
`lib/sound.inc` (`SID_INIT`, `PLAY_SOUND`) instead of its own copies.

### Bug: CIA data-direction-register / joystick corruption

**Fixed** a real, previously unnoticed bug: `pong.asm` left CIA1 port A
permanently configured as keyboard-column-select output from setup
onward, so reading the joystick afterward silently read back stale
keyboard-scan data instead. Symptom: the player paddle drifted downward
on its own with nothing held — easy to mistake for "the AI is just very
good." Fixed by switching to `read_joy2`/`READ_KEY`, which each set
CIA1's direction register to what they need immediately before reading,
rather than trusting a one-time setup. Verified the fix changed
*nothing* else about game logic by cross-checking scores bit-for-bit
against the pre-refactor version under real, deliberate joystick input.

### Bug: play field using only ~2/3 of the screen width

**Fixed**: same root cause as `bounce.asm`'s edge bug — `RIGHT_PADDLE_X`
and the ball's X bounds were kept under 256 to avoid needing the X-MSB
register. `RIGHT_PADDLE_X` is now 320 (X-MSB set once at startup, since
the paddle never moves in X); the ball's X position is now a full
16-bit value with its own X-MSB bit maintained every frame in
`update_sprites`. Net recentered from column 14 to column 20 (the
screen's true center). Score positions rebalanced to stay symmetric.

### Bug: paddle stopping short of the top/bottom edges, plus a serve-overshoot bug it exposed

**Fixed**: `PADDLE_YMIN`/`PADDLE_YMAX` had only ever been calibrated to
the *minimum* needed for ball-interception coverage, not to let the
paddle's own sprite reach the true top/bottom edges. Recalibrating
these surfaced a second, subtler bug: once the paddle could reach close
enough to the bottom wall, a served ball could land exactly on that
wall while still heading toward it — past where the wall-bounce check
(built to catch a ball only at the instant it *arrives* there) could
detect it — and wrap around instead of bouncing. Fixed by clamping the
served position to strictly less than the wall, not just "close
enough" (landing exactly on it has the identical bug even without
clamping, on perfectly ordinary non-clamped serves too). Re-verified
paddle coverage with an exhaustive Python simulation checking every
`ball_y`/`paddle_y` combination before touching assembly, matching the
project's own established practice.

---

## `lander.asm`: moved onto the standard library

**Refactored** `lander.asm` to use `lib/graphics.inc`
(`BITMAP_MODE_ON`/`OFF`, `CLEAR_BITMAP`, `SET_SCREEN_COLOR`,
`SPRITE_INIT`, `wait_frame`), `lib/input.inc` (`read_joy2`, `READ_KEY`,
`CIA_KEYBOARD_SETUP`), `lib/sound.inc` (`SID_INIT`, `PLAY_SOUND`,
`engine_sound_on`/`off`), and `lib/text.inc` (`print_msg`/`PRINT`,
`CLS`). Several of these — `BITMAP_MODE_OFF`, `engine_sound_on`/`off`
— were originally extracted *from* this exact file's code in earlier
work, so this was largely deleting duplicate copies.

### Bug: same CIA/joystick bug as `pong.asm`, found independently

**Fixed**: the identical root cause as `pong.asm`'s bug above, but
worse here — fuel drained and the ship visibly drifted sideways with
nothing held, on every single flight, silently eating into (or
destroying) the fuel margin the game's whole balance was tuned around.
Same fix (`read_joy2`/`READ_KEY`).

### New library routine: `sprite0_explode`

`show_explosion`'s visual effect (double the sprite, flash through a
color sequence, hide it) wasn't duplicated elsewhere in the project,
but is a genuinely reusable primitive. **Extracted** into
`graphics.inc` as `sprite0_explode`, taking the color sequence as a
plain table (address + count) so the mechanics live in the library
while the specific fire-colored sequence and crash sound stay local to
`lander.asm`. **Added** `SPRITE_X_EXPAND`/`SPRITE_Y_EXPAND` constants
to `hardware.inc` (were missing).

---

## Library documentation: "Requires" quick-reference headers

**Added** a scannable "Requires" block to the top of each of the five
library `.inc` files (right after "Provides"), listing exactly which
labels/constants a caller must declare before `.include`ing that file,
their size, whether they need to be zero page, and which routine
actually uses them — `hardware.inc` (needs nothing), `text.inc`
(`str_ptr`, `cmp_ptr`, `kw_ptr`), `input.inc` (`word_dest_ptr`),
`graphics.inc` (`gfx_ptr` plus the `sprite0_bounce_step` group:
`xpos`/`ypos`/`xdir`/`ydir`/`XMIN`/`XMAX`/`YMIN`/`YMAX`), `sound.inc`
(`engine_playing`). The longer prose explanation of *why* (the
unconditional-assembly gotcha, aliasing safety) stays below each block
for anyone who wants it; the quick answer is now right at the top.

**Found and fixed while verifying these were pure comment changes**: the
shipped `.prg` files for `bounce.asm`, `pong.asm`, and `demo.asm` were
stale, left over from before `graphics.inc` grew during the
`lander.asm` work — a library file growing shifts assembled addresses
in *any* program that includes it, whether or not that program calls
the new routine. Only `lander.prg` had been re-shipped at the time.
Fixed by re-assembling and re-verifying every `.prg` in the package
against a fresh assembly of its own source.

---

## Assembler feature: multi-error reporting — **IN PROGRESS, not fully shipped**

User was offered a menu of possible assembler extensions (multi-error
reporting, illegal/undocumented opcodes, lowercase PETSCII, auto-unique
macro labels, `.incbin`, symbol/map file export) and chose multi-error
reporting: collecting and reporting several independent mistakes per
run instead of stopping at the first one.

**Design**: errors are split into "stays fatal" (file I/O, circular
`.include`, macro/conditional-assembly structural problems — whole-file
issues where "skip and continue" doesn't make sense) and "becomes
recoverable" (undefined symbols, malformed expressions, unsupported
addressing modes, branch-out-of-range, symbol redefinition, unknown
mnemonics — self-contained, per-line problems). Recoverable errors are
recorded and the code supplies an explicit safe fallback value right at
each call site and continues normally — no stack unwinding needed. If
pass 1 records any errors, pass 2 is skipped (its results would be
worthless downstream noise). Capped at 20 collected errors with a
"... and N more" summary.

**Found and fixed two latent crash bugs** while making these paths
reachable instead of instantly fatal: `.align 0` fell through to a
division by zero (silent UB in C, would have been an uncaught
`ZeroDivisionError` in Python); an unsupported addressing mode fell
through to a null-pointer dereference (C) / `KeyError` (Python) before
the fix.

**Status:**
- `c64asm.py` — **done, shipped**, edited in place in
  `/mnt/user-data/outputs/`
- `c64asm.c` (single-file) — implemented and tested, but the working
  copy has **not yet been copied over** the shipped file
- split-source project — implemented and tested, but has **not yet
  been re-zipped** into the shipped `c64asm-split-src.zip`
- Documentation (`c64asm-reference.md` §16, `README.md`'s known-limitations
  list, both of which currently still describe the old single-error
  behavior) — **not yet updated**

All three implementations were verified to produce byte-for-byte
identical error text for the same test cases, and the full 112-check
project regression suite was re-run against the modified `c64asm.py`
with no failures — but this verification happened in scratch working
directories, not against the files actually sitting in
`/mnt/user-data/outputs/` for the C implementations. See the handoff
summary from earlier in this session for exact next steps and file
locations.

**Also noted, not fixed** (pre-existing, unrelated to this feature):
Python's `parse_operand()` never receives or passes the source line's
raw text to its error calls the way both C implementations do, so a
handful of Python error messages show only `(line N)` instead of
`(line N: source text)`. Predates this session's work; flagged as a
possible small follow-up.
