@echo off
rem Starts our build of FM3 (build\rel\fm3.exe) for testing: saves and settings in out\user, the log
rem in out\run\fm3.log, and ReGPU's debug server on (port 7341) so tools\fm3ctl.ps1 can attach.
rem Game data: the Disc 1 files hl360 installed (FM3_GAME_DIR overrides it).
setlocal
set "ROOT=%~dp0"
if not defined FM3_GAME_DIR set "FM3_GAME_DIR=%ROOT%reference\rexglue recomp reference\hl360-forza3\game\disc1"
set "FM3_USER_DIR=%ROOT%out\user"
set "REGPU_DEBUG=1"
if not exist "%ROOT%build\rel\fm3.exe" (
    echo build\rel\fm3.exe is missing: build it first ^(README.md^).
    pause
    exit /b 1
)
if not exist "%ROOT%out\run" mkdir "%ROOT%out\run"
cd /d "%ROOT%out\run"
start "" "%ROOT%build\rel\fm3.exe" "%FM3_GAME_DIR%"
