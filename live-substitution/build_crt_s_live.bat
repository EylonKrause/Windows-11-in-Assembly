@echo off
REM ===========================================================================
Rem  live-run proof for changes 178-181 (_wcsupr_s, _strlwr_s, _wcslwr_s, _strupr_s).
Rem  /md is required: with the default static CRT this exe would carry its own
REM  invalid-parameter handler state, so the live exports and our assembly (which
REM  calls ucrtbase's _invalid_parameter_noinfo) would consult two different
REM  handlers; the static one is unset, so the live export __fastfails the
REM  process (exit code 9, no output). Changes 150 and 178 record the same trap.
REM  Sacrificial single-threaded child; no system process is touched, nothing on
REM  disk is modified.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Focs_wus.obj "%C%\178-wcsupr-s\impl.asm" >nul || goto :err
ml64 /nologo /c /Focs_sls.obj "%C%\179-strlwr-s\impl.asm" >nul || goto :err
ml64 /nologo /c /Focs_wls.obj "%C%\180-wcslwr-s\impl.asm" >nul || goto :err
ml64 /nologo /c /Focs_sus.obj "%C%\181-strupr-s\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_crt_s.c cs_wus.obj cs_sls.obj cs_wls.obj cs_sus.obj /Fe:live_subst_crt_s.exe >nul || goto :err
"%H%live_subst_crt_s.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
