@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 256 (ntdll!RtlFindSetBits, RtlFindClearBits).
REM
REM  Both exports are patched ONE AT A TIME, each driven through its own name
REM  with its own counter: they are separate code in ntdll and one
REM  implementation serves both here, so a wrapper routing one through the
REM  other would otherwise go unnoticed.
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
ml64 /nologo /c /Fofsb.obj "%C%\256-rtlfindsetbits\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_fsb.c fsb.obj /Fe:live_subst_fsb.exe >nul || goto :err
"%H%live_subst_fsb.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
