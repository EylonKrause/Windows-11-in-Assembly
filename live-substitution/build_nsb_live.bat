@echo off
REM ===========================================================================
Rem  live-run proof for change 257 (ntdll!RtlNumberOfSetBits and relatives).
REM
Rem  All four exports are patched one at a time, each driven through its own
REM  name with its own counter, because they are separate code in ntdll and a
REM  wrapper routing one through another would otherwise go unnoticed.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of ntdll. No system process is touched, nothing on disk is modified,
REM  and every prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Fonsb.obj "%C%\257-rtlnumberofsetbits\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_nsb.c nsb.obj /Fe:live_subst_nsb.exe >nul || goto :err
"%H%live_subst_nsb.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
