@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 194 (_i64toa_s).
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
ml64 /nologo /c /Fofs_i64.obj "%C%\194-i64toa-s\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_fmt_s.c fs_i64.obj /Fe:live_subst_fmt_s.exe >nul || goto :err
"%H%live_subst_fmt_s.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
