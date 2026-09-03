@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fo wcslen.obj "%C%\001-wcslen\impl.asm"
ml64 /nologo /c /Fo memchr.obj "%C%\002-memchr\impl.asm"
ml64 /nologo /c /Fo wcschr.obj "%C%\003-wcschr\impl.asm"
ml64 /nologo /c /Fo wcscmp.obj "%C%\004-wcscmp\impl.asm"
cl /nologo /O2 live_subst.c "%C%\001-wcslen\reference.c" "%C%\002-memchr\reference.c" "%C%\003-wcschr\reference.c" "%C%\004-wcscmp\reference.c" wcslen.obj memchr.obj wcschr.obj wcscmp.obj /Fe:live_subst.exe
echo cl_exit=%ERRORLEVEL%
