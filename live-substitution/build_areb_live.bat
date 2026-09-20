@echo off
REM ===========================================================================
Rem  live-run proof for change 259 (ntdll!RtlAreBitsSet, RtlAreBitsClear).
REM
Rem  Both exports are patched one at a time, each with its own counter: they
REM  are separate code in ntdll and one implementation serves both here, so a
REM  wrapper routing one through the other would otherwise go unnoticed. The
Rem  corpus is built to answer yes as well as no -- a range check that only
REM  ever said no would prove nothing about the loop.
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of ntdll. No system process is touched, nothing on disk is modified,
REM  and the prologue is restored and verified byte-for-byte.
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foareb.obj "%C%\259-rtlarebitsset\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_areb.c areb.obj /Fe:live_subst_areb.exe >nul || goto :err
"%H%live_subst_areb.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
