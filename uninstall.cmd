@echo off
rem Stops the tray icon, undoes the Claude Code status-line setup, and clears
rem this program's leftover notification-area entries for the current user.
rem It deletes no files: remove this folder yourself afterwards.
if not exist "%~dp0ClaudeWeekUsageTray.exe" (
  echo ClaudeWeekUsageTray.exe was not found next to this script.
  exit /b 1
)
"%~dp0ClaudeWeekUsageTray.exe" --uninstall
exit /b %errorlevel%
