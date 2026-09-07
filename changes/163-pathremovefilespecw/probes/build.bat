@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
cd /d "%~dp0"
if "%1"=="" (echo usage: build.bat ^<prfs^|prfs2^|prfs3^|prfs4^> & exit /b 1)
cl /nologo /O2 /MD %1.c /Fe:%1.exe shlwapi.lib >nul || exit /b 1
"%~dp0%1.exe"
