@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 255 (ntdll!RtlFindLongestRunClear).
REM
REM  Sacrificial single-threaded child; it patches only its own copy-on-write
REM  copy of ntdll. No system process is touched, nothing on disk is modified,
REM  and the prologue is restored and verified byte-for-byte.
REM
REM  BOTH observables are compared -- the returned length AND the written
REM  *StartingIndex -- because the index is where the tie-break lives, and a
REM  harness that checked only the length would pass an implementation that
REM  updated its best on ">=" instead of ">".
REM ===========================================================================
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
set H=%~dp0
set C=%H%..\changes
cd /d "%H%"
ml64 /nologo /c /Foflrc.obj "%C%\255-rtlfindlongestrunclear\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_flrc.c flrc.obj /Fe:live_subst_flrc.exe >nul || goto :err
"%H%live_subst_flrc.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
