# Claude Weekly Usage Tray — Implementation Handoff

## Mission

Build a minimal native Windows x64 tray app named **ClaudeWeekUsageTray**.

It shows the user's Claude Code subscription usage without reading, storing, logging, or sending OAuth tokens, passwords, API keys, or session transcripts. Use only the usage values Claude Code delivers to a configured `statusLine` command.

This is a new, independent repository. Do not modify the sibling Codex repository.

## Git and remote ownership

The user wants **Claude** to create the initial and follow-up commits, create the GitHub remote repository, and push releases. Do not ask another agent to perform those Git or remote actions. Before creating the remote, confirm the target GitHub owner, repository visibility, and release authority with the user if they have not already supplied them.

## User-visible behavior

- Use a real Windows notification-area icon (`Shell_NotifyIcon`), never a taskbar window or an overlay near the clock.
- The tray glyph shows the **remaining 5-hour limit** as a large bold full integer: `100 - five_hour.used_percentage`. For example, 27% used displays `73`.
- If no usable live snapshot exists, display `--`.
- Left-click toggles the detail panel. Right-click opens a menu with **Show panel** and **Exit**.
- The detail panel identifies the product as **CLAUDE** and shows both windows:
  - **5-HOUR LIMIT** — remaining percentage and reset time.
  - **7-DAY LIMIT** — remaining percentage and reset time.
- Include the latest-update time and clearly mark stale data. Never imply a stale value is newly fetched.
- Close hides the panel; Exit stops the app.

## Required data path

Claude Code's custom `statusLine` command receives JSON on stdin. When the subscription data is available, it includes this shape:

```json
{
  "rate_limits": {
    "five_hour": { "used_percentage": 27, "resets_at": 1780000000 },
    "seven_day": { "used_percentage": 41, "resets_at": 1780400000 }
  }
}
```

Implement a tiny companion status-line command that forwards only those four scalar values to the running tray process over a local-only channel. It must preserve any existing user `statusLine` behavior rather than overwrite it. If safe composition is not possible, provide an explicit opt-in setup step and leave the existing configuration unchanged.

Treat `used_percentage` as used, clamp it to 0–100, and render remaining as `100 - used`. Treat `resets_at` as an epoch timestamp only after validating it. Missing, malformed, or partial fields must fail closed to `--` or “Unavailable”.

The primary path is event-driven: it updates when Claude Code sends a status-line payload. Do not pretend that a 5-second loop independently refreshes Claude usage while Claude Code is idle or closed.

## Explicit security boundary

Do **not** read `~/.claude/.credentials.json`, Windows Credential Manager, browser storage, Keychain, environment API keys, or any OAuth bearer token. Do **not** call `https://api.anthropic.com/api/oauth/usage` in this project unless the user later explicitly requests that separate feature and approves its security design.

The app may receive and retain only the four usage fields above in memory. Do not create an updater, downloader, startup registration, telemetry, analytics, or cloud service.

## Implementation constraints

- Native Win32 C++17; no .NET runtime and no third-party runtime dependency.
- Keep the resident process minimal. Prefer Win32/GDI and standard library facilities.
- Use a loopback-only IPC endpoint or another local process channel. Authenticate or scope it so arbitrary local processes cannot spoof usage into the tray.
- Avoid a browser login flow: Claude Code owns its own login. The app should explain that Claude Code must be installed, signed in, and configured for the status-line integration.
- Keep all customer-facing copy plain English.
- Provide a small `--self-test` and focused parser/IPC tests. Test both 5-hour and 7-day conversion, invalid values, full two-digit tray labels, stale state, click/menu behavior, and the no-token boundary.

## Setup, cleanup, and release

- Document how the user enables the system-tray icon in **Settings > Personalization > Taskbar > Other system tray icons**.
- Provide a reversible, current-user-only cleanup command for duplicate tray-icon settings. It must not delete the EXE or touch unrelated applications.
- Release ZIP: `ClaudeWeekUsageTray.exe`, any required status-line helper, cleanup command, and `SHA256SUMS` manifest. Do not include PDBs, a .NET runtime, or secrets.
- State clearly if the EXE is unsigned.

## Definition of done

1. With a valid Claude status-line payload showing 27% five-hour usage and 41% seven-day usage, the tray visibly shows `73`; the panel shows 73% and 59% remaining with their reset times.
2. With no payload or invalid data, the tray visibly shows `--` and the panel does not invent usage.
3. Existing Claude Code status-line configuration is preserved or the app refuses automatic setup and explains the opt-in step.
4. Source scan and runtime checks demonstrate that no OAuth token or credential file is read, stored, logged, or transmitted.
5. Native release build, tests, and self-test pass before release.
