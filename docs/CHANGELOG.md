# Changelog

Notable changes to c64asm, newest first. This project doesn't use
version numbers or dated releases, so entries are grouped by feature
rather than by version — each one names what changed and points at
where it's documented in full (`c64asm-reference.md`, `README.md`, or
`lib-reference.md`).

Every entry below shipped identically across all three implementations
(the Python reference `c64asm.py`, the single-file C `c64asm.c`, and
the 14-module split-source C in `c64asm-split-src.zip`) and passed
this project's full regression suite before being marked done — that
discipline is this project's own standing practice, not something
worth repeating in every entry below.

## `editor.asm`: RUN/STOP cancels F3/F4/F5's own prompts

F3 (save), F4 (delete), and F5 (load) can now all be backed out of
mid-prompt without side effects, not just by pressing RETURN with
nothing typed. The C64 keyboard has no key labeled ESC; RUN/STOP
(PETSCII $03, also reachable as Ctrl+C) is the conventional C64
equivalent for "abort this," so that's what cancels here too --
during the filename prompt itself, and during F4's own Y/N
confirmation. Implemented by forcing the typed-length counter to zero
and falling into the same exit point an empty RETURN already uses, so
every caller's existing cancellation check handles it without any
separate code path. Confirmed RUN/STOP remains harmless during
ordinary typing in the main editing loop, where it isn't checked for
at all and simply falls through to being ignored like any other
unhandled control code, the same as before this change.

## `editor.asm`: F4 (delete), and fixed saving over an existing file

Two related changes, both built on the same mechanism. First, the
fix: loading a file, editing it, and saving under the same name
wasn't actually updating the file on disk at all -- real, well-
documented CBM DOS behavior, not a bug in the KERNAL calls themselves.
The drive refuses to write to a filename that already exists (a real
`63, FILE EXISTS` error) unless told otherwise. CBM DOS offers a
shortcut for this (`@0:name,S,W`, "save and replace"), but it has a
well-documented data-corruption bug on original 1541 firmware, fixed
only in later revisions (the 1541-II and 1571) -- not something a
program can detect or route around at the KERNAL call level, and
serious enough that CBM DOS references consistently recommend against
it. Fixed instead by `SCRATCH`ing the existing file first, then
writing fresh -- two plain, well-understood operations instead of the
buggy shortcut.

Second, F4: a delete command, built directly on that same `SCRATCH`
mechanism now that it exists. Unlike F2 (new) and F5 (load), which
overwrite the in-memory document without confirmation, F4 asks first
(Y/N) -- deleting a file from disk has no "just reload it" recovery
path the way an unsaved screen does, so the difference in stakes is
real, not just an inconsistency for its own sake.

A real, exact-value parsing bug came up while building this, caught
by testing against a genuinely wrong case rather than only the happy
path: the command channel's response to a SCRATCH command reports the
count of files actually deleted as a zero-padded two-digit field
(`01,FILES SCRATCHED,00,00` for none, `...,04,00` for four), and an
early version only checked for a bare `0` immediately followed by a
comma -- which never actually appears, since the field is always two
digits, so `"00"` was being misread as a nonzero count. The practical
effect: deleting a file that didn't exist was reported as `DELETED.`
instead of `FILE NOT FOUND.` Fixed by checking that both digits are
zero, which is what the field format actually guarantees. Testing this
meant extending `mini6502.py`'s command-channel simulation to actually
process a SCRATCH command against `self.disk_files` (removing the
matching entry and reporting a real count) rather than just returning
a fixed status string, which is also what made the original
overwrite bug possible to reproduce and verify fixed in the first
place, rather than assumed.

See `editor.asm`'s own header comment for the complete reasoning,
including why deleting a file briefly interrupted mid-operation can
still lose data (an inherent tradeoff of avoiding the buggier "@0:"
shortcut, not a flaw specific to this approach).

## `editor.asm`: F2 (new file)

The last of the five functions planned for this editor: F2 clears the
document back to blank and resets the cursor to (0,0), reusing the
same 4×240-byte loop shape `write_screen_to_file`/`read_file_to_screen`
already established for touching exactly the 960-byte editable area
(rows 0-23) and nothing else, so it can't disturb the status line the
same way an early version of the save code once did. No confirmation
prompt before clearing -- deliberately consistent with F5 (load),
which already overwrites the current document without asking first;
adding a confirmation only here would be a second, inconsistent rule
rather than a genuinely safer one. The status line's help text is
abbreviated to `F7=DIR` (from `F7=DIRECTORY`) to fit all five commands
in the 40-column row; the intro screen, which wraps naturally through
ordinary `PRINT`/`CHROUT`, still spells it out in full.

