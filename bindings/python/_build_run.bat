@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set DISTUTILS_USE_SDK=1
set MSSdk=1
cd /d C:\Users\aurel\Desktop\leekwars_generator_c\bindings\python
python setup.py build_ext --inplace > _build.log 2>&1
exit /b %ERRORLEVEL%
