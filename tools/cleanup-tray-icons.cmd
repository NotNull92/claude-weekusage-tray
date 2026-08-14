@echo off
rem Reports leftover notification-area entries for ClaudeWeekUsageTray in the
rem current user's own settings. Pass --apply to remove the stale ones; a .reg
rem backup is written first and no files are ever deleted.
"%~dp0ClaudeWeekUsageTray.exe" --cleanup-tray-icons %*
