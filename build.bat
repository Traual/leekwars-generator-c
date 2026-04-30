@echo off
REM Build the C engine + tests using the cmake bundled with VS 2026
REM Build Tools. Sources vcvars64 first so cl.exe and friends are found.

setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

set CMAKE="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set CTEST="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

cd /d "%~dp0"
%CMAKE% -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :err
%CMAKE% --build build
if errorlevel 1 goto :err
%CTEST% --test-dir build --output-on-failure
exit /b 0

:err
echo === build failed ===
exit /b 1
