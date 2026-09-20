@echo off
REM ===========================================================================
REM  Live substitution for nine ucrtbase string primitives:
REM    032 strlen  033 strcmp  036 wcsspn  037 wcscspn  039 strspn
REM    040 strcspn 041 wcsncmp 044 _wcsnicmp 045 _strnicmp
REM
REM  /MD so the process uses the SAME ucrtbase this harness patches -- with the
REM  static CRT the patched export and the CRT the test itself runs on would be
REM  two different copies, and the run would prove nothing about either.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"

ml64 /nologo /c /Focs032.obj "%C%\032-strlen\impl.asm"    >nul || goto :err
ml64 /nologo /c /Focs033.obj "%C%\033-strcmp\impl.asm"    >nul || goto :err
ml64 /nologo /c /Focs036.obj "%C%\036-wcsspn\impl.asm"    >nul || goto :err
ml64 /nologo /c /Focs037.obj "%C%\037-wcscspn\impl.asm"   >nul || goto :err
ml64 /nologo /c /Focs039.obj "%C%\039-strspn\impl.asm"    >nul || goto :err
ml64 /nologo /c /Focs040.obj "%C%\040-strcspn\impl.asm"   >nul || goto :err
ml64 /nologo /c /Focs041.obj "%C%\041-wcsncmp\impl.asm"   >nul || goto :err
ml64 /nologo /c /Focs044.obj "%C%\044-wcsnicmp\impl.asm"  >nul || goto :err
ml64 /nologo /c /Focs045.obj "%C%\045-strnicmp\impl.asm"  >nul || goto :err

cl /nologo /O2 /MD live_subst_crtstr.c cs032.obj cs033.obj cs036.obj cs037.obj cs039.obj ^
   cs040.obj cs041.obj cs044.obj cs045.obj /Fe:live_subst_crtstr.exe >nul || goto :err

"%H%live_subst_crtstr.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD/RUN ERROR & endlocal & exit /b 1
