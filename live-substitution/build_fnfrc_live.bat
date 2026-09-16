@echo off
REM ===========================================================================
REM  LIVE-RUN PROOF for change 261 (ntdll!RtlFindNextForwardRunClear,
REM  ntdll!RtlFindLastBackwardRunClear).
REM
REM  Both exports are patched ONE AT A TIME, each with its own counter: they
REM  scan in OPPOSITE directions and clip the run at opposite ends, so an
REM  implementation that routed one through the other would answer wrongly
REM  rather than merely go unnoticed.
REM
REM  Both the returned LENGTH and the WRITTEN START are compared, and the start
REM  is poisoned before every call -- "nothing found" still writes it, and the
REM  two forms write different values there.
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
ml64 /nologo /c /Fofnfrc.obj "%C%\261-rtlfindnextforwardrunclear\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_fnfrc.c fnfrc.obj /Fe:live_subst_fnfrc.exe >nul || goto :err
"%H%live_subst_fnfrc.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