## Fixed: directory listing needs secondary address 0, not an arbitrary data channel

Root cause found and fixed, confirmed against real hardware via
`dir_sa_test.asm`: the special "$" directory request only produces a
well-formed listing when opened with secondary address 0 (matching
BASIC's own `LOAD"$",8`) -- unlike an ordinary file, where any value
2-14 works equally well as a data channel. `dir_demo.asm`,
`dir_raw.asm`, and `editor.asm`'s own directory code all used
secondary address 4, based on the general data-channel rule, which
was never actually correct for this specific request. Real hardware
showed exactly this: secondary address 0 returned the expected
`01 04 01 01 00 00 12 22...`; both 2 and 4 returned the same garbage
(`41 00` then a repeating 4-byte pattern), with `READST` never once
flagging an error the entire time -- which is what made this
genuinely hard to isolate rather than obviously wrong, and why it took
three rounds of narrowing (a raw byte dump, a drive-health check over
the command channel, then a direct secondary-address comparison) to
actually pin down instead of guessing.

Fixed the secondary address in all three files, and while there,
ported `dir_demo.asm`/`dir_raw.asm`'s other two fixes back into
`editor.asm`'s own directory code, which had never received them: it
now checks `OPEN`'s carry flag instead of assuming success, stops the
text-reading loop cleanly on `READST` rather than potentially hanging
on a missing terminator, and sends an explicit reverse-off after every
line.

The more important fix is in `mini6502.py` itself, not just this
project's own three `.asm` files: its virtual disk simulation didn't
distinguish directory reads by secondary address at all before this,
which is exactly why the original bug passed every test here and only
surfaced on real hardware. `_do_open` now only generates the
well-formed listing for secondary address 0; any other secondary
address with a "$" filename is modeled as a file that isn't there
(immediate EOF), not by reproducing the exact garbage bytes seen on
one real drive, since those specific bytes aren't something to treat
as a general, portable guarantee -- what matters for testing purposes
is that the wrong secondary address reliably does NOT produce a
well-formed listing, which this now enforces. Confirmed this actually
closes the gap by deliberately reverting the secondary address back to
4 and checking that the existing test suite now fails clearly instead
of passing -- it did, which is what "a test that would have caught
this" actually means, not just adding assertions and hoping.

## `dir_sa_test.asm`

A third diagnostic tool, testing a specific assumption that was never
actually verified: `dir_demo.asm`/`dir_raw.asm` always opened the "$"
directory request with secondary address 4, based on the general rule
that any value 2-14 is a valid data channel for an ordinary file --
but that rule describes reading a normal file, and this project never
actually confirmed the special "$" directory request behaves the same
way regardless of which secondary address is used. It was assumed,
not verified -- worth naming plainly, since `dir_status.asm` just
confirmed the drive itself is healthy ("00, OK,00,00"), which rules
out "no drive" as the explanation for `dir_raw.asm`'s garbage output
and points at exactly this kind of narrower, previously-unchecked
assumption instead. Notably, BASIC's own `LOAD"$",8` uses secondary
address 0 specifically, not an arbitrary data channel number.

Opens the directory with secondary addresses 0, 2, and 4 in turn and
prints the first 8 bytes each one returns, side by side, so they can
be compared directly rather than tested one at a time across separate
program runs.

## `dir_status.asm`

Another diagnostic tool: reads and prints the drive's own status
message off the command channel (secondary address 15), independent
of any directory-specific logic. Built directly in response to
`dir_raw.asm`'s real-hardware output -- "41 00" instead of the
expected "01 04" load address, then a tight repeating 4-byte pattern
for a full 128-byte dump, with `READST` never once flagging an error
the whole time. That combination -- the KERNAL apparently believing
the read is going fine while returning nonsense -- points away from a
parsing bug in this project's own code and toward the drive itself, or
how it's responding, which is exactly what the command channel exists
to report directly: every real disk operation writes its own result
there, whether or not a program asks for it, and a healthy drive
answers with a recognizable status string with no filename needed to
read it at all.

Testing this needed mini6502.py to grow actual command-channel
simulation (`drive_status`) -- secondary address 15 didn't do anything
special before this file needed it to.

Still an open, unresolved question: what this tool reports on the
actual hardware in question, and what that says about where the real
problem is.

## `dir_raw.asm`

A diagnostic tool, not a feature: dumps the raw bytes `OPEN`/`CHRIN`
return for a directory listing, in hex, with no interpretation at all.
Built because `dir_demo.asm`'s actual behavior on real hardware/VICE
("8192" printed, then two pi characters, then a hang) matched neither
its expected error message nor a well-formed listing, and `dir_demo`'s
own output -- which only ever shows its *interpretation* of the
bytes -- can't distinguish "the KERNAL returned something unexpected"
from "the parsing logic is misreading otherwise-correct data." This
shows the unfiltered truth instead: whether `OPEN`'s carry flag came
back set or clear, `READST` before any read, and every byte actually
returned, so the point where reality diverges from expectation is
visible directly rather than guessed at.

A real bug turned up while building this diagnostic tool itself, which
is worth calling out precisely because a diagnostic tool with its own
undetected bug is actively worse than no tool at all: an early version
read `READST`'s value, then called `PRINT` (which clobbers `A`
internally) before actually printing that value, producing
self-contradictory output ("STOPPED -- READST NONZERO: 00" -- nonzero
and 00 in the same line) that would have pointed straight at a wrong
conclusion. Fixed by saving the value across the `PRINT` call, the
same technique already used correctly for the `OPEN` error code right
next to it; the new regression test specifically checks the reported
value is real, not just present.

