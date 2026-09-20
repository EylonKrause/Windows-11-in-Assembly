@echo off
REM ===========================================================================
Rem  live-run proof for changes 182-185 (_strset_s, _wcsset_s, _strnset_s, _wcsnset_s).
Rem  /md is required: with the default static CRT this exe would carry its own
REM  invalid-parameter handler state, so the live exports and our assembly (which
REM  calls ucrtbase's _invalid_parameter_noinfo) would consult two different
REM  handlers; the static one is unset, so the live export __fastfails the
REM  process (exit code 9, no output). Changes 150, 178 and 182 record the trap.
REM  Sacrificial single-threaded child; no system process is touched, nothing on
REM  disk is modified.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Focf_ss.obj "%C%\182-strset-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Focf_ws.obj "%C%\183-wcsset-s\impl.asm"  >nul || goto :err
ml64 /nologo /c /Focf_sn.obj "%C%\184-strnset-s\impl.asm" >nul || goto :err
ml64 /nologo /c /Focf_wn.obj "%C%\185-wcsnset-s\impl.asm" >nul || goto :err
cl /nologo /O2 /MD live_subst_crt_fill.c cf_ss.obj cf_ws.obj cf_sn.obj cf_wn.obj /Fe:live_subst_crt_fill.exe >nul || goto :err
"%H%live_subst_crt_fill.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
