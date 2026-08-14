// The status-line mode of the program: ClaudeWeekUsageTray.exe --statusline
#pragma once

namespace cwut {

// Reads the status-line payload from stdin, forwards exactly four numbers to a
// running tray, then prints either one short line of its own or the output of
// the status-line command the user already had.
//
// Always returns 0. A problem here must never break Claude Code's rendering.
int RunStatusLine();

}  // namespace cwut
