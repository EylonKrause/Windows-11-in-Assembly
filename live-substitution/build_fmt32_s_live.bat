@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for changes 198-201 -- the bounded 32-bit formatter family.
REM  SIX exports, four implementations: _ltoa_s / _ltow_s are patched and driven
REM  separately rather than assumed to be _itoa_s / _itow_s.
REM  /MD is REQUIRED: errno and the invalid-parameter handler must be UCRTBASE's --
REM  the same ones our assembly writes through via its exported _errno /
REM  _invalid_parameter_noinfo. With the static CRT the live export __fastfails.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fof32_itoa.obj  "%C%\198-itoa-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fof32_ultoa.obj "%C%\199-ultoa-s\impl.asm" >nul || goto :err
ml64 /nologo /c /Fof32_itow.obj  "%C%\200-itow-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fof32_ultow.obj "%C%\201-ultow-s\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_fmt32_s.c f32_itoa.obj f32_ultoa.obj f32_itow.obj f32_ultow.obj /Fe:live_subst_fmt32_s.exe >nul || goto :err
"%H%live_subst_fmt32_s.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
