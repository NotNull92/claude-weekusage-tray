@echo off
setlocal enabledelayedexpansion

rem Builds ClaudeWeekUsageTray.exe with MSVC. One executable does everything:
rem the tray icon, the setup and cleanup commands, and the status-line mode
rem Claude Code runs. It links the static CRT, so no runtime redistributable
rem is needed.

set "ROOT=%~dp0"
set "OUT=%ROOT%build"
set "OBJ=%OUT%\obj"

if /i "%~1"=="clean" (
  if exist "%OUT%" rmdir /s /q "%OUT%"
  echo Cleaned.
  exit /b 0
)

call :find_vcvars
if not defined VCVARS (
  echo Could not find a Visual Studio x64 build environment.
  echo Install "Desktop development with C++" and run this script again.
  exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 (
  echo Failed to initialise the build environment.
  exit /b 1
)

if not exist "%OBJ%\tray" mkdir "%OBJ%\tray"

set "CFLAGS=/nologo /std:c++17 /EHsc /W4 /permissive- /O2 /MT /GS /Gy /utf-8 /DUNICODE /D_UNICODE /DNDEBUG /D_CRT_SECURE_NO_WARNINGS"
set "LFLAGS=/nologo /INCREMENTAL:NO /DEBUG:NONE /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /guard:cf"

echo Building ClaudeWeekUsageTray.exe ...
cl %CFLAGS% /guard:cf /Fo"%OBJ%\tray\\" /Fe"%OUT%\ClaudeWeekUsageTray.exe" ^
   "%ROOT%src\common\json.cpp" "%ROOT%src\common\usage.cpp" "%ROOT%src\common\winutil.cpp" ^
   "%ROOT%src\common\ipc.cpp" ^
   "%ROOT%src\tray\main.cpp" "%ROOT%src\tray\panel.cpp" "%ROOT%src\tray\trayicon.cpp" ^
   "%ROOT%src\tray\menu.cpp" "%ROOT%src\tray\theme.cpp" "%ROOT%src\tray\setupdialog.cpp" "%ROOT%src\tray\cli.cpp" "%ROOT%src\tray\selftest.cpp" ^
   "%ROOT%src\statusline\statusline.cpp" ^
   /link %LFLAGS% /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1

if exist "%ROOT%uninstall.cmd" copy /y "%ROOT%uninstall.cmd" "%OUT%\" >nul

echo.
echo Built:
echo   %OUT%\ClaudeWeekUsageTray.exe
exit /b 0

:find_vcvars
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
    if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if defined VCVARS exit /b 0
for %%e in (2022 2019) do (
  for %%p in ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") do (
    for %%d in (Enterprise Professional Community BuildTools) do (
      if not defined VCVARS if exist "%%~p\%%e\%%d\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=%%~p\%%e\%%d\VC\Auxiliary\Build\vcvars64.bat"
      )
    )
  )
)
exit /b 0
