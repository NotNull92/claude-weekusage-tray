# Security boundary

This program exists to display four numbers. The design goal was that it
should be impossible for it to leak a secret, because it never has one.

## What it is allowed to hold

| Field | Source | Kept where |
| --- | --- | --- |
| `five_hour.used_percentage` | Claude Code status-line payload | memory only |
| `five_hour.resets_at` | Claude Code status-line payload | memory only |
| `seven_day.used_percentage` | Claude Code status-line payload | memory only |
| `seven_day.resets_at` | Claude Code status-line payload | memory only |

Plus the local time each snapshot arrived, stamped by the tray itself rather
than taken from the sender.

Usage figures are never written to disk. Restarting the tray resets it to `--`
until Claude Code sends something new.

## What it never touches

- `~/.claude/.credentials.json` or any other credential file
- Windows Credential Manager (`CredRead`, `CredEnumerate`) and DPAPI
- browser storage, cookies, or profile directories
- `ANTHROPIC_API_KEY` or any other environment secret
- OAuth tokens, bearer headers, or `api.anthropic.com`
- session ids, conversation transcripts, or `.jsonl` history files

The status-line payload contains more than the rate limits. The parser reaches
for `rate_limits.five_hour` and `rate_limits.seven_day` and copies four numeric
values out of them. Every other key is left in the parsed document and
discarded when it goes out of scope. The one place data leaves status-line mode
is the wire message, which is six ASCII fields long and has nowhere to put
anything else.

## The local channel

The tray listens on `127.0.0.1` on an ephemeral port. It never binds
`INADDR_ANY`, so nothing outside the machine can reach it.

Authentication is a 256-bit token from `BCryptGenRandom`, published in
`%LOCALAPPDATA%\ClaudeWeekUsageTray\endpoint.json`. That file is created with a
protected DACL granting full access to the current user account and nobody
else, so a different account on the same machine cannot read the token and
cannot push fake usage into your tray. The token is compared without an early
exit.

Messages are capped at 512 bytes, connections that do not present the right
token get `NO` and are closed, and every value is re-validated on arrival even
though the sender already validated it.

The honest limit of this design: any process running **as you** can read that
file, and therefore can send usage numbers to your own tray. Defending against
code already running under your own account is not something a tray program can
do, and pretending otherwise would be worse than saying it plainly. The
consequence of that attack is a wrong number on your own screen.

## Validation rules

- `used_percentage` must be a JSON number. Strings, booleans, and nulls are
  rejected. It is clamped to 0–100 and rounded **up**, so the remaining figure
  never claims more headroom than you have.
- `resets_at` must be a JSON number inside 2020-01-01 … 2100-01-01, in seconds
  or milliseconds. Anything else is dropped and the panel shows
  "Reset time unavailable" rather than a made-up date.
- A payload with no usable window is rejected outright. The tray keeps showing
  `--`.
- An invalid reset time does not discard a valid percentage, and a valid
  seven-day figure never stands in for a missing five-hour one.

Every one of these rules has a test in `--self-test`.

## Verifying it yourself

```powershell
.\ClaudeWeekUsageTray.exe --self-test
pwsh -File .\tools\security-scan.ps1
```

`--self-test` includes a check that reads the shipped binary back off disk and
confirms the strings a credential-reading build would need are simply not
present, in both ASCII and UTF-16. `security-scan.ps1` does the same against
the source with comments stripped, and additionally asserts that the listener
is pinned to loopback and that the data model declares no field outside the
four scalars.

To watch it at runtime, Process Monitor filtered to `ClaudeWeekUsageTray.exe`
shows file activity limited to `%LOCALAPPDATA%\ClaudeWeekUsageTray`, and
`~/.claude/settings.json` only while `--setup`, `--remove-statusline`, or
`--uninstall` runs.

## Reporting a problem

Open an issue at
<https://github.com/NotNull92/claude-weekusage-tray/issues>. If it concerns the
boundary above rather than a display bug, please say so in the title.