Still an open question, not a resolved one: whether what this tool
shows will actually explain `dir_demo.asm`'s behavior on real
hardware. That's the next thing to find out.

## `dir_demo.asm`

`editor.asm`'s disk directory listing, pulled out into its own
standalone program -- reported issues with `editor.asm` on real
hardware/VICE prompted breaking the problem down into smaller pieces
that are easier to test and debug in isolation, starting with this
one. Not a new feature so much as a diagnostic step: the smallest
program that can show whether something's actually wrong with how
this project reads a disk directory, separate from everything else
`editor.asm` does.

Re-reading the directory code for this surfaced two real gaps, both
invisible against mini6502.py's own virtual disk specifically because
that simulation's `OPEN` always succeeds and its generated listing is
always well-formed: the code never checked whether `OPEN` actually
succeeded (real `OPEN` signals failure via the carry flag -- no drive
present, no disk in it, device not responding -- and skipping that
check is exactly how a failed `OPEN` turns into reading garbage or
hanging instead of a clear error), and the text-reading loop had no
way to stop early if a stream ended before a `$00` terminator ever
arrived. Testing either gap needed mini6502.py to grow the ability to
simulate a missing drive (`device_present`) and a deliberately
malformed listing, neither of which existed before -- see
`mini6502.py`'s own `_do_open` comment. Also added an explicit
"reverse off" after every line, so the disk name line's own
reverse-video code can't visually carry into the rest of the listing
regardless of whether a real drive already sends one itself, which
public documentation on this specific point turned out to be
ambiguous about.

Whether these two fixes were the actual cause of what was seen on real
hardware isn't confirmed yet -- this is a step in an ongoing
back-and-forth, not a closed loop. If they turn out to help,
`editor.asm`'s own directory code should get the same fixes next.

## `editor.asm`: fixed F5 and F7 key codes

F5 did nothing, and F7 triggered load instead of the directory
listing. The actual bug: the four unshifted C64 function keys are
sequential PETSCII codes with no gaps -- F1=$85, F3=$86, F5=$87,
F7=$88 -- but the code used $88 for F5 (which is really F7) and $8A
for F7 (which is really shifted-F3/F4, not a key this editor even
listens for). Found from real use on hardware/VICE, not by this
project's own testing: the regression test used the exact same wrong
values the implementation did, so both agreed with each other and
neither caught the mismatch against the real keyboard. Re-verified the
correct codes against the same authoritative source used originally,
fixed both the code and the test's key constants, and added a new
check that presses each function key's real code in isolation and
confirms only its own intended action fires -- specifically so a
mistake shaped like this one can't pass silently again. See
`editor.asm`'s own header comment for the corrected key list.

## `editor.asm`: load, save, and directory listing

Real KERNAL disk I/O (`SETLFS`/`SETNAM`/`OPEN`/`CHKIN`/`CHKOUT`/
`CLRCHN`/`CLOSE`/`READST`, plus `CHRIN`/`CHROUT`'s file-redirected
behavior) added to the one-screen text editor, saving and loading the
document as a SEQ file and parsing a real disk directory listing's
byte format for F7. New status line (row 24; the editable area is now
rows 0-23, down from all 25) shows help text by default, a filename
prompt for F3/F5, and a one-line result ("SAVED.", "FILE NOT FOUND.",
"CANCELLED.") after each operation.

