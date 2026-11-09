@echo off
rem Installs Disc 2 (the Content Install Disc: 6 car packs and 2 track packs, ~3.4 GB) into out\user\dlc, the folder
rem play.cmd and launcher.cmd use, as the console installs it to the hard drive (launcher\FM3Launcher\Disc2.cs).
rem   install-disc2.cmd                      the "...Disc 2 (Content Install Disc).iso" in this folder
rem   install-disc2.cmd <image.iso | folder | package file>
rem The launcher's Play tab does the same ("Disc 2 content"). Close the game first.
setlocal
set "ROOT=%~dp0"
if not defined FM3_USER_DIR set "FM3_USER_DIR=%ROOT%out\user"
set "SRC=%~1"
if "%SRC%"=="" for %%f in ("%ROOT%*Disc 2*.iso") do set "SRC=%%~ff"
if "%SRC%"=="" (
    echo No Disc 2 image found next to this script: install-disc2.cmd ^<Disc 2 .iso, its folder, or a package file^>
    exit /b 1
)
set "EXE=%ROOT%launcher\FM3Launcher\bin\Release\net9.0\win-x64\FM3Launcher.exe"
if not exist "%EXE%" (
    dotnet build "%ROOT%launcher\FM3Launcher" -c Release --nologo || exit /b 1
)
echo Installing Disc 2 content from "%SRC%" into "%FM3_USER_DIR%\dlc"...
"%EXE%" --install-disc2 "%SRC%"
