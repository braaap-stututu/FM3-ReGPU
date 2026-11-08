@echo off
rem Starts the FM3 launcher (launcher\FM3Launcher) for this checkout, with the same folders as play.cmd: game data
rem from hl360's Disc 1 (FM3_GAME_DIR overrides it), saves and settings in out\user. Builds the launcher first if
rem it isn't built yet (needs the .NET 9 SDK). The launcher starts build\rel\fm3.exe in out\run, so fm3.log and the
rem debug server's files land where play.cmd puts them.
setlocal
set "ROOT=%~dp0"
if not defined FM3_GAME_DIR set "FM3_GAME_DIR=%ROOT%reference\rexglue recomp reference\hl360-forza3\game\disc1"
set "FM3_USER_DIR=%ROOT%out\user"
rem The launcher's Profiles tab picks the profile (settings.ini), so no FM3_PROFILE here. A first settings.ini
rem plays Baseline: the old Player save is damaged (issue H).
if not exist "%FM3_USER_DIR%\settings.ini" (
    if not exist "%FM3_USER_DIR%" mkdir "%FM3_USER_DIR%"
    > "%FM3_USER_DIR%\settings.ini" echo profile=Baseline
)
set "EXE=%ROOT%launcher\FM3Launcher\bin\Release\net9.0\win-x64\FM3Launcher.exe"
if not exist "%EXE%" (
    dotnet build "%ROOT%launcher\FM3Launcher" -c Release --nologo || (pause & exit /b 1)
)
start "" "%EXE%"
