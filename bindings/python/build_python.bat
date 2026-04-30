@echo off
REM Build the leekwars_c Python extension. Sources VS 2026 vcvars.

call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul

set DISTUTILS_USE_SDK=1
set MSSdk=1

cd /d "%~dp0"
python setup.py build_ext --inplace
exit /b %ERRORLEVEL%
