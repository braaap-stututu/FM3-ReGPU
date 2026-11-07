@echo off
rem Runs a command with the VS 2022 Build Tools x64 environment and LLVM in front of PATH
rem (LLVM is not on the system PATH; clang-cl / lld-link come from C:\Program Files\LLVM\bin).
rem   tools\vcenv.cmd cmake ...    tools\vcenv.cmd ninja -C build/tools
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=C:\Program Files\LLVM\bin;%PATH%"
%*