This is genuinely new ground for the project: every exact register
convention (`SETNAM`'s X=pointer-low/Y=pointer-high in particular,
easy to get backwards) and the directory listing's own byte structure
(a fake load address, then one BASIC-program-style "line" per file --
link pointer, a line number doubling as the block count, null-
terminated text) were verified against multiple independent,
authoritative sources before any 6502 code was written, not assumed
from memory.

**On testing**: this project has no way to test against a real IEC bus
or drive — mini6502.py's own emulation has never simulated disk
hardware. Rather than ship untested disk I/O, mini6502.py gained a
virtual file system trapping every KERNAL call this needed (see that
file's own header comment on `disk_files`), which is what the editor's
61-check regression test (up from 31) runs against. That's a real,
useful check — it confirms this program's own KERNAL call sequence,
register usage, and byte-for-byte file/directory-listing contents
match documented KERNAL conventions — but it is **not** the same as
testing against a real 1541 or VICE, which is still the right final
check before trusting this with anything you'd mind losing. That
boundary is documented directly in both `editor.asm`'s and
`mini6502.py`'s own header comments, not just here.

One real, meaningful bug came up during testing, not a cosmetic one: an
early version of `write_screen_to_file` copied the entire 1000-byte
screen, which meant saving a document also saved whatever was on the
status line at that exact moment — including, in one actual test run,
the tail end of the "SAVE AS:" prompt and the filename just typed into
it, baked directly into the saved file's own content. Fixed by
narrowing both the save and load copy loops to the 960-byte editable
area (rows 0-23) only, matching the row-24-is-not-part-of-the-document
design the status line itself introduced. `mini6502.py`'s own `_do_open`
was also adjusted after an initial design flaw: the first version only
flagged a missing file once something tried to read it, which left an
awkward ambiguity between "empty file" and "not found" for a caller
checking status right after `OPEN`; it now flags this immediately,
which is both simpler to model and simpler for `editor.asm`'s own load
logic to check.

See `README.md`'s file table and `editor.asm`'s own header comment for
the complete picture, including the exact SEQ filename-suffix
convention (`,S,W`/`,S,R`) and what F7's directory listing shows.

## `editor.asm`

A simple, one-screen text editor — the first step toward a planned
load/save/directory-listing follow-up, not a finished editor on its
own: it writes directly to screen memory, which is exactly what a
future "save" needs to read from (and a future "load" write back to),
so there's no separate document buffer that would need its own
conversion step added later.

Reads input via `GETIN` ($FFE4), the KERNAL's own keyboard-buffer
poll, rather than `lib/keyboard.inc`'s matrix-scanning `READ_KEY` --
right for a full-screen editor that needs Return/Delete/cursor keys as
ordinary keystrokes, not the single fixed key `READ_KEY` is built to
check. This needed real verification against an authoritative source
before writing any 6502 code, not assumed from memory: PETSCII and
screen memory use genuinely different byte values for the same
characters, and getting the conversion wrong would have meant garbage
displayed for whatever was actually typed. Cursor position is tracked
by a small row-address lookup table rather than a runtime multiply by
40 (not a power of two, and not one of `lib/math.inc`'s own small
non-power-of-two multipliers either) -- with only 25 possible rows,
precomputing was both simpler and faster than deriving a new multiply
routine for a number this specific to one screen layout.

Testing this needed a real extension to `mini6502.py` itself: only
`CHROUT`/`CHRIN` were emulated before, and `GETIN` -- essential for
this editor's whole design -- wasn't. Added a `getin_queue` mechanism
mirroring the existing `chrin_queue` one, and, while in there, fixed a
related pre-existing gap where neither trap actually set the CPU's
zero/negative flags after loading a value into `A`, which happened to
never matter before since nothing previously branched on the result of
a `CHRIN`/`GETIN` call the way `jsr GETIN` / `beq ...` (this editor's
own busy-wait) depends on. Purely additive to `mini6502.py` -- nothing
before this used `GETIN`, so there was no regression risk, confirmed
by the full existing test suite (292 checks across all eight demos)
passing unchanged. The editor's own regression test (31 checks) caught
one real mistake along the way, in the test itself rather than the
editor: an early version used $9D (cursor LEFT) where $1D (cursor
RIGHT) was intended, which looked at first like an editor bug before
the actual cause turned up.

## `lib/music.inc` and `music_demo.asm`

A two-voice SID music player, and a real demo built on it — "Twinkle
Twinkle Little Star" (public domain; the melody is the 18th-century
French folk tune "Ah! vous dirai-je, maman"), a sawtooth melody voice
over a triangle bass line, border color pulsing on the beat. The
library provides the sequencer (`MUSIC_INIT`, `music_tick` called once
per frame, `music_stop`); the actual note data — frequency and
duration tables for each voice — is the caller's own, the same way
`sound.inc` doesn't provide its own sound effects, only the mechanism
to play one. Frequencies were computed directly from the equal-
temperament/PAL-SID-clock formula rather than transcribed from a
reference table, and the demo's own regression test (103 checks)
recomputes every expected frequency independently, rather than
checking the assembled program's SID register writes against numbers
copied from the same place the `.asm` file's own data came from — a
transcription mistake in either the source data or the test would
actually be caught this way, not hidden by both sides agreeing by
construction. Confirmed correct frequency and gate-on/off state for
all 48 melody notes and all 6 bass notes, in order, including the
melody correctly looping back to its first note after a full cycle.
`wait_frame` is written fresh in the demo itself rather than pulling
in `graphics.inc` for it, which would otherwise mean satisfying nine
zero-page/RAM requirements this demo has no other use for. Also added
the previously-missing voice 2 and voice 3 SID register constants
(`VOICE2_FREQ_LO` and friends) to `hardware.inc`, matching voice 1's
own existing naming — only voice 1 had been needed by anything before
now. See `lib-reference.md` and `README.md`'s file table.

