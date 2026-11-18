@echo off
rem Starts the hl360 baseline build (ReXGlue) with a COPY of our Baseline profile (the one with the Fiat and the
rem user's liveries), for comparing vinyls with our build (session 42). The copy lives in hl360's user-fiat folder
rem (reference\rexglue recomp reference\hl360-forza3\user-fiat: the save under the same XUID, hl360's own headers,
rem the DLC folder linked from its user folder), so hl360's own Baseline save and our out\user are never touched.
rem To refresh the copy from our current save, delete user-fiat\E0000BA5E11E0001 and ask Claude (NOTES session 42).
setlocal
set "ROOT=%~dp0"
set "HL360=%ROOT%reference\rexglue recomp reference\hl360-forza3"
set "EXE=%HL360%\out\build\win-amd64-release"
if not exist "%EXE%\fm3.exe" (
    echo The hl360 build is missing: %EXE%\fm3.exe
    pause
    exit /b 1
)
if not exist "%HL360%\user-fiat\E0000BA5E11E0001" (
    echo The profile copy is missing: %HL360%\user-fiat
    pause
    exit /b 1
)
tasklist /fi "imagename eq fm3.exe" 2>nul | find /i "fm3.exe" >nul
if not errorlevel 1 (
    echo An fm3.exe is already running ^(ours or hl360^). Close it first.
    pause
    exit /b 1
)
cd /d "%EXE%"
start "" "%EXE%\fm3.exe" "--user_data_root=%HL360%\user-fiat"
