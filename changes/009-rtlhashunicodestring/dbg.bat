@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
cd /d "%~dp0"
ml64 /nologo /c impl.asm
echo ml64=%ERRORLEVEL%
cl /nologo /O2 correctness.c reference.c upcase.c impl.obj /Fe:correctness.exe
echo cl=%ERRORLEVEL%
if exist correctness.exe correctness.exe
echo run=%ERRORLEVEL%