## `c64disasm.py`

A disassembler — the other direction from everything else in this
project, turning a `.prg` back into `c64asm.py`-compatible source. A
genuinely different problem from assembling: a raw binary doesn't say
which of its bytes are meant to be executed as instructions and which
are data that just happens to sit in the same address space, so this
follows actual code flow from the program's entry point (branches,
jumps, and calls, recursively) rather than blindly decoding byte by
byte, and shows anything never reached that way as plain `.byte` data
— the honest answer for a data byte, even when it isn't the most
informative one. Also detects and reconstructs printable PETSCII text
runs as readable `.text "..."` lines, self-verified against `c64asm.py`'s
own encoding function before ever being used, so a wrong guess simply
falls back to `.byte` instead of risking incorrect output.

Deliberately a single Python tool, not matched across three
implementations the way `c64asm.py` itself is — see `c64disasm.py`'s
own header comment for the full reasoning. Its correctness test is
disassembling and reassembling every `.prg` this project ships — seven
real demos plus the illegal-opcode showcase — and confirming
byte-for-byte identical output to the original across all three
`c64asm` builds, not a synthetic test written to be easy to pass. One
real bug came up during that testing and was fixed before shipping:
an early version generated a `jsr $FFD2`-style KERNAL call as a
reference to a *generated* label that was never actually defined
anywhere in the output, since the real target address falls well
outside the disassembled file's own range — now any jump/call/branch
target outside the file becomes a plain hex address instead of an
undefined symbol. See `README.md`'s "Disassembler" section.

## `GETTING-STARTED.md`

