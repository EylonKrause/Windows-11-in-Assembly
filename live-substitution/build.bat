@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fowcslen.obj "%C%\001-wcslen\impl.asm" >nul || goto :err
ml64 /nologo /c /Fomemchr.obj "%C%\002-memchr\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowcschr.obj "%C%\003-wcschr\impl.asm" >nul || goto :err
ml64 /nologo /c /Fowcscmp.obj "%C%\004-wcscmp\impl.asm" >nul || goto :err
cl /nologo /O2 /c /Foref_wcslen.obj "%C%\001-wcslen\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_memchr.obj "%C%\002-memchr\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_wcschr.obj "%C%\003-wcschr\reference.c" >nul || goto :err
cl /nologo /O2 /c /Foref_wcscmp.obj "%C%\004-wcscmp\reference.c" >nul || goto :err
cl /nologo /O2 live_subst.c ref_wcslen.obj ref_memchr.obj ref_wcschr.obj ref_wcscmp.obj wcslen.obj memchr.obj wcschr.obj wcscmp.obj /Fe:live_subst.exe >nul || goto :err
"%H%live_subst.exe"
popd & endlocal & exit /b 0
:err
echo BUILD ERROR & popd & endlocal & exit /b 1
