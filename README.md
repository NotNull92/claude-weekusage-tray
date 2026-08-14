# ClaudeWeekUsageTray

A small Windows tray program that shows how much of your Claude Code
subscription limit is left.

<img src="docs/tray-icon.png" width="72" alt="Tray icon showing 73"> &nbsp;&nbsp;
<img src="docs/panel.png" width="300" alt="Detail panel showing 73% and 59% remaining">

The number in the notification area is the **percentage of your 5-hour limit
still available**. Click it for a panel with both windows and their reset
times.

Read in other languages: [한국어](README.ko.md)

## What it does, and what it will not do

It shows usage. That is all it does.

The only data it ever holds is four numbers that Claude Code already hands to
its status-line command: the used percentage and reset time for the 5-hour
window and for the 7-day window.

It does **not**:

- read `~/.claude/.credentials.json`, Windows Credential Manager, browser
  storage, or any environment API key
- read or store an OAuth token, a session id, or a conversation transcript
- call `api.anthropic.com` or any other network service
- install an updater, register itself to start with Windows, or send telemetry

There is no login flow, because there is nothing to log in to. Claude Code owns
your sign-in; this program only listens for numbers Claude Code chooses to
send. See [SECURITY.md](SECURITY.md) for the full boundary and how to verify it
yourself.

## Requirements

- Windows 10 or 11, 64-bit
- Claude Code installed, signed in, and actually running

Usage figures only arrive while Claude Code is running. Nothing refreshes on
its own while Claude Code is closed, and the program says so rather than
pretending otherwise.

## Install

1. Download the release ZIP and unpack it somewhere you intend to keep it, for
   example `C:\Tools\ClaudeWeekUsageTray`. It holds two files,
   `ClaudeWeekUsageTray.exe` and `uninstall.cmd`. The program records the path
   it runs from, so moving it later means running setup again.
2. Check the download against the `SHA256SUMS-v*.txt` published beside it:

   ```powershell
   Get-FileHash .\ClaudeWeekUsageTray.exe -Algorithm SHA256
   ```

3. Run `ClaudeWeekUsageTray.exe`.

   Claude Code has to be told to send usage, so on startup the program checks
   whether that is set up and offers to do it if not. Answer **Yes** and it
   writes the setting for you, after backing your settings file up. If you
   already run your own status-line command it says so first, and keeps that
   command.

4. Use Claude Code. The number appears as soon as Claude Code draws its status
   line, which is immediately once you send a message.

Until the first payload arrives the tray shows `--`. That is the honest state,
not an error.

If you prefer to do the setup yourself, or want it scripted, the same thing is
available without the prompt:

```powershell
.\ClaudeWeekUsageTray.exe --setup
```

Moving the folder later breaks the link, because Claude Code stores the path it
should run. Start the program from its new home and it notices and offers to
point Claude Code at the new location.

### The EXE is not code-signed

The executable is unsigned. Windows SmartScreen will warn you the first time
you run it. If that is not acceptable to you, build from source yourself with
`build.cmd`; the build needs nothing but Visual Studio's C++ tools.

## Make the icon visible

Windows 11 hides new tray icons behind the chevron by default. To pin it:

**Settings → Personalization → Taskbar → Other system tray icons**, then turn
**ClaudeWeekUsageTray** on.

Until you do that the icon lives in the overflow flyout that opens when you
click the `^` chevron next to the clock.

## Using it

| Action | Result |
| --- | --- |
| Left-click the icon | Show or hide the detail panel |
| Right-click the icon | Menu with **Show panel** and **Exit** |
| Run the program again | Opens the panel of the copy already running |
| Close the panel | Hides the panel; the program keeps running |
| **Exit** in the menu | Stops the program |

The panel shows both windows as remaining percentage, both reset times, and
when the figures last arrived. If nothing has arrived for a while the panel
says so plainly and the tray number dims. A stale figure is never presented as
a fresh reading.

## If you already use a status line

Claude Code allows one `statusLine` command. If you already have one, `--setup`
refuses to touch it and tells you what it would have done:

```powershell
.\ClaudeWeekUsageTray.exe --setup
```

To keep your command and add the tray on top of it:

```powershell
.\ClaudeWeekUsageTray.exe --setup --wrap-existing
```

Your command still runs, receives exactly the same input, and its output is
printed unchanged. The helper simply also forwards the four numbers to the
tray. Your original command is recorded so it can be put back.

Either way `settings.json` is backed up to
`settings.json.cwut-backup-<timestamp>` before anything is written, and any
other keys you had, such as `padding`, are preserved.

To undo:

```powershell
.\ClaudeWeekUsageTray.exe --remove-statusline
```

If your `statusLine` entry is in a shape the setup does not recognise, it
changes nothing and prints the lines to add by hand.

## Cleaning up duplicate tray entries

Windows remembers a notification-area entry per executable path, so moving or
rebuilding the program can leave stale entries in the settings list. To see
them:

```powershell
.\ClaudeWeekUsageTray.exe --cleanup-tray-icons
```

To remove the stale ones:

```powershell
.\ClaudeWeekUsageTray.exe --cleanup-tray-icons --apply
```

This only touches `HKEY_CURRENT_USER\Control Panel\NotifyIconSettings`, only
entries whose executable is `ClaudeWeekUsageTray.exe`, and only in your own
account. It writes a `.reg` backup first so the change can be undone, and it
never deletes a file.

## Uninstall

```powershell
.\uninstall.cmd
```

That stops the tray icon, puts your status-line setting back the way it was,
and clears this program's notification-area entries, writing a `.reg` backup
first. It deletes no files, so afterwards delete the folder yourself, plus
`%LOCALAPPDATA%\ClaudeWeekUsageTray` if you do not want to keep the backups.

Nothing else is left behind. There is no installer, no service, and no
registry key outside the notification-area entry Windows creates for any tray
program.

## Building from source

```powershell
.\build.cmd
.\build\ClaudeWeekUsageTray.exe --self-test
pwsh -File .\tools\security-scan.ps1
```

You need Visual Studio 2019 or later with **Desktop development with C++**.
There is no other dependency: native Win32 and C++17, static CRT, no .NET, no
third-party library. Everything links into one executable. `build.cmd clean`
removes the output.

To produce the release ZIP and its `SHA256SUMS-v*.txt`:

```powershell
pwsh -File .\tools\package.ps1
```

## How it works

Claude Code runs its `statusLine` command on every render and pipes a JSON
payload to it on stdin. That command is this same executable started as
`ClaudeWeekUsageTray.exe --statusline`. In that mode it reads the payload,
picks out four numbers, and sends them to the running tray over a loopback TCP
connection guarded by a 256-bit token stored in a file readable only by your
account. Nothing else in the payload is read, kept, or forwarded.

One caveat if you try that by hand: PowerShell does not wait for a
GUI-subsystem program, so `echo '{...}' | .\ClaudeWeekUsageTray.exe
--statusline` returns before the output arrives. Claude Code reads the pipe
properly. To see it yourself, run it through `cmd /c` instead.

This is event-driven. The tray updates when Claude Code sends something. A
30-second timer inside the tray only re-renders what it already has, so the
glyph dims and the wording turns stale on time; it does not fetch anything.

[DESIGN.md](DESIGN.md) covers the rest.

## Licence

MIT. See [LICENSE](LICENSE).