A short, example-driven walkthrough distinct from `c64asm-
reference.md`'s exhaustive syntax reference — three verified,
runnable examples (a minimal border-color program, a deliberate error
to show the error-reporting format, and a standard-library "hello
world" using `lib/text.inc`) building from nothing to a working
program, then a map pointing at the rest of the project's
documentation. Every example and command in it was actually run
before being written down, including the multi-file build commands
for all three implementations — one of which surfaced a real bug in
the example itself (a bare `.basic` with no explicit label, placed
right before a `.include`, silently jumped into the library's own
code instead of the program's), which became the guide's own
explanation of that exact gotcha rather than a mistake quietly fixed
and forgotten.

## `lib/math.inc`: `MULT_3`/`MULT_5`/`MULT_6`/`MULT_7`/`MULT_9`/`MULT_10`/`MULT_12`

Closes a real gap `MULT_2`/`MULT_4`/`MULT_8`/`MULT_16` left open: a
`.struct` that comes out to a non-power-of-two size — 6 bytes, like
`c64asm-reference.md`'s own `Room` example — had no library support at
all until now. Each is a shift-and-add (or, for `MULT_7`,
shift-and-subtract) built from the same power-of-two shifts the
existing macros already use — `MULT_6` is `(A*4) + (A*2)`, for
instance. Verified against 77 real, runtime-executed test cases (7
macros × 11 input values each, checked via `mini6502.py`) before
shipping, not just checked for correct assembly.

Unlike the power-of-two macros, these need one byte of zero page
(`mult_scratch`) — the 6502 has no register-to-register arithmetic at
all, so combining two shifted copies of `A` needs somewhere
addressable to hold one of them while the other stays in `A`. There's
deliberately no non-power-of-two `DIV_N`: dividing by an arbitrary
small constant needs real division logic, not a short shift-and-add,
which is a meaningfully bigger, slower thing than anything else in
this file — seemed better to leave it out and say so than ship
something half-baked. `c64asm-reference.md` §10's own `Room` example
now uses `MULT_6` directly instead of noting that nothing in the
library applied to it. See `lib/math.inc`'s own header comment for the
complete reasoning, and `c64asm-reference.md` §10.

## Cycle counts in `--listing`

Every assembled instruction in the listing file now shows how many
cycles it takes, cross-checked entry-by-entry against 6502.org's
published NMOS 6502/6510 timing table rather than reconstructed from
memory, given how easy this is to get subtly wrong. Handles the two
well-known variable cases explicitly instead of guessing: `4/5` for a
*read* instruction (`lda`, `cmp`, and similar) using indexed
addressing that might cross a page boundary at runtime, and `2/3/4`
for a conditional branch (not taken / taken / taken across a page).
Correctly excludes the page-crossing bonus for writes (`sta`) and
read-modify-write instructions (`inc`, `asl`, and similar) using
indexed addressing, which always take a fixed number of cycles
regardless — confirmed directly in test output that `lda $1000,x`
shows `4/5` while `sta $2000,x` shows a plain `5`, not `4/5`, despite
using the same addressing mode. Illegal/undocumented opcodes are
deliberately left blank rather than guessed. Along the way, found and
fixed a small pre-existing inconsistency between the Python and C
implementations' listing output (C wasn't trimming all trailing
whitespace from the source column, only newlines) that had never
surfaced before since nothing previously compared listing file
*content* between implementations directly. See `c64asm-reference.md`
§19.

## `adventure.asm`: each `room_exits` row individually `.tag`'d

`.tag` checks one struct instance, not a whole array — but nothing
stops tagging each *element* of an array on its own, which turned out
to be a real, meaningful check `.struct` alone never provided:
`.struct` guarantees `Exits` itself is 4 fields, but says nothing
about whether any particular row in `room_exits` actually has 4
values written out. Before this, a mistyped row (three values instead
of four) assembled cleanly with no warning at all, silently shifting
every room after it by one byte — confirmed by deliberately
introducing exactly that mistake and watching it assemble without
complaint. Tagging each of the five rows individually closes that gap
— the same mistake now fails immediately, naming which row and by how
much it's off — while assembling to *exactly* the same bytes as
before, confirmed byte-for-byte identical to the prior shipped
`adventure.prg`, and re-verified against the full game-playing test
suite including the win-condition playthrough. See
`c64asm-reference.md` §12, "Tagging each element of an array".

## `.tag`/`.endtag`

Binds a data block to a `.struct`, automatically checking at
`.endtag` that the block is really that struct's size:

```asm
room_data: .tag Room
        .word room0_desc
        .byte FOREST, $ff, COTTAGE, $ff
.endtag
```

Replaces the manual pattern of an `_end` label plus `.assert
end_label - start_label == Name.size` with something automatic. Pure
observation, not transformation — unlike `.repeat`/`.struct`, `.tag`
doesn't capture or reshape the lines between `.tag` and `.endtag` at
all, it just records `pc` at each end and compares the difference, so
whatever's actually in between (`.byte`, `.word`, `.text`, `.incbin`,
ordinary instructions, anything) assembles completely normally on its
own terms. Recoverable, not fatal, the same as `.assert` — a wrong
`.tag` doesn't corrupt anything about the rest of the file the way a
malformed `.repeat`/`.struct`/`.macro` would, so there's no reason to
stop assembly over it. Checks a single struct instance's size only,
not an array of them — `adventure.asm`'s array-of-records `room_exits`
table still uses the `.assert`-based pattern for that case, documented
alongside `.tag` itself. See `c64asm-reference.md` §12.

## `lib/math.inc`: `DIV_2`/`DIV_4`/`DIV_8`/`DIV_16`

Truncating, unsigned division of the A register by a small power of
two, via right shifts (`LSR`) — the mirror of `MULT_N` below, for the
reverse operation: recovering a record index from a byte offset. See
`c64asm-reference.md` §10, "Indexing an array of records".

## `lib/math.inc`: `MULT_2`/`MULT_4`/`MULT_8`/`MULT_16`

Multiply the A register by a small power of two via left shifts
(`ASL`), since the 6502 has no multiply instruction — mainly for
indexing into an array of `.struct` records. `adventure.asm`'s
room-navigation code now uses `MULT_4` in place of two hand-written
`asl a` lines. See `c64asm-reference.md` §10.

