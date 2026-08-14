# Design

## Shape

One executable with two lifetimes.

```
ClaudeWeekUsageTray.exe --statusline      ClaudeWeekUsageTray.exe
  started by Claude Code per render         resident, started by you
  reads stdin JSON                          Shell_NotifyIcon glyph
  extracts 4 scalars                        detail panel (GDI)
  sends over loopback  ------------------>  loopback listener
  runs the wrapped command                  setup / cleanup / uninstall
  exits                                     self-test
```

`src/common` holds what both modes need: a small JSON reader, the usage model
and its validation rules, the IPC wire format, and a handful of Windows
helpers. `src/tray` is the resident half, `src/statusline` the per-render half.

Native Win32 and C++17, static CRT, no .NET, no third-party library. The
resident process is a message loop, one listener thread, and two windows.

## Why two lifetimes, and why one file

Claude Code runs its `statusLine` command on every render, then throws it away.
That is the wrong lifetime for a tray icon, so the icon lives in a resident
process and the short-lived command only forwards to it.

They could be two executables, and were at first. One file ships instead,
because a release the user has to keep two matched binaries together is a
release that breaks when one of them is moved. The modes share the parser and
the wire format anyway, so splitting them bought nothing but a second file to
lose.

The single binary is GUI-subsystem, so starting the tray from Explorer never
flashes a console. That does not cost the status-line mode anything: stdin and
stdout are inherited handles, and a process gets them whether or not it has a
console. The only visible consequence is that PowerShell does not wait for a
GUI-subsystem child, which makes manual testing at a prompt look wrong even
though Claude Code reads the pipe correctly.

## Why loopback TCP

The requirement was a channel that is local-only and cannot be spoofed by an
arbitrary process. A named pipe with a security descriptor would work equally
well; loopback TCP was chosen because binding `127.0.0.1` with an ephemeral
port is unambiguous to reason about and easy to demonstrate in a test, and the
token file carries the same per-user DACL a named pipe would have needed.

The wire format is one line of six ASCII fields:

```
CWUT1 <64-hex-token> <fiveUsed> <fiveReset> <sevenUsed> <sevenReset>
```

`-` stands in for a field the payload did not supply. There is deliberately no
room in this format for anything but the four numbers. See SECURITY.md for the
authentication details.

## Why the glyph is drawn, not loaded

The tray shows a number that changes, so the icon is rendered on demand rather
than picked from a set of prepared images.

Drawing text straight into a 16-pixel bitmap does not work. GDI's hinting
reshapes bold digits at that size and the result looks broken. So nothing is
drawn at the final size: the label goes onto a canvas four times larger, where
the outlines are reproduced faithfully, and that is box-filtered down. White on
black with grayscale antialiasing, luminance read back as coverage. ClearType
is off for this draw, because subpixel colour fringes would corrupt the
coverage.

Box-filtering leaves most of a thin stem at partial coverage, which reads as a
grey smudge. A curve is applied afterwards that drops the faintest fringe and
pushes the rest towards solid, so the weight on screen matches the weight of
the typeface, which is Segoe UI Black.

Three decisions come out of measuring the ink rather than the font metrics:

- **Size.** The largest type whose ink fits wins, not the largest whose line
  box fits. Digits have no descenders, so fitting by line box would waste about
  a third of the height.
- **Position.** The glyph is centred on its ink, so it is optically centred
  instead of centred on a box with empty space in it.
- **Width.** Three digits cannot fit across a tray icon at a readable height,
  so `100` is condensed horizontally by up to 28% before the type size gives
  way. Squeezing it beats shrinking it, and both beat showing something that
  is not the real number.

The colour is the Claude accent orange, the same one the panel uses for its
bars, in a darker shade on a light taskbar so the contrast holds either way.

Stale data is drawn at reduced alpha. Dimming is a legitimate signal that the
number is old; changing the number would not be.

## Fail-closed rules

The parser has one job it must not get wrong: never invent a figure. So

- a payload that does not parse produces no snapshot at all
- a window with no numeric `used_percentage` is not a window
- `100 - used` is computed from a value already clamped to 0–100
- `used_percentage` rounds **up**, so remaining rounds down
- a bad `resets_at` costs you the reset line, not the percentage
- the tray glyph reads the five-hour window only; a seven-day figure never
  substitutes for it

`--self-test` covers each of these, plus the IPC round trip, token rejection,
oversized and malformed messages, the rendered glyph, the menu, the panel's
show/hide behaviour, and one end-to-end run of status-line mode through real
stdin and stdout pipes.

## Event-driven, and honest about it

Nothing polls Claude. The tray updates when a payload arrives, full stop. The
30-second timer inside the tray re-renders state the process already has, so
the glyph dims and the panel wording changes at the right moment. When Claude
Code is closed, the figures stop changing and the interface says the data is
stale rather than implying a fresh reading.

`kStaleAfterSeconds` is 15 minutes. Claude Code renders its status line
frequently while in use, so a gap that long means it is not running.

## Status-line composition

Claude Code allows a single `statusLine` command, which makes "add ours"
inherently destructive. The rules the setup follows:

1. No `statusLine` at all: write ours.
2. Already ours: do nothing and say so.
3. A `{ type: "command", command: "<string>" }` entry that is not ours: refuse,
   and print what `--wrap-existing` would do. With that flag, record the
   original command, install ours, and have status-line mode run the original
   with the same stdin and print its output unchanged.
4. Anything else, including a file that does not parse as JSON: change nothing
   and print the lines to add by hand.

All of it happens in one window. The offer, the outcome of accepting it, and
any error are three things to say, but three stock message boxes in a row is a
worse way to say them than one window that changes what it shows. So the
program draws its own: same palette, same type, same rounded shapes as the
detail panel, with a real primary button rather than a system Yes/No pair.
Long paths are folded at their separators first, because `DrawText` only breaks
at spaces and a Windows path has none, which silently swallows its middle.

A downloaded copy that is simply run does none of this, and would sit in the
tray showing `--` forever with no hint why. So the tray inspects the setting on
startup and, when usage cannot reach it, says so and offers to fix it. The
offer is the opt-in: nothing is written unless the answer is yes, and the same
four rules then apply. The check also catches the copy that was moved, or the
second copy downloaded to a new folder, where the setting still names a path
that is no longer the one running. That case re-points the command and leaves
the record of any wrapped user command alone, because that command is still
theirs and still wanted.

`settings.json` is backed up before any write, unrecognised keys inside
`statusLine` are carried over, and `--remove-statusline` puts the original
back.

## Things deliberately left out

An updater, a downloader, startup registration, telemetry, a settings UI, and
any notion of an account. Each of them would have widened the boundary in
SECURITY.md, and none of them is needed to show four numbers.
