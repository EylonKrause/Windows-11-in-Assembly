@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
cd /d "%~dp0"
cl /nologo /O2 live_subst.c ref_wcslen.obj ref_memchr.obj ref_wcschr.obj ref_wcscmp.obj wcslen.obj memchr.obj wcschr.obj wcscmp.obj /Fe:live_subst.exe
echo exit=%ERRORLEVEL%
