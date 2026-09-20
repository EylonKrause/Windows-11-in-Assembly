@echo off
REM ===========================================================================
Rem  live-run proof for changes 194-197 -- the bounded 64-bit formatter family.
Rem  /md is required: errno and the invalid-parameter handler must be UCRTBASE's --
REM  the same ones our assembly writes through via its exported _errno /
REM  _invalid_parameter_noinfo. With the static CRT the live export __fastfails.
REM  Sacrificial single-threaded child; no system process is touched.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fofs_i64.obj  "%C%\194-i64toa-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fofs_u64.obj  "%C%\195-ui64toa-s\impl.asm" >nul || goto :err
ml64 /nologo /c /Fofs_i64w.obj "%C%\196-i64tow-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Fofs_u64w.obj "%C%\197-ui64tow-s\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_fmt_s.c fs_i64.obj fs_u64.obj fs_i64w.obj fs_u64w.obj /Fe:live_subst_fmt_s.exe >nul || goto :err
"%H%live_subst_fmt_s.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