## `--warn-unused-all`, and `--warn-unused` scoped to the main file

`--warn-unused` now only reports symbols defined in the file named on
the command line, not anything it `.include`s, with a one-line count
of how many more were suppressed. `--warn-unused-all` restores the
original, unscoped behavior for anyone who wants the full picture. The
scoping exists because the original, unscoped version reported so much
routine library noise — 184 warnings against `demo.asm`, almost
entirely unused `keyboard.inc` constants for keys that particular demo
never checks — that it was hard to use in practice on any program
built on the standard library. See `c64asm-reference.md` §21.

## `--warn-unused`

Warns, after assembly finishes, about every label or `=`/`.equ`
constant (including `.struct` fields) defined but never referenced
anywhere in the program. Opt-in, never fails the build. See
`c64asm-reference.md` §21.

## `.assert`, and `==`/`!=` expression operators

`.assert condition[, "message"]` fails assembly (recoverably, the same
as `.error`) if `condition` evaluates to false — a compile-time
sanity check, most usefully paired with a `.struct`'s `Name.size`
(catching a struct that gained or lost a field out from under code
that assumed a specific size, say). Needed a real equality operator to
be useful for that, so `==` and `!=` were added to the expression
grammar at the same time — deliberately not `<`/`>`/`<=`/`>=` as
binary comparisons, since `<`/`>` are already unary low/high-byte
operators here, and overloading them for both meanings would be
genuinely ambiguous to parse. `adventure.asm`'s
`compute_room_exits_offset` now carries an `.assert` guarding its
hand-rolled multiply against exactly that kind of drift. See
`c64asm-reference.md` §11 and §4.

## `sprites.asm`: a real demo using `.incbin`

A 4-frame sprite animation loaded from `star_anim.bin`, an external
binary asset, via `.incbin`'s offset/length slicing — instead of the
hand-transcribed `.byte` sprite data every other demo uses. Same
W/A/S/D movement and border stop as `demo.asm`'s own star, plus
continuous frame-cycling independent of movement. See `README.md`'s
file table.

## `.struct`

Named byte offsets into a data record (`Room.north` instead of a bare
offset number) — `.byte`/`.word`/`.res` field declarations inside a
`.struct`/`.endstruct` block, producing `Name.field` symbols plus an
automatic `Name.size`. `adventure.asm`'s `room_exits` table was
converted from four separate parallel arrays
(`exit_north`/`exit_south`/`exit_east`/`exit_west`, one per direction)
into a single `.struct`-based combined table, indexed per room.
Required widening identifier parsing to allow a dot mid-symbol-name,
in exactly the two places that actually needed it, confirmed not to
change behavior for any previously-valid program (a dotted identifier
was always a hard parse error before). See `c64asm-reference.md` §10.

## `.incbin`

Imports raw bytes from an external binary file directly into the
assembled output, with optional `offset`/`length` arguments to slice
out part of a larger file — for sprite/font/music data made in an
external tool, instead of hand-transcribing it as `.byte` lists.
Reuses `.include`'s own path resolution rules (relative to the
including file first, `--lib-dir` as a fallback). Every error
`.incbin` can produce is fatal, not recoverable, deliberately unlike
`.byte`'s undefined-symbol handling: an `.incbin` problem means the
assembler doesn't know the emitted byte *count*, which would corrupt
every address computed after it if assembly tried to continue. See
`c64asm-reference.md` §14.

## `.repeat`/`.dup`

Assembles a block of code `count` times at assembly time, with an
optional index available inside the block via the same `\param`-style
substitution macro parameters already use. `.dup`/`.enddup` are exact
synonyms for `.repeat`/`.endrepeat`. Implemented as an anonymous macro
immediately invoked `count` times in a row, reusing the macro system's
own per-invocation local-label scoping rather than building a parallel
mechanism. `count` must be a plain integer literal, not a symbol or
expression, since `.repeat` is expanded during the same preprocessing
pass as `.macro`/`.include`, entirely before pass 1 builds a symbol
table. See `c64asm-reference.md` §9.

## `.error`/`.warning`

