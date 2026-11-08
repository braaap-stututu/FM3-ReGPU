@echo off
rem Starts the hl360 baseline build (ReXGlue) for comparison testing. It reads fm3.user.toml beside its exe:
rem your Baseline profile (E0000BA5E11E0001), 1280x720 windowed, D3D12, frame stats on, texture dumps to
rem dumps\hl360 (create dumps\hl360\TRIGGER while it runs to capture a frame). Logs: its logs\ folder.
rem No debug server: tools\fm3ctl.ps1 only attaches to our build (play.cmd).
setlocal
set "ROOT=%~dp0"
set "HL360=%ROOT%reference\rexglue recomp reference\hl360-forza3\out\build\win-amd64-release"
if not exist "%HL360%\fm3.exe" (
    echo The hl360 build is missing: %HL360%\fm3.exe
    echo Build it from the hl360 folder: scripts\build_sdk.cmd, then scripts\build.cmd release.
    pause
    exit /b 1
)
rem Both builds are called fm3.exe: don't start a second game next to a running one.
tasklist /fi "imagename eq fm3.exe" 2>nul | find /i "fm3.exe" >nul
if not errorlevel 1 (
    echo An fm3.exe is already running ^(ours or hl360^). Close it first.
    pause
    exit /b 1
)
cd /d "%HL360%"
start "" "%HL360%\fm3.exe"
