@echo off
rem Runs a command with the build toolchain in front of PATH:
rem  - toolchain\env.cmd when tools\get-toolchain.ps1 has made it (portable MSVC, Windows SDK, LLVM, CMake, Ninja;
rem    GitHub issue #2), unless FM3_SYSTEM_TOOLCHAIN=1;
rem  - else the installed VS 2022 Build Tools x64 environment and LLVM (C:\Program Files\LLVM\bin, not on the system PATH).
rem   tools\vcenv.cmd cmake ...    tools\vcenv.cmd ninja -C build/tools
if not "%FM3_SYSTEM_TOOLCHAIN%"=="1" if exist "%~dp0..\toolchain\env.cmd" (
    call "%~dp0..\toolchain\env.cmd"
    goto run
)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
:run
%*