`.error "message"` records a recoverable error with a custom message;
`.warning "message"` prints a message without affecting whether
assembly succeeds. Typically paired with `.ifdef`/`.ifndef` to check a
precondition — every file in the standard library now checks its own
required zero-page symbols this way, right at the top, turning a
confusing `Undefined symbol` error surfacing three files deep into one
clear message naming exactly what's missing. Building this out
surfaced a real, pre-existing bug: an earlier `demo.asm` checked for a
"Y (restart)" keypress using a keyboard-matrix value that was actually
the **5** key's position, not Y's — found while cross-checking against
`keyboard.inc`'s new named key constants, which made the mismatch
visible for the first time; fixed in both `demo.asm` and its test.
See `c64asm-reference.md` §15.

## `--vice-labels`

Writes a VICE monitor label file (one `add_label` command per symbol),
for debugging by name in the VICE monitor — `break .main_loop` instead
of `break $0a60`, and disassembly showing names instead of bare
addresses. See `c64asm-reference.md` §20.

## `keyboard.inc`

Named `KEY_<name>_COL`/`KEY_<name>_ROW`/`KEY_<name>_CODE` constants for
every key on the C64 keyboard matrix, verified against the standard
published matrix reference, plus `wait_any_key` for a blocking "wait
for any key, return which one" read. Writing this out surfaced the
`demo.asm` Y-key bug described above.

## `demo.asm`: WASD movement, Q to exit

The star sprite now moves with W/A/S/D, stopping at the screen edges,
with a short sound on each successful move; the exit key changed from
Y to Q, with updated on-screen instructions.

## `demo.asm`: a visible sprite

The demo's sprite data was previously all zero bytes — invisible, even
though sprite hardware setup was otherwise correct. Replaced with a
small hand-drawn star.

## `demo.asm`: wait for a keypress before switching to bitmap mode

Previously, `demo.asm` switched to bitmap mode immediately after
printing its welcome text, wiping the screen before there was any real
chance to read it. Now waits for a keypress first, with on-screen
instructions explaining the controls.

## `.charset upper`/`.charset lower`

Controls how `.text`/`.asc`/`.byte` string literals encode letters:
forced-uppercase (`upper`, the default, matching every C64 program's
default display charset) or case-preserving (`lower`, once a program
has switched to the C64's alternate character set at runtime).
`adventure.asm`'s room descriptions were converted to natural mixed
case using this. See `c64asm-reference.md` §6.

## `.text`/`.byte` mixed strings and numeric bytes

`.text`/`.asc` (and quoted-string arguments to `.byte`) accept
comma-separated numeric bytes alongside quoted strings on the same
line — `.text "HELLO", 13, 0` instead of a separate `.text "HELLO"`
followed by `.byte 13, 0`. See `c64asm-reference.md` §7.

## Illegal/undocumented 6502/6510 opcode support

`.cpu 6510x` enables the well-known undocumented NMOS 6502/6510
opcodes (`LAX`, `SAX`, `DCP`, and others); `.cpu 6510` (the default)
or `.cpu 6502` turns support back off. Off by default, since these
aren't part of the documented instruction set. See
`c64asm-reference.md` §17 and `c64asm-opcode-reference.md`.

## Multi-error reporting

Most kinds of mistake — an undefined symbol, a malformed expression,
an unsupported addressing mode, a branch out of range, a redefined
symbol — no longer stop assembly at the first one found. Independent
problems are collected (up to 20 per run) and reported together in one
pass, closer to how a modern compiler behaves. A smaller category of
whole-file structural problems (a missing `.include`d file, a
malformed `.macro`, and similar) still stops assembly immediately,
since the shape of the rest of the file becomes genuinely ambiguous
once one of those is true. See `c64asm-reference.md` §22.

## Foundational feature set

Everything else the assembler could already do by the time the entries
above began: the full NMOS 6502/6510 instruction set (all 56
documented mnemonics, every addressing mode), automatic zero-page vs.
absolute addressing selection, two-pass assembly with forward
references, a real expression evaluator (`+ - * /`, parentheses, unary
`<`/`>` for low/high byte, `$hex`/`%binary`/decimal/`'char'` literals,
`*` for the current program counter), macros (`.macro`/`.endmacro`)
with named parameter substitution and recursive invocation, local
labels (`@label`) automatically scoped per macro invocation, `.include`
with automatic include-once semantics and circular-include detection,
conditional assembly (`.if`/`.elif`/`.else`/`.endif`,
`.ifdef`/`.ifndef`), the `.prg` output format, `--listing` output, and
the original standard library files (`hardware.inc`, `text.inc`,
`input.inc`, `graphics.inc`, `sound.inc`) extracted from and
cross-checked against this project's own demo programs
(`hello.asm`, `bounce.asm`, `pong.asm`, `adventure.asm`, `lander.asm`).
See `c64asm-reference.md` and `README.md` for the full, current
picture of all of it.
