@echo off
REM ===========================================================================
Rem  live-run proof for change 265 (ntdll!RtlAppendAsciizToString).
REM
REM  The whole destination buffer is compared, not the status and not Length.
REM  This export writes into a caller's buffer and never writes a terminator --
REM  unlike its wide sibling, which change 101 landed and which does, so the
REM  buffer is poison-filled and folded into a 64-bit hash for every case.
REM
REM  One case in three is built NOT to fit, because a successful append and a
REM  refusal are different paths and only the refusal may leave the buffer
REM  alone. The run counts both and fails if either is thin.
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
ml64 /nologo /c /Foappasciiz.obj "%C%\265-rtlappendasciiztostring\impl.asm" >nul || goto :err
cl /nologo /O2 live_subst_appasciiz.c appasciiz.obj /Fe:live_subst_appasciiz.exe >nul || goto :err
"%H%live_subst_appasciiz.exe"
endlocal & exit /b %errorlevel%
:err
echo BUILD ERROR & endlocal & exit /b 1
